/*
 * config_store.cpp  --  see config_store.h
 */
#include "config_store.h"
#include "config.h"       // GYRO_SCALE, WALL_THRESH_* (runtime)
#include "mouse_config.h"  // turn_params
#include "eeprom.h"
#include "report.h"
#include <string.h>
#include <stddef.h>   // offsetof

static int s_present = 0;
static int s_loaded  = 0;

// Where each tier actually came from, kept so a dump can say so rather than
// printing numbers with no provenance. "60" means nothing on its own; "60,
// from EEPROM" and "60, compiled default because nothing was saved" are
// different facts and only one of them means she is calibrated.
static uint8_t s_version     = 0;
static int     s_thr_loaded  = 0;
static int     s_spin_loaded = 0;
static int     s_turn_loaded = 0;

// Payload mirrors what is live in RAM. Kept as a struct so the length byte in
// the header and the bytes actually written can never disagree.
typedef struct {
  float   gyro_scale;      /* 4 */
  int16_t thresh_left;     /* 2 */
  int16_t thresh_right;    /* 2 */
  int16_t thresh_front;    /* 2 */
  int16_t reserved;        /* 2  -- explicit: without it the compiler pads to a
                                    4-byte multiple anyway, and the checksum
                                    would then cover bytes nothing ever sets. */
  /* Every turn x { entry, exit, lead_out, omega, alpha }.           160 */
  /* int16 keeps it compact; eeprom_write splits page boundaries for */
  /* us, so the block spanning three pages is not a problem.         */
  int16_t turn[TURN_COUNT][5];   /* 160 */
  /* APPENDED, not inserted. Every field above keeps its offset, so a v4   */
  /* block still loads its turns correctly and simply lacks these two.     */
  /* Inserting a field mid-struct would silently shift turn[] and load 16  */
  /* turns' worth of misaligned rubbish - which would pass the checksum,   */
  /* because the checksum covers the bytes, not their meaning.             */
  int16_t spin_omega;            /* 2 */
  int16_t spin_alpha;            /* 2 */
  /* APPENDED again. The spin gate below had to change with this: it read
   * "len >= sizeof(the struct)", which was the right test only while spin was
   * the last field. Left alone it would have demanded 182 bytes to load a
   * value that lives at byte 176, and every v5 block on every chip would have
   * quietly dropped its spin dynamics back to the compiled defaults. That is
   * the exact failure this file's header warns about, and it still nearly got
   * me -- so the gates are offsetof() now, which cannot go stale. */
  int16_t run_speed;             /* 2  mm/s along a straight   */
  int16_t run_accel;             /* 2  mm/s/s                  */
  int16_t run_diag_speed;        /* 2  mm/s along a diagonal   */
} ConfigV6;                      /* 182 */

static int s_run_loaded = 0;

static uint8_t sum8(const uint8_t *p, uint16_t n) {
  uint8_t s = 0;
  while (n--) s = (uint8_t)(s + *p++);
  return s;
}

void config_store_begin(void) {
  s_present = eeprom_present();
  s_loaded  = 0;
  if (!s_present) {
    report_write("CFG,no-eeprom (defaults, nothing persists)\r\n");
    config_store_report(0);
    return;
  }

  uint8_t hdr[6];
  if (!eeprom_read(CONFIG_ADDR, hdr, sizeof(hdr))) {
    report_write("CFG,read-fail\r\n");
    return;
  }
  if (hdr[0] != 'E' || hdr[1] != '4' || hdr[2] != 'C' || hdr[3] != '1') {
    report_write("CFG,blank (defaults)\r\n");     // never written, not a fault
    config_store_report(0);
    return;
  }

  const uint8_t ver = hdr[4];
  const uint8_t len = hdr[5];
  if (len == 0 || len > 200) { report_write("CFG,bad-length\r\n"); return; }

  uint8_t buf[200 + 1];                            // payload + checksum
  if (!eeprom_read((uint16_t)(CONFIG_ADDR + 6), buf, (uint16_t)(len + 1))) {
    report_write("CFG,read-fail\r\n");
    return;
  }
  // checksum covers version, length and payload
  uint8_t ck = (uint8_t)(hdr[4] + hdr[5] + sum8(buf, len));
  if (ck != buf[len]) { report_printf("CFG,checksum-fail v%u\r\n", ver); return; }

  // Load what this build understands; a shorter (older) payload leaves the
  // remaining fields at their defaults, a longer (newer) one is truncated.
  ConfigV6 c;
  memset(&c, 0, sizeof(c));
  memcpy(&c, buf, len < sizeof(c) ? len : sizeof(c));

  if (len >= sizeof(float)) {
    // Refuse a value that cannot be a real calibration - a corrupt float must
    // not be allowed to silently wreck every turn the mouse makes.
    if (c.gyro_scale > 0.80f && c.gyro_scale < 1.20f) {
      GYRO_SCALE = c.gyro_scale;
    } else {
      report_printf("CFG,gyro_scale out of range, ignored\r\n");
    }
  }

  // Thresholds only if the block is actually long enough to contain them.
  // Testing the LENGTH rather than the version is what lets a v1 block load
  // cleanly here instead of reading four bytes of somebody else's data.
  int thr_loaded = 0;
  if (len >= 10) {
    // 0 is not a threshold, it is an uncalibrated field; a negative one is
    // corruption. Either way the compiled default is the safer answer.
    if (c.thresh_left  > 0 && c.thresh_left  < 4096 &&
        c.thresh_right > 0 && c.thresh_right < 4096 &&
        c.thresh_front > 0 && c.thresh_front < 8192) {
      WALL_THRESH_LEFT  = c.thresh_left;
      WALL_THRESH_RIGHT = c.thresh_right;
      WALL_THRESH_FRONT = c.thresh_front;
      thr_loaded = 1;
    } else {
      report_write("CFG,thresholds out of range, ignored\r\n");
    }
  }

  // Turns, only if the block is long enough to hold them.
  int turns_loaded = 0;
  // Gated on the bytes the TURNS need, NOT on sizeof the whole struct - a v5
  // build must still load a v4 block's turns. Getting this wrong is how
  // appending a field silently discards everything that came before it.
  if (len >= (int)offsetof(ConfigV6, spin_omega)) {
    int sane = 1;
    for (int i = 0; i < TURN_COUNT; i++) {
      if (c.turn[i][0] < 0   || c.turn[i][0] > 500)   sane = 0;   // entry
      if (c.turn[i][1] < -200|| c.turn[i][1] > 500)   sane = 0;   // exit (may be negative)
      if (c.turn[i][2] < 0   || c.turn[i][2] > 500)   sane = 0;   // lead_out
      if (c.turn[i][3] < 1   || c.turn[i][3] > 2000)  sane = 0;   // omega
      if (c.turn[i][4] < 1   || c.turn[i][4] > 32000) sane = 0;   // alpha
    }
    if (sane) {
      // The ANGLE is deliberately not persisted. A saved -90 that came back as
      // +90 through a corrupt byte would turn her the wrong way at speed, and
      // it is not a thing anyone tunes - it is what the turn IS.
      for (int i = 0; i < TURN_COUNT; i++) {
        turn_params[i].entry_offset = c.turn[i][0];
        turn_params[i].exit_offset  = c.turn[i][1];
        turn_params[i].lead_out     = c.turn[i][2];
        turn_params[i].omega        = (float)c.turn[i][3];
        turn_params[i].alpha        = (float)c.turn[i][4];
      }
      turns_loaded = 1;
    } else {
      report_write("CFG,turn params out of range, ignored\r\n");
    }
  }

  // Spin dynamics, appended after the turns.
  int spin_loaded = 0;
  if (len >= (int)offsetof(ConfigV6, run_speed)) {
    if (c.spin_omega > 0 && c.spin_omega < 2000 &&
        c.spin_alpha > 0 && c.spin_alpha < 32000) {
      OMEGA_SPIN_TURN = (float)c.spin_omega;
      ALPHA_SPIN_TURN = (float)c.spin_alpha;
      spin_loaded = 1;
    } else {
      report_write("CFG,spin params out of range, ignored\r\n");
    }
  }

  // Fast-run speeds, appended after the spin dynamics.
  int run_loaded = 0;
  if (len >= (int)sizeof(ConfigV6)) {
    if (c.run_speed  >= 100 && c.run_speed  <= 3000 &&
        c.run_accel  >= 100 && c.run_accel  <= 32000 &&
        c.run_diag_speed >= 100 && c.run_diag_speed <= 3000) {
      RUN_SPEED        = (float)c.run_speed;
      RUN_ACCELERATION = (float)c.run_accel;
      RUN_DIAG_SPEED   = (float)c.run_diag_speed;
      run_loaded = 1;
    } else {
      report_write("CFG,run speeds out of range, ignored\r\n");
    }
  }

  s_run_loaded  = run_loaded;
  s_loaded      = 1;
  s_version     = ver;
  s_thr_loaded  = thr_loaded;
  s_spin_loaded = spin_loaded;
  s_turn_loaded = turns_loaded;
  config_store_report(0);
}

// The live configuration, as she actually holds it.
//
// full = 0 is the boot summary. full = 1 adds a line per turn, which is the
// only way to see the twelve rows nothing drives yet - they are invisible
// otherwise, and a slot you cannot inspect is a slot you cannot trust.
void config_store_report(int full) {
  int w = (int)(GYRO_SCALE * 1000.0f);
  if (s_loaded) {
    report_printf("CFG,loaded v%u gyro_scale=%d.%03d\r\n", s_version, w / 1000, w % 1000);
  } else {
    report_printf("CFG,defaults gyro_scale=%d.%03d\r\n", w / 1000, w % 1000);
  }
  report_printf("CFG,thr l=%d r=%d f=%d %s\r\n",
                WALL_THRESH_LEFT, WALL_THRESH_RIGHT, WALL_THRESH_FRONT,
                s_thr_loaded ? "(eeprom)" : "(defaults)");
  report_printf("CFG,spin w=%d al=%d %s\r\n",
                (int)OMEGA_SPIN_TURN, (int)ALPHA_SPIN_TURN,
                s_spin_loaded ? "(eeprom)" : "(defaults)");
  report_printf("CFG,run v=%d a=%d diag=%d %s\r\n",
                (int)RUN_SPEED, (int)RUN_ACCELERATION, (int)RUN_DIAG_SPEED,
                s_run_loaded ? "(eeprom)" : "(defaults)");
  report_printf("CFG,turn in=%d out=%d w=%d al=%d %s\r\n",
                turn_params[1].entry_offset, turn_params[1].lead_out,
                (int)turn_params[1].omega, (int)turn_params[1].alpha,
                s_turn_loaded ? "(eeprom)" : "(defaults)");

  if (full) {
    report_printf("CFG,dump v%u present=%d loaded=%d turns=%d\r\n",
                  s_version, s_present, s_loaded, TURN_COUNT);
    for (int i = 0; i < TURN_COUNT; i++) {
      const TurnParameters &p = turn_params[i];
      report_printf("TRN,%d,%s,v=%d,in=%d,ex=%d,out=%d,a=%d,w=%d,al=%d\r\n",
                    i, turn_names[i], p.speed, p.entry_offset, p.exit_offset,
                    p.lead_out, (int)p.angle, (int)p.omega, (int)p.alpha);
    }
    report_write("CFG,dump end\r\n");
  }
}

int config_store_save(void) {
  if (!s_present) return 0;

  ConfigV6 c;
  memset(&c, 0, sizeof(c));
  c.gyro_scale   = GYRO_SCALE;
  c.thresh_left  = (int16_t)WALL_THRESH_LEFT;
  c.thresh_right = (int16_t)WALL_THRESH_RIGHT;
  c.thresh_front = (int16_t)WALL_THRESH_FRONT;
  c.reserved     = 0;
  for (int i = 0; i < TURN_COUNT; i++) {
    c.turn[i][0] = (int16_t)turn_params[i].entry_offset;
    c.turn[i][1] = (int16_t)turn_params[i].exit_offset;
    c.turn[i][2] = (int16_t)turn_params[i].lead_out;
    c.turn[i][3] = (int16_t)turn_params[i].omega;
    c.turn[i][4] = (int16_t)turn_params[i].alpha;
  }

  c.spin_omega = (int16_t)OMEGA_SPIN_TURN;
  c.spin_alpha = (int16_t)ALPHA_SPIN_TURN;

  c.run_speed      = (int16_t)RUN_SPEED;
  c.run_accel      = (int16_t)RUN_ACCELERATION;
  c.run_diag_speed = (int16_t)RUN_DIAG_SPEED;

  uint8_t blk[6 + sizeof(ConfigV6) + 1];
  blk[0] = 'E'; blk[1] = '4'; blk[2] = 'C'; blk[3] = '1';
  blk[4] = (uint8_t)CONFIG_VERSION;
  blk[5] = (uint8_t)sizeof(ConfigV6);
  memcpy(&blk[6], &c, sizeof(c));
  blk[6 + sizeof(c)] = (uint8_t)(blk[4] + blk[5] + sum8(&blk[6], sizeof(c)));

  if (!eeprom_write(CONFIG_ADDR, blk, (uint16_t)sizeof(blk))) return 0;

  // Read back rather than trust the write: a chip at the wrong address or a
  // write-protected part will ACK and quietly do nothing.
  uint8_t rb[sizeof(blk)];
  if (!eeprom_read(CONFIG_ADDR, rb, (uint16_t)sizeof(rb))) return 0;
  if (memcmp(blk, rb, sizeof(blk)) != 0) { report_write("CFG,verify-fail\r\n"); return 0; }

  s_loaded = 1;
  return 1;
}

int config_store_present(void) { return s_present; }
int config_store_loaded(void)  { return s_loaded; }

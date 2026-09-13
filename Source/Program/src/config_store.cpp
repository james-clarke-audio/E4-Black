/*
 * config_store.cpp  --  see config_store.h
 */
#include "config_store.h"
#include "config.h"       // GYRO_SCALE, WALL_THRESH_* (runtime)
#include "mouse_config.h"  // turn_params
#include "eeprom.h"
#include "report.h"
#include <string.h>

static int s_present = 0;
static int s_loaded  = 0;

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
  /* Four turns x { entry, exit, lead_out, omega, alpha }.            40 */
  /* int16 because the whole block must fit ONE 24LC256 page: a page  */
  /* write that crosses a boundary WRAPS rather than continuing, so a */
  /* 65th byte would silently overwrite byte 0 of the same page.      */
  /* 6 header + 52 payload + 1 checksum = 59, inside the 64 at 512.   */
  int16_t turn[4][5];      /* 40 */
} ConfigV3;                /* 52 */

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
    return;
  }

  uint8_t hdr[6];
  if (!eeprom_read(CONFIG_ADDR, hdr, sizeof(hdr))) {
    report_write("CFG,read-fail\r\n");
    return;
  }
  if (hdr[0] != 'E' || hdr[1] != '4' || hdr[2] != 'C' || hdr[3] != '1') {
    report_write("CFG,blank (defaults)\r\n");     // never written, not a fault
    return;
  }

  const uint8_t ver = hdr[4];
  const uint8_t len = hdr[5];
  if (len == 0 || len > 64) { report_write("CFG,bad-length\r\n"); return; }

  uint8_t buf[64 + 1];                             // payload + checksum
  if (!eeprom_read((uint16_t)(CONFIG_ADDR + 6), buf, (uint16_t)(len + 1))) {
    report_write("CFG,read-fail\r\n");
    return;
  }
  // checksum covers version, length and payload
  uint8_t ck = (uint8_t)(hdr[4] + hdr[5] + sum8(buf, len));
  if (ck != buf[len]) { report_printf("CFG,checksum-fail v%u\r\n", ver); return; }

  // Load what this build understands; a shorter (older) payload leaves the
  // remaining fields at their defaults, a longer (newer) one is truncated.
  ConfigV3 c;
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
  if (len >= 52) {
    int sane = 1;
    for (int i = 0; i < 4; i++) {
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
      for (int i = 0; i < 4; i++) {
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

  s_loaded = 1;
  int w = (int)(GYRO_SCALE * 1000.0f);
  report_printf("CFG,loaded v%u gyro_scale=%d.%03d\r\n", ver, w / 1000, w % 1000);
  report_printf("CFG,thr l=%d r=%d f=%d %s\r\n",
                WALL_THRESH_LEFT, WALL_THRESH_RIGHT, WALL_THRESH_FRONT,
                thr_loaded ? "(eeprom)" : "(defaults)");
  report_printf("CFG,turn in=%d out=%d w=%d al=%d %s\r\n",
                turn_params[1].entry_offset, turn_params[1].lead_out,
                (int)turn_params[1].omega, (int)turn_params[1].alpha,
                turns_loaded ? "(eeprom)" : "(defaults)");
}

int config_store_save(void) {
  if (!s_present) return 0;

  ConfigV3 c;
  memset(&c, 0, sizeof(c));
  c.gyro_scale   = GYRO_SCALE;
  c.thresh_left  = (int16_t)WALL_THRESH_LEFT;
  c.thresh_right = (int16_t)WALL_THRESH_RIGHT;
  c.thresh_front = (int16_t)WALL_THRESH_FRONT;
  c.reserved     = 0;
  for (int i = 0; i < 4; i++) {
    c.turn[i][0] = (int16_t)turn_params[i].entry_offset;
    c.turn[i][1] = (int16_t)turn_params[i].exit_offset;
    c.turn[i][2] = (int16_t)turn_params[i].lead_out;
    c.turn[i][3] = (int16_t)turn_params[i].omega;
    c.turn[i][4] = (int16_t)turn_params[i].alpha;
  }

  uint8_t blk[6 + sizeof(ConfigV3) + 1];
  blk[0] = 'E'; blk[1] = '4'; blk[2] = 'C'; blk[3] = '1';
  blk[4] = (uint8_t)CONFIG_VERSION;
  blk[5] = (uint8_t)sizeof(ConfigV3);
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

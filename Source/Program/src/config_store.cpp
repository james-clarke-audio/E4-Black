/*
 * config_store.cpp  --  see config_store.h
 */
#include "config_store.h"
#include "config.h"     // GYRO_SCALE (now a runtime variable)
#include "eeprom.h"
#include "report.h"
#include <string.h>

static int s_present = 0;
static int s_loaded  = 0;

// Payload mirrors what is live in RAM. Kept as a struct so the length byte in
// the header and the bytes actually written can never disagree.
typedef struct {
  float gyro_scale;
} ConfigV1;

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
  ConfigV1 c;
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

  s_loaded = 1;
  int w = (int)(GYRO_SCALE * 1000.0f);
  report_printf("CFG,loaded v%u gyro_scale=%d.%03d\r\n", ver, w / 1000, w % 1000);
}

int config_store_save(void) {
  if (!s_present) return 0;

  ConfigV1 c;
  memset(&c, 0, sizeof(c));
  c.gyro_scale = GYRO_SCALE;

  uint8_t blk[6 + sizeof(ConfigV1) + 1];
  blk[0] = 'E'; blk[1] = '4'; blk[2] = 'C'; blk[3] = '1';
  blk[4] = (uint8_t)CONFIG_VERSION;
  blk[5] = (uint8_t)sizeof(ConfigV1);
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

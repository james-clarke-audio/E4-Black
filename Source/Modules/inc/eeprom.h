/*
 * eeprom.h  --  24LC256 I2C EEPROM on I2C1 (shared bus with the OLED).
 *   32 KB, 7-bit address 0x50 (A0/A1/A2/WP all grounded), 64-byte write pages,
 *   16-bit word addressing. Used to persist the discovered maze across power-off.
 */
#ifndef EEPROM_H
#define EEPROM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define EEPROM_SIZE 32768u   /* 24LC256 = 256 Kbit = 32 KB */

/* Return 1 on success, 0 on failure. */
int eeprom_read (uint16_t addr, uint8_t *buf, uint16_t len);
int eeprom_write(uint16_t addr, const uint8_t *buf, uint16_t len);  /* page-aware */
int eeprom_present(void);   /* 1 if the chip ACKs its address */

#ifdef __cplusplus
}
#endif
#endif /* EEPROM_H */

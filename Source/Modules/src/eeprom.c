#include "eeprom.h"
#include "i2c.h"            /* hi2c1 */
#include "stm32f4xx_hal.h"

#define EE_ADDR8  (0x50u << 1)   /* HAL uses the 8-bit address: 0xA0 */
#define EE_PAGE   64u            /* 24LC256 write page size */

static int ee_wait_ready(void) {
  /* Poll ACK until the internal write cycle finishes (<= 5 ms typical). */
  for (int i = 0; i < 50; i++) {
    if (HAL_I2C_IsDeviceReady(&hi2c1, EE_ADDR8, 1, 2) == HAL_OK) return 1;
    HAL_Delay(1);
  }
  return 0;
}

int eeprom_present(void) {
  return HAL_I2C_IsDeviceReady(&hi2c1, EE_ADDR8, 2, 20) == HAL_OK;
}

int eeprom_read(uint16_t addr, uint8_t *buf, uint16_t len) {
  if ((uint32_t)addr + len > EEPROM_SIZE) return 0;
  return HAL_I2C_Mem_Read(&hi2c1, EE_ADDR8, addr, I2C_MEMADD_SIZE_16BIT,
                          buf, len, 500) == HAL_OK;
}

int eeprom_write(uint16_t addr, const uint8_t *buf, uint16_t len) {
  if ((uint32_t)addr + len > EEPROM_SIZE) return 0;
  while (len) {
    uint16_t page_off = (uint16_t)(addr % EE_PAGE);
    uint16_t chunk    = (uint16_t)(EE_PAGE - page_off);   /* room left in this page */
    if (chunk > len) chunk = len;
    if (HAL_I2C_Mem_Write(&hi2c1, EE_ADDR8, addr, I2C_MEMADD_SIZE_16BIT,
                          (uint8_t *)buf, chunk, 500) != HAL_OK) return 0;
    if (!ee_wait_ready()) return 0;      /* wait out the write cycle */
    addr += chunk; buf += chunk; len -= chunk;
  }
  return 1;
}

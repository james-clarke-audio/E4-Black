/*
 * config_store.h  --  small, versioned settings block in the 24LC256 EEPROM.
 *
 * Holds the values that are properties of THIS mouse on THIS surface rather
 * than of the code: things you calibrate rather than compile. Kept well clear
 * of the maze store (which owns bytes 0..262) and page-aligned.
 *
 * EEPROM layout @ CONFIG_ADDR:
 *   [0..3]  magic 'E','4','C','1'
 *   [4]     version
 *   [5]     payload length, bytes
 *   [6..]   payload
 *   [6+len] checksum = 8-bit sum of [4 .. 6+len-1]
 *
 * v1 payload: float gyro_scale.
 * Adding a field means bumping the version and widening the payload; an older
 * block still loads, the new field just takes its default. Wall thresholds are
 * the next tenants.
 *
 * No EEPROM fitted is NOT an error: everything runs from the compiled defaults
 * and save reports failure, so the mouse is fully usable on a board without a
 * chip - it simply forgets between power cycles.
 */
#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdint.h>

#define CONFIG_ADDR      512u   /* clear of the maze store, 64-byte page aligned */
#define CONFIG_VERSION   1u

#ifdef __cplusplus
extern "C" {
#endif

/* Load from EEPROM if a valid block is there; otherwise leave the compiled
 * defaults in place. Call once at boot, after the I2C bus is up. */
void config_store_begin(void);

/* Write the current live values. 0 if there is no EEPROM or the write failed. */
int  config_store_save(void);

/* 1 if a chip answered at boot. */
int  config_store_present(void);

/* 1 if a valid, checksum-clean block was found and loaded at boot. */
int  config_store_loaded(void);

#ifdef __cplusplus
}
#endif
#endif /* CONFIG_STORE_H */

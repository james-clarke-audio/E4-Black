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
 * v1 payload: float gyro_scale.                                    (4 bytes)
 * v2 payload: + int16 thresh_left, thresh_right, thresh_front,
 *               int16 reserved (explicit, so sizeof has no implicit
 *               padding the checksum would cover but nothing sets)  (12 bytes)
 * v3 payload: + int16 turn[4][5] = entry, exit, lead_out, omega,
 *               alpha for each of the four turns                    (52 bytes)
 *
 * v4 payload: turn[] widened to all 16 turn types                  (172 bytes)
 * v5 payload: + int16 spin_omega, spin_alpha                       (176 bytes)
 * v6 payload: + int16 run_speed, run_accel, run_diag_speed         (182 bytes)
 *
 * ALWAYS APPEND, NEVER INSERT. The load is length-driven and reads by struct
 * offset, so a field added in the middle shifts everything after it and an
 * older block decodes as rubbish - rubbish that PASSES THE CHECKSUM, because
 * the checksum covers the bytes and not what they mean.
 *
 * CORRECTION (I had this wrong in v3): there is NO one-page ceiling here.
 * eeprom_write already splits its writes at page boundaries, so a block may
 * span as many 64-byte pages as it likes. The real limits are the length byte
 * (255) and the read buffer below. The v3 comment claiming a 57-byte cap was
 * describing a constraint the driver had already solved.
 *
 * Adding a field means bumping the version and widening the payload. The load
 * path is length-driven, not version-driven: a short (v1) block still loads its
 * gyro scale and the thresholds simply keep their compiled defaults, and a block
 * written by a FUTURE build is truncated to what this one understands. So an
 * older mouse and a newer one can share a chip without either corrupting it.
 *
 * No EEPROM fitted is NOT an error: everything runs from the compiled defaults
 * and save reports failure, so the mouse is fully usable on a board without a
 * chip - it simply forgets between power cycles.
 */
#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdint.h>

#define CONFIG_ADDR      512u   /* clear of the maze store, 64-byte page aligned */
#define CONFIG_VERSION   6u

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

/* Report the live configuration. full = 0 is the four-line boot summary;
 * full = 1 adds one line per turn. Driven from the app by "CFG?". */
void config_store_report(int full);

#ifdef __cplusplus
}
#endif
#endif /* CONFIG_STORE_H */

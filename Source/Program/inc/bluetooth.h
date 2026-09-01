/*
 * bluetooth.h
 *
 * Utilities for talking to the UART Bluetooth module (HM-10 / HM-18 class)
 * on USART1. Kept module-agnostic and reusable: runtime re-baud, a generic
 * AT command/response helper, and a baud-rate sweep that finds the rate the
 * module is actually listening on. Handy now for configuration and later for
 * driving / re-configuring the live debug link.
 *
 * NOTE: HM-10/HM-18 modules only accept AT commands while NOT connected to a
 * BLE central. Disconnect LightBlue (or any phone) before running a sweep.
 */
#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <stdint.h>

// Reconfigure USART1 to `baud` (8N1) at runtime. Returns true on success.
// Uses DeInit -> set baud -> Init, which is the reliable way to re-baud a
// running HAL UART; all other framing fields are left as CubeMX set them.
bool bt_set_baud(uint32_t baud);

// Send an AT command at `baud` and capture the module's reply.
//   cmd       : command text, e.g. "AT" or "AT+NAME?"  (no CR/LF -- see below)
//   with_crlf : append "\r\n" (many clones need it) vs send bare (genuine HM-10)
//   resp      : caller buffer, null-terminated on return
//   resp_sz   : size of resp in bytes
// Returns the number of reply bytes captured (0 = silence / wrong baud).
int bt_send_at(uint32_t baud, const char *cmd, bool with_crlf,
               char *resp, int resp_sz);

// Sweep the standard HM-10/HM-18 baud rates, sending "AT" at each and looking
// for an "OK" reply. Shows progress on the OLED. Leaves USART1 at the found
// baud on success. Returns the baud that answered, or 0 if none did.
uint32_t bt_baud_sweep(void);

// Ensure the module + this UART end up at `target` baud, wherever the module is
// now (idempotent, baud-agnostic; persists in module NVM). Run once by button
// with no phone connected. Returns `target` on success, 0 if the module was
// silent. Used to move the debug link from 230400 down to 57600.
uint32_t bt_ensure_baud(uint32_t target);

#endif // BLUETOOTH_H

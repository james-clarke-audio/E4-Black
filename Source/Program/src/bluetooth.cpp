/*
 * bluetooth.cpp
 *
 * Implementation of the HM-10 / HM-18 UART Bluetooth helpers declared in
 * bluetooth.h. See that header for usage notes (chiefly: AT commands only
 * work while the module is NOT connected over BLE).
 */
#include "bluetooth.h"
#include "usart.h"
#include "ssd1306.h"
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef huart1;   // defined by CubeMX

// Standard HM-10 / HM-18 AT+BAUD map, most-likely-first.
// (AT+BAUD index: 0=9600 1=19200 2=38400 3=57600 4=115200 5=4800 6=2400
//                 7=1200 8=230400)
static const uint32_t k_bauds[] = {
    9600, 115200, 57600, 38400, 19200, 4800, 2400, 1200, 230400
};
static const int k_nbauds = (int)(sizeof(k_bauds) / sizeof(k_bauds[0]));

bool bt_set_baud(uint32_t baud) {
  if (HAL_UART_DeInit(&huart1) != HAL_OK) {
    return false;
  }
  huart1.Init.BaudRate = baud;          // framing fields stay as CubeMX set (8N1)
  return HAL_UART_Init(&huart1) == HAL_OK;
}

// Drain and discard any bytes sitting in the RX path, then clear error flags
// so a prior wrong-baud burst can't poison the next exchange.
static void bt_flush_rx(void) {
  uint8_t junk;
  while (HAL_UART_Receive(&huart1, &junk, 1, 5) == HAL_OK) {
    /* discard */
  }
  __HAL_UART_CLEAR_OREFLAG(&huart1);
}

int bt_send_at(uint32_t baud, const char *cmd, bool with_crlf,
               char *resp, int resp_sz) {
  if (resp_sz > 0) {
    resp[0] = '\0';
  }
  if (!bt_set_baud(baud)) {
    return 0;
  }

  bt_flush_rx();

  // send the command
  HAL_UART_Transmit(&huart1, (uint8_t *)cmd, (uint16_t)strlen(cmd), 100);
  if (with_crlf) {
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);
  }

  // collect the reply byte-by-byte until a gap longer than the per-byte
  // timeout. At a wrong baud we either get nothing or noise/framing errors
  // (HAL returns non-OK) and bail immediately; at the right baud "OK" (and
  // often "OK\r\n") arrives cleanly.
  int n = 0;
  uint8_t ch;
  while (n < resp_sz - 1) {
    if (HAL_UART_Receive(&huart1, &ch, 1, 250) == HAL_OK) {
      resp[n++] = (char)ch;
    } else {
      break;
    }
  }
  resp[n] = '\0';
  return n;
}

uint32_t bt_baud_sweep(void) {
  char resp[24];
  char line[24];

  for (int i = 0; i < k_nbauds; i++) {
    uint32_t baud = k_bauds[i];

    SSD1306_Clear();
    SSD1306_GotoXY(0, 0);
    SSD1306_Puts("BT baud sweep", &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_GotoXY(0, 16);
    sprintf(line, "try: %lu", (unsigned long)baud);
    SSD1306_Puts(line, &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_UpdateScreen();

    // Genuine HM-10 wants a bare "AT"; many clones want "AT\r\n". Try both.
    int n = bt_send_at(baud, "AT", false, resp, sizeof(resp));
    if (n == 0 || strstr(resp, "OK") == NULL) {
      n = bt_send_at(baud, "AT", true, resp, sizeof(resp));
    }

    if (n > 0 && strstr(resp, "OK") != NULL) {
      // Found it -- UART is already left at this baud by bt_send_at().
      SSD1306_Clear();
      SSD1306_GotoXY(0, 0);
      SSD1306_Puts("BT FOUND", &Font_7x10, SSD1306_COLOR_WHITE);
      SSD1306_GotoXY(0, 16);
      sprintf(line, "baud: %lu", (unsigned long)baud);
      SSD1306_Puts(line, &Font_7x10, SSD1306_COLOR_WHITE);
      SSD1306_GotoXY(0, 32);
      SSD1306_Puts("reply:", &Font_7x10, SSD1306_COLOR_WHITE);
      SSD1306_GotoXY(0, 48);
      SSD1306_Puts(resp, &Font_7x10, SSD1306_COLOR_WHITE);
      SSD1306_UpdateScreen();
      return baud;
    }
  }

  SSD1306_Clear();
  SSD1306_GotoXY(0, 0);
  SSD1306_Puts("BT sweep: no OK", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_GotoXY(0, 16);
  SSD1306_Puts("module silent", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_UpdateScreen();
  return 0;
}

// Map a baud rate to its HM-10/HM-18 AT+BAUD index (see k_bauds note above).
static int bt_at_index(uint32_t baud) {
  // This DSD Tech HM-18 uses a LINEAR AT+BAUD map (verified empirically 31 Aug
  // 2026: it reported index 8 at 230400 and index 3 at 9600), NOT the genuine
  // Huamao map. So on THIS unit: 57600 = index 6, 230400 = index 8.
  switch (baud) {
    case 1200:  return 0;  case 2400:   return 1;  case 4800:   return 2;
    case 9600:  return 3;  case 19200:  return 4;  case 38400:  return 5;
    case 57600: return 6;  case 115200: return 7;  case 230400: return 8;
    default:    return -1;
  }
}

// Make sure the module (and this UART) end up at `target` baud, wherever the
// module is now. Idempotent and baud-agnostic:
//   1. If the module already answers AT at `target`, done.
//   2. Otherwise sweep to find its current baud, send "AT+BAUD<idx>" to switch
//      it, then settle this UART at `target` and confirm.
// The module stores the new baud in NVM, so this survives power cycles - run it
// ONCE (by button, with no phone connected so the module is in command mode).
// Returns `target` on success, 0 if the module never answered.
uint32_t bt_ensure_baud(uint32_t target) {
  char resp[48];
  char line[24];
  int idx = bt_at_index(target);
  if (idx < 0) return 0;

  // 1) already at target?
  if ((bt_send_at(target, "AT", false, resp, sizeof(resp)) > 0 && strstr(resp, "OK")) ||
      (bt_send_at(target, "AT", true,  resp, sizeof(resp)) > 0 && strstr(resp, "OK"))) {
    bt_set_baud(target);
    SSD1306_Clear();
    SSD1306_GotoXY(0, 0);  SSD1306_Puts("BT already", &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_GotoXY(0, 16); SSD1306_Puts("at 57600", &Font_7x10, SSD1306_COLOR_WHITE);
    SSD1306_UpdateScreen();
    return target;
  }

  // 2) find the module's current baud (drives the OLED itself)
  uint32_t cur = bt_baud_sweep();
  if (cur == 0) return 0;               // module silent - leave as-is
  HAL_Delay(600);                        // let the "FOUND" screen be read

  // 2a) read + show the module's reported baud BEFORE we change it
  resp[0] = 0;
  if (bt_send_at(cur, "AT+BAUD?", false, resp, sizeof(resp)) <= 0)
    bt_send_at(cur, "AT+BAUD?", true, resp, sizeof(resp));
  SSD1306_Clear();
  SSD1306_GotoXY(0, 0);  SSD1306_Puts("BT+BAUD? was:", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_GotoXY(0, 16); SSD1306_Puts(resp[0] ? resp : "(no reply)", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_GotoXY(0, 32); sprintf(line, "at %lu", (unsigned long)cur);
  SSD1306_Puts(line, &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_UpdateScreen();
  HAL_Delay(1800);

  if (cur == target) { bt_set_baud(target); return target; }

  // 3) switch the module: "AT+BAUD<idx>" (reply arrives at the OLD baud).
  //    Try bare, then CRLF; accept "OK"/"Set" as success. Settle after.
  char cmd[16];
  sprintf(cmd, "AT+BAUD%d", idx);
  char sw[48]; sw[0] = 0;
  int n = bt_send_at(cur, cmd, false, sw, sizeof(sw));
  if (n <= 0 || !(strstr(sw, "Set") || strstr(sw, "OK"))) {
    bt_send_at(cur, cmd, true, sw, sizeof(sw));   // clone variant: try with CRLF
  }
  HAL_Delay(120);
  // Huamao only *applies* a new baud on a soft reset - AT+BAUD alone just stores
  // it, and a hard power-cycle reverts the uncommitted change. Reset to apply.
  char rst[48]; rst[0] = 0;
  if (bt_send_at(cur, "AT+RESET", false, rst, sizeof(rst)) <= 0 ||
      !(strstr(rst, "RESET") || strstr(rst, "OK"))) {
    bt_send_at(cur, "AT+RESET", true, rst, sizeof(rst));
  }
  HAL_Delay(1200);                       // wait for the module to reboot at 57600

  // 4) settle this UART at target and confirm (a few tries with delays)
  bt_set_baud(target);
  HAL_Delay(80);
  int ok = 0;
  for (int t = 0; t < 4 && !ok; t++) {
    if ((bt_send_at(target, "AT", false, resp, sizeof(resp)) > 0 && strstr(resp, "OK")) ||
        (bt_send_at(target, "AT", true,  resp, sizeof(resp)) > 0 && strstr(resp, "OK"))) ok = 1;
    else HAL_Delay(60);
  }
  bt_set_baud(target);                   // leave UART at target regardless

  SSD1306_Clear();
  SSD1306_GotoXY(0, 0);
  SSD1306_Puts(ok ? "BT -> 57600 OK" : "BT switch FAIL", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_GotoXY(0, 16);
  SSD1306_Puts("reply:", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_GotoXY(0, 32);
  SSD1306_Puts(sw[0] ? sw : "(no reply)", &Font_7x10, SSD1306_COLOR_WHITE);
  SSD1306_UpdateScreen();
  return ok ? target : 0;
}

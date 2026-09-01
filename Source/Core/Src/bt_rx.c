#include "bt_rx.h"
#include "usart.h"   /* huart1 + HAL */

#define BTRX_SZ 256u                 /* power of two */
static volatile uint8_t  s_buf[BTRX_SZ];
static volatile uint16_t s_head = 0, s_tail = 0;

void bt_rx_init(void) {
  __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
}
void bt_rx_push(uint8_t b) {         /* ISR context */
  uint16_t n = (uint16_t)((s_head + 1u) & (BTRX_SZ - 1u));
  if (n != s_tail) { s_buf[s_head] = b; s_head = n; }   /* drop on full */
}
int bt_rx_pop(uint8_t *b) {
  if (s_tail == s_head) return 0;
  *b = s_buf[s_tail];
  s_tail = (uint16_t)((s_tail + 1u) & (BTRX_SZ - 1u));
  return 1;
}

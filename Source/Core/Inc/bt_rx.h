/*
 * bt_rx.h  --  interrupt-driven UART1 receive ring buffer.
 * The USART1 ISR pushes each byte the instant it arrives (no overrun); the
 * main loop pops bytes at its leisure. Needed because multi-byte commands
 * (menu lines, maze injection) arrive faster than the ~20 ms menu loop polls.
 */
#ifndef BT_RX_H
#define BT_RX_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void bt_rx_init(void);        // enable the USART1 RXNE interrupt
void bt_rx_push(uint8_t b);   // called from USART1_IRQHandler
int  bt_rx_pop(uint8_t *b);   // main loop: 1 if a byte was available
#ifdef __cplusplus
}
#endif
#endif /* BT_RX_H */

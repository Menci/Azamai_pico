#ifndef UART_H
#define UART_H

#include <stdint.h>

void io_uart_init(uint8_t tx_pin, uint8_t rx_pin);
void io_uart_run(uint8_t itf);

#endif

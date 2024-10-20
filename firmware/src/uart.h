#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <FreeRTOS.h>

void io_uart_init(UBaseType_t priority_u2t, UBaseType_t priority_t2u);

#endif

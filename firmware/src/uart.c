/*
 * Mai Pico UART Bridge
 * Menci <github.com/Menci>
 *
 * UART to USB CDC bridge
 */

#include "class/cdc/cdc_device.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "portmacro.h"
#include "tusb.h"

#include "board_defs.h"

#include <FreeRTOS.h>
#include <queue.h>
#include <stdint.h>
#include <task.h>

#define UART_BAUDRATE 9600
#define UART_DATABITS 8
#define UART_STOPBITS 1

#define BUFFER_SIZE 1024

QueueHandle_t u2t_queue, t2u_queue;

void u2t_read_isr()
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	while (uart_is_readable(UART_PORT)) {
		char ch = uart_getc(UART_PORT);
		xQueueSendFromISR(u2t_queue, &ch, &xHigherPriorityTaskWoken);
	}

	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void u2t_write_task()
{
	while (1) {
		char ch;
		if (xQueueReceive(u2t_queue, &ch, portMAX_DELAY) != pdTRUE) {
			continue;
		}
		do {
			tud_cdc_n_write_char(UART_ITF, ch);
		} while (xQueueReceive(u2t_queue, &ch, 0) == pdTRUE);

		tud_cdc_n_write_flush(UART_ITF);
	}
}

void t2u_read_task()
{
	const TickType_t xFrequency = pdMS_TO_TICKS(1);
	while (1) {
		char ch;
		while (tud_cdc_n_available(UART_ITF) && (ch = tud_cdc_n_read_char(UART_ITF)) >= 0) {
			xQueueSend(t2u_queue, &ch, xFrequency);
		}
		vTaskDelay(xFrequency);
	}
}

void t2u_write_task()
{
	const TickType_t xFrequency = pdMS_TO_TICKS(1);
	while (1) {
		while (uart_is_writable(UART_PORT)) {
			char ch;
			if (xQueueReceive(t2u_queue, &ch, portMAX_DELAY) != pdTRUE) {
				continue;
			}
			uart_putc_raw(UART_PORT, ch);
		}
		vTaskDelay(xFrequency);
	}
}

void io_uart_init(UBaseType_t priority_u2t, UBaseType_t priority_t2u)
{
	gpio_set_function(UART_TX, GPIO_FUNC_UART);
	gpio_set_function(UART_RX, GPIO_FUNC_UART);
	gpio_set_pulls(UART_TX, 1, 0);
	gpio_set_pulls(UART_RX, 1, 0);

	u2t_queue = xQueueCreate(BUFFER_SIZE, sizeof(char));
	t2u_queue = xQueueCreate(BUFFER_SIZE, sizeof(char));

	uart_init(UART_PORT, UART_BAUDRATE);
	uart_set_hw_flow(UART_PORT, false, false);
	uart_set_format(UART_PORT, UART_DATABITS, UART_STOPBITS, UART_PARITY_NONE);
	uart_set_fifo_enabled(UART_PORT, false);

	irq_set_exclusive_handler(UART_IRQ, &u2t_read_isr);
	irq_set_enabled(UART_IRQ, true);
	uart_set_irq_enables(UART_PORT, true, false);

	xTaskCreate(u2t_write_task, "u2t_write", configMINIMAL_STACK_SIZE, NULL, priority_u2t, NULL);

	xTaskCreate(t2u_read_task, "t2u_read", configMINIMAL_STACK_SIZE, NULL, priority_t2u, NULL);
	xTaskCreate(t2u_write_task, "t2u_write", configMINIMAL_STACK_SIZE, NULL, priority_t2u, NULL);
}

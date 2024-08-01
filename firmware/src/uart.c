/*
 * Mai Pico UART Bridge
 * Menci <github.com/Menci>
 *
 * UART to USB CDC bridge
 */

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "tusb.h"

#include "board_defs.h"

#define UART_BAUDRATE 9600
#define UART_DATABITS 8
#define UART_STOPBITS 1

#define UART_BUFFER_SIZE 1024

inline static uint32_t min(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

typedef struct
{
    uint8_t *begin, *end, *head;
    size_t capacity;
    size_t count;
} ring_buffer_t;

inline void ring_init(ring_buffer_t *buffer, void *data, size_t size)
{
    buffer->begin = data;
    buffer->end = data + size;
    buffer->head = data;
    buffer->capacity = size;
    buffer->count = 0;
}

inline void ring_clear(ring_buffer_t *buffer)
{
    buffer->head = buffer->begin;
    buffer->count = 0;
}

inline uint32_t ring_can_pop(ring_buffer_t *buffer)
{
    return buffer->count;
}

inline uint8_t ring_pop(ring_buffer_t *buffer)
{
    uint8_t data = *buffer->head;
    buffer->head++;

    if (buffer->head == buffer->end)
        buffer->head = buffer->begin;
    buffer->count--;

    return data;
}

inline uint32_t ring_can_push(ring_buffer_t *buffer)
{
    return buffer->capacity - buffer->count;
}

inline void ring_push(ring_buffer_t *buffer, uint8_t data)
{
    uint8_t *tail = buffer->head + buffer->count;
    if (tail >= buffer->end)
        tail -= buffer->capacity;
    *tail = data;
    buffer->count++;
}

ring_buffer_t buffer_u2t, buffer_t2u;

static void tick_tud_read(uint8_t itf)
{
    uint32_t len = min(tud_cdc_n_available(itf), ring_can_push(&buffer_t2u));
    if (len) {
        uint8_t data[len];
        uint32_t real_len = tud_cdc_n_read(itf, data, len);
        for (uint32_t i = 0; i < real_len; i++)
            ring_push(&buffer_t2u, data[i]);
    }
}

static void tick_tud_write(uint8_t itf)
{
    uint32_t len = min(tud_cdc_n_write_available(itf), ring_can_pop(&buffer_u2t));
    if (len) {
        uint8_t data[len];
        for (uint32_t i = 0; i < len; i++)
            data[i] = ring_pop(&buffer_u2t);
        tud_cdc_n_write(itf, data, len);
        tud_cdc_n_write_flush(itf);
    }
}

static void tick_uart_read()
{
    while (ring_can_push(&buffer_u2t)) {
        if (!uart_is_readable(UART_PORT))
            break;
        ring_push(&buffer_u2t, uart_getc(UART_PORT));
    }
}

static void tick_uart_write()
{
    while (ring_can_pop(&buffer_t2u)) {
        if (!uart_is_writable(UART_PORT))
            break;
        uart_putc_raw(UART_PORT, ring_pop(&buffer_t2u));
    }
}

void io_uart_init(uint8_t tx_pin, uint8_t rx_pin)
{
    gpio_set_function(rx_pin, GPIO_FUNC_UART);
    gpio_set_function(tx_pin, GPIO_FUNC_UART);

    static uint8_t buffer_u2t_data[UART_BUFFER_SIZE], buffer_t2u_data[UART_BUFFER_SIZE];
    ring_init(&buffer_u2t, buffer_u2t_data, UART_BUFFER_SIZE);
    ring_init(&buffer_t2u, buffer_t2u_data, UART_BUFFER_SIZE);
}

static void uart_check_connected(uint8_t itf, bool* just_connected, bool* just_disconnected)
{
    static bool was_connected;
    bool connected = tud_cdc_n_connected(itf);
    if (was_connected && !connected) {
        *just_connected = false;
        *just_disconnected = true;
    } else if (!was_connected && connected) {
        *just_connected = true;
        *just_disconnected = false;
    } else {
        *just_connected = false;
        *just_disconnected = false;
    }
    was_connected = connected;
}

void io_uart_run(uint8_t itf)
{
    bool just_connected, just_disconnected;
    uart_check_connected(itf, &just_connected, &just_disconnected);
    if (just_connected) {
        ring_clear(&buffer_u2t);
        ring_clear(&buffer_t2u);

        uart_init(UART_PORT, UART_BAUDRATE);
        uart_set_hw_flow(UART_PORT, false, false);
        uart_set_format(UART_PORT, UART_DATABITS, UART_STOPBITS, UART_PARITY_NONE);
        uart_set_fifo_enabled(UART_PORT, true);
    } else if (just_disconnected) {
        uart_deinit(UART_PORT);
    }
    tick_tud_read(itf);
    tick_uart_read();
    tick_tud_write(itf);
    tick_uart_write();

    if (tud_cdc_n_connected(0)) {
        static char buffer[1024];
        sprintf(buffer, "U2T buffer size: %u, T2U buffer size: %u\r\n", buffer_u2t.count, buffer_t2u.count);
        tud_cdc_n_write(0, buffer, strlen(buffer));
    }
}

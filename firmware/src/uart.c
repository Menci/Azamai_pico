/*
 * Mai Pico UART Bridge
 * Menci <github.com/Menci>
 *
 * UART to USB CDC bridge
 */

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "tusb.h"
#include <stdarg.h>

#include "board_defs.h"

#define UART_BAUDRATE 9600
#define UART_DATABITS 8
#define UART_STOPBITS 1

#define TOUCH_BUFFER_SIZE 16

typedef struct {
    bool left_brace; // "{"/"(" has been read
    char buffer[TOUCH_BUFFER_SIZE + 1];
    size_t i;
} touch_buffer_t;

static void cdprintf(const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    tud_cdc_n_write(0, buffer, strlen(buffer));
}

static bool touch_buffer_on_data(touch_buffer_t *buffer, uint8_t data)
{
    if (buffer->left_brace)
    {
        if (data == '}' || data == ')')
        {
            // Null-terminate buffer
            buffer->buffer[buffer->i] = 0;
            // Reset buffer
            buffer->i = 0;
            buffer->left_brace = false;
            return true;
        }

        if (buffer->i < TOUCH_BUFFER_SIZE)
        {
            buffer->buffer[buffer->i++] = data;
        }
    }
    else
    {
        if (data == '{' || data == '(')
        {
            buffer->left_brace = true;
        }
    }
    return false;
}

void io_uart_init(uint8_t tx_pin, uint8_t rx_pin)
{
    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    gpio_set_function(rx_pin, GPIO_FUNC_UART);
    gpio_set_pulls(tx_pin, 1, 0);
    gpio_set_pulls(rx_pin, 1, 0);

    uart_init(UART_PORT, UART_BAUDRATE);
    uart_set_hw_flow(UART_PORT, false, false);
    uart_set_format(UART_PORT, UART_DATABITS, UART_STOPBITS, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_PORT, true);
}

#define TOUCH_DATA_LENGTH 7
struct {
    char touch_data[TOUCH_DATA_LENGTH];
    bool has_touch_data;
    bool conditioning_mode;
} touch_state;

static void handle_u2t(uint8_t itf, char *buffer)
{
    size_t length = strlen(buffer);
    if (length == 4) // "LAr2"
    {
        cdprintf("handle_u2t(): pass-through 4-byte response: %s\r\n", buffer);

        char write_buffer[] = "(LAr2)";
        memcpy(write_buffer + 1, buffer, 4);
        // if (tud_cdc_n_connected(itf))
        {
            tud_cdc_n_write(itf, write_buffer, strlen(write_buffer));
            tud_cdc_n_write_flush(itf);
        }
    }
    else if (length == 7) // "......."
    {
        if (touch_state.conditioning_mode)
        {
            cdprintf("handle_u2t(): ignored touch data %s in conditioning mode\r\n", buffer);
            // ignore touch data in conditioning mode
        }
        else
        {
            if (!touch_state.has_touch_data ||
                memcmp(touch_state.touch_data, buffer, TOUCH_DATA_LENGTH) != 0)
            {
                memcpy(touch_state.touch_data, buffer, TOUCH_DATA_LENGTH);
                touch_state.has_touch_data = true;

                cdprintf("handle_u2t(): updating touch data: %s\r\n", buffer);
            }

            char write_buffer[] = "(.......)";
            memcpy(write_buffer + 1, buffer, TOUCH_DATA_LENGTH);
            // if (tud_cdc_n_connected(itf))
            {
                tud_cdc_n_write(itf, write_buffer, strlen(write_buffer));
                tud_cdc_n_write_flush(itf);
            }
        }
    }
}

static void handle_t2u(uint8_t itf, char *buffer)
{
    if (strcmp(buffer, "RSET") == 0)
    {
        touch_state.has_touch_data = false;
        touch_state.conditioning_mode = false;
    }
    else if (strcmp(buffer, "HALT") == 0)
    {
        touch_state.has_touch_data = false;
        touch_state.conditioning_mode = true;
    }
    else if (strcmp(buffer, "STAT") == 0)
    {
        touch_state.conditioning_mode = false;
    }
    else if (touch_state.conditioning_mode &&
             strlen(buffer) == 4 &&
             (buffer[0] == 'L' || buffer[0] == 'R') && (buffer[2] == 'r' || buffer[2] == 'k'))
    {
        // conditioning mode command
    }
    else
    {
        cdprintf("handle_t2u(): Unknown command: %s\r\n", buffer);
        return;
    }

    cdprintf("handle_t2u(): pass-through command: %s\r\n", buffer);

    // Passthrough the command
    uart_putc_raw(UART_PORT, '{');
    for (size_t i = 0; i < 4; i++)
    {
        uart_putc_raw(UART_PORT, buffer[i]);
    }
    uart_putc_raw(UART_PORT, '}');
}

void io_uart_run(uint8_t itf)
{
    static touch_buffer_t u2t, t2u;
    while (uart_is_readable(UART_PORT))
    {
        if (touch_buffer_on_data(&u2t, uart_getc(UART_PORT)))
        {
            handle_u2t(itf, u2t.buffer);
        }
    }

    static bool connected = false;
    if (tud_cdc_n_connected(itf) && !connected)
    {
        connected = true;
        cdprintf("io_uart_run(): connected to cdc\r\n");
    }
    else if (!tud_cdc_n_connected(itf) && connected)
    {
        connected = false;
        cdprintf("io_uart_run(): disconnected from cdc\r\n");
    }

    if (tud_cdc_n_available(itf))
    {
        while (true)
        {
            int32_t ch = tud_cdc_n_read_char(itf);
            if (ch < 0)
            {
                break;
            }
            cdprintf("io_uart_run(): from cdc: %c\r\n", ch);
            if (touch_buffer_on_data(&t2u, ch))
            {
                handle_t2u(itf, t2u.buffer);
            }
        }
    }
}

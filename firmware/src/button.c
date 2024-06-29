/*
 * Mai Controller Buttons
 * WHowe <github.com/whowechina>
 * 
 */

#include "button.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"

#include "config.h"
#include "board_defs.h"

static const uint8_t gpio_def[] = BUTTON_DEF;
static const bool button_pressed[] = BUTTON_PRESSED;
static uint8_t gpio_real[] = BUTTON_DEF;

#define BUTTON_NUM (sizeof(gpio_def))

static bool sw_val[BUTTON_NUM]; /* true if pressed */
static uint64_t sw_freeze_time[BUTTON_NUM];

void button_init()
{
    for (int i = 0; i < BUTTON_NUM; i++)
    {
        sw_val[i] = false;
        sw_freeze_time[i] = 0;
        uint8_t gpio = mai_cfg->alt.buttons[i];
        if (gpio > 29) {
            gpio = gpio_def[i];
        }
        gpio_real[i] = gpio;
        gpio_init(gpio);
        gpio_set_function(gpio, GPIO_FUNC_SIO);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_pull_up(gpio);
    }
}

bool button_is_stuck()
{
    return false;
}

uint8_t button_num()
{
    return BUTTON_NUM;
}

uint8_t button_real_gpio(int id)
{
    if (id >= BUTTON_NUM) {
        return 0xff;
    }
    return gpio_real[id];
}

uint8_t button_default_gpio(int id)
{
    if (id >= BUTTON_NUM) {
        return 0xff;
    }
    return gpio_def[id];
}

static uint16_t button_reading;

/* If a switch flips, it freezes for a while */
#define DEBOUNCE_FREEZE_TIME_US 3000
#define DELAY_TICKS 0
void button_update()
{
    uint64_t now = time_us_64();
    uint16_t buttons = 0;

    for (int i = BUTTON_NUM - 1; i >= 0; i--) {
        bool pressed_expected = button_pressed[i];
        bool value = gpio_get(gpio_real[i]);
        bool sw_pressed = (value && pressed_expected) || (!value && !pressed_expected);
        
        if (now >= sw_freeze_time[i]) {
            if (sw_pressed != sw_val[i]) {
                sw_val[i] = sw_pressed;
                sw_freeze_time[i] = now + DEBOUNCE_FREEZE_TIME_US;
            }
        }

        buttons <<= 1;
        if (sw_val[i]) {
            buttons |= 1;
        }
    }

    if (DELAY_TICKS != 0) {
        static uint16_t button_history[DELAY_TICKS];
        button_reading = button_history[0];
        memmove(&button_history[0], &button_history[1], sizeof(button_history[0]) * (DELAY_TICKS - 1));
        button_history[DELAY_TICKS - 1] = buttons;
    } else {
        button_reading = buttons;
    }
}

uint16_t button_read()
{
    return button_reading;
}

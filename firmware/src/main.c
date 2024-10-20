/*
 * Controller Main
 * WHowe <github.com/whowechina>
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/platform.h"
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "bsp/board.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "tusb.h"
#include "usb_descriptors.h"

#ifdef AZAMAI_BUILD
#include "uart.h"

#include <FreeRTOS.h>
#include <task.h>
#endif

#include "aime.h"
#include "nfc.h"

#include "board_defs.h"

#include "touch.h"
#include "button.h"
#include "rgb.h"

#include "save.h"
#include "config.h"
#include "cli.h"
#include "commands.h"
#include "io.h"
#include "hid.h"

#define TASK_PRIORITY_HIGHEST (configMAX_PRIORITIES - 1)
#define TASK_PRIORITY_HIGH    (configMAX_PRIORITIES - 2)
#define TASK_PRIORITY_LOW     (configMAX_PRIORITIES - 3)
#define TASK_PRIORITY_LOWEST  (configMAX_PRIORITIES - 4)

static void button_lights_clear()
{
    for (int i = 0; i < 8; i++) {
        rgb_set_button(i, 0, 0);
    }
}

static void button_lights_rainbow()
{
    static uint16_t loop = 0;
    loop++;
    uint16_t buttons = button_read();
    for (int i = 0; i < 8; i++) {
        uint8_t phase = (i * 256 + loop) / 8;
        uint32_t color;
        if (buttons & (1 << i)) {
            color = rgb32_from_hsv(phase, 64, 255);
        } else {
            color = rgb32_from_hsv(phase, 240, 20);
        }
        rgb_set_button(i, color, 0);
    }
}

static void run_lights()
{
    static bool was_rainbow = true;
    bool go_rainbow = !io_is_active() && !aime_is_active();

    if (go_rainbow) {
        button_lights_rainbow();
    } else if (was_rainbow) {
        button_lights_clear();
    }

    was_rainbow = go_rainbow;
}


const int aime_intf = 3;
static void cdc_aime_putc(uint8_t byte)
{
    tud_cdc_n_write(aime_intf, &byte, 1);
    tud_cdc_n_write_flush(aime_intf);
}

static void aime_run()
{
    if (tud_cdc_n_available(aime_intf)) {
        uint8_t buf[32];
        uint32_t count = tud_cdc_n_read(aime_intf, buf, sizeof(buf));

        for (int i = 0; i < count; i++) {
            aime_feed(buf[i]);
        }
    }
}
static mutex_t core1_io_lock;
static void core1_loop()
{
    while (1) {
        if (mutex_try_enter(&core1_io_lock, NULL)) {
            run_lights();
            rgb_update();
            mutex_exit(&core1_io_lock);
        }
        cli_fps_count(1);
        sleep_ms(1);
    }
}

#ifndef AZAMAI_BUILD
static void core0_loop()
{
    uint64_t next_frame = time_us_64();

    while(1) {
        tud_task();
        io_update();

        cli_run();
        aime_run();
        save_loop();
        cli_fps_count(0);

        sleep_until(next_frame);
        next_frame += 1000; // 1KHz

#ifndef AZAMAI_BUILD
        touch_update();
#endif

        button_update();

        hid_update();
    }
}
#else
void usbd_task()
{
    const TickType_t xFrequency = pdMS_TO_TICKS(1);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        tud_task();

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void io_task()
{
    const TickType_t xFrequency = pdMS_TO_TICKS(1);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        io_update();
        button_update();
        hid_update();

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void cli_task()
{
    const TickType_t xFrequency = pdMS_TO_TICKS(1);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        cli_run();
        cli_fps_count(0);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void aime_task()
{
    const TickType_t xFrequency = pdMS_TO_TICKS(1);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        aime_run();

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void init_tasks()
{
    xTaskCreate(usbd_task, "usbd", configMINIMAL_STACK_SIZE, NULL, TASK_PRIORITY_HIGHEST, NULL);
    xTaskCreate(io_task, "io", configMINIMAL_STACK_SIZE, NULL, TASK_PRIORITY_HIGH, NULL);
    xTaskCreate(aime_task, "aime", configMINIMAL_STACK_SIZE, NULL, TASK_PRIORITY_LOW, NULL);
    xTaskCreate(cli_task, "cli", configMINIMAL_STACK_SIZE, NULL, TASK_PRIORITY_LOWEST, NULL);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    while (1) {
        tight_loop_contents();
    }
}

void vApplicationMallocFailedHook(void)
{
    while (1) {
        tight_loop_contents();
    }
}
#endif

void init()
{
#ifndef AZAMAI_BUILD
    sleep_ms(50);
    set_sys_clock_khz(150000, true);
    board_init();
#endif

    tusb_init();
    stdio_init_all();

    config_init();
    mutex_init(&core1_io_lock);

#ifdef AZAMAI_BUILD
    io_uart_init(TASK_PRIORITY_HIGH, TASK_PRIORITY_LOW);
#else
    save_init(board_id_32() ^ 0xcafe1111, &core1_io_lock);

    touch_init();
#endif

    button_init();
    rgb_init();

#ifdef AZAMAI_BUILD
    nfc_init_spi(SPI_PORT, SPI_MISO, SPI_SCK, SPI_MOSI, SPI_NSS);
#else
    nfc_attach_i2c(I2C_PORT);
#endif
    nfc_init();
    aime_init(cdc_aime_putc);
    aime_set_mode(mai_cfg->aime.mode);
    aime_virtual_aic(mai_cfg->aime.virtual_aic);

    cli_init("mai_pico>", "\n   << Mai Pico Controller >>\n"
                            " https://github.com/whowechina\n\n");
    commands_init();

    mai_runtime.key_stuck = button_is_stuck();

#ifdef AZAMAI_BUILD
    multicore_launch_core1(core1_loop);
    init_tasks();
    vTaskDelete(xTaskGetCurrentTaskHandle());
    while (1);
#endif
}

int main(void)
{
#ifdef AZAMAI_BUILD
    sleep_ms(50);
    set_sys_clock_khz(150000, true);
    board_init();

    xTaskCreate(init, "init", configMINIMAL_STACK_SIZE, NULL, TASK_PRIORITY_HIGHEST, NULL);

    vTaskStartScheduler();

    // Should never reach here
    while (1) {
        tight_loop_contents();
    }
#else

    init();
    multicore_launch_core1(core1_loop);
    core0_loop();
    return 0;
#endif
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    printf("Get from USB %d-%d\n", report_id, report_type);
    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
    hid_proc(buffer, bufsize);
}

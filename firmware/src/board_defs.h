/*
 * Mai Controller Board Definitions
 * WHowe <github.com/whowechina>
 */

#if defined BOARD_MAI_PICO

#ifndef AZAMAI_BUILD
#define I2C_PORT i2c1
#define I2C_SDA 6
#define I2C_SCL 7
#define I2C_FREQ 400*1000
#endif

#ifdef AZAMAI_BUILD
#define SPI_PORT spi0
#define SPI_MISO 16
#define SPI_SCK 18
#define SPI_MOSI 19
#define SPI_NSS 17

#define UART_PORT uart1
#define UART_TX 8
#define UART_RX 9
#endif

#ifdef AZAMAI_BUILD
#define RGB_ORDER GRB // or RGB
#define RGB_COUNT 13
#define RGB_DEF { \
  /* Button */ RGB_PIN(22, {2, 3}, {2, 2}, {2, 1}, {2, 0}, {2, 7}, {2, 6}, {2, 5}, {2, 4}), \
  /*  Body  */ RGB_PIN(15, {1, 0}, {1, 1}, {1, 2}, {1, 3}), \
  /*  Aime  */ RGB_PIN(28, {2, 0}) \
}
#else
#define RGB_PIN 13
#define RGB_ORDER GRB // or RGB
#define RGB_BUTTON_MAP { 5, 4, 3, 2, 1, 0, 7, 6, 8, 9, 10, 11 }
#endif

/* 8 main buttons, Test, Service, Navigate, Coin */
#ifdef AZAMAI_BUILD
#define BUTTON_DEF { 4, 5, 21, 20, 27, 26, 6, 7, 10, 11, 12, 13 }
#else
#define BUTTON_DEF { 1, 0, 4, 5, 8, 9, 3, 2, 12, 11, 10, 14 }
#endif

/* HID Keycode: https://github.com/hathach/tinyusb/blob/master/src/class/hid/hid.h */
// P1: WEDCXZAQ3(F1)(F2)(F3) P2: (Numpad)89632147*(F1)(F2)(F3)
#define BUTTON_NKRO_MAP_P1 "\x1a\x08\x07\x06\x1b\x1d\x04\x14\x20\x3a\x3b\x3c"
#define BUTTON_NKRO_MAP_P2 "\x60\x61\x5e\x5b\x5a\x59\x5c\x5f\x55\x3a\x3b\x3c"

#define TOUCH_MAP { E3, A2, B2, D2, E2, A1, B1, D1, E1, C2, A8, B8, \
                    D8, E8, A7, B7, D7, E7, A6, B6, D6, E6, A5, B5, \
                    D5, E5, C1, A4, B4, D4, E4, A3, B3, D3, XX, XX }
#else

#endif

/*
 * RGB LED (WS2812) Strip control
 * WHowe <github.com/whowechina>
 * 
 */

#include "rgb.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef AZAMAI_BUILD
#include "aime.h"
#endif

#include "bsp/board.h"
#include "hardware/pio.h"
#include "hardware/timer.h"

#include "ws2812.pio.h"

#include "board_defs.h"
#include "config.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifdef AZAMAI_BUILD
typedef struct {
    int led_cnt;
    int name; // the software index of the LED,
              // not essentially the physical index

    uint32_t buf;
    struct {
        uint32_t color; // current color
        uint32_t target; // target color
        uint16_t duration;
        uint16_t elapsed;
    } fade_ctx;
} rgb_member_t;
typedef struct {
    int pin;
    int member_cnt;
    rgb_member_t *members;
} rgb_pin_t;
#define RGB_PIN(pin, ...) {pin, sizeof((rgb_member_t[]){__VA_ARGS__}) / sizeof(rgb_member_t), (rgb_member_t[]){__VA_ARGS__}}
static const rgb_pin_t rgb_def[] = RGB_DEF;
#else
uint32_t rgb_buf[20];
static struct {
    uint32_t color; // current color
    uint32_t target; // target color
    uint16_t duration;
    uint16_t elapsed;
} fade_ctx[20];
static const uint8_t button_led_map[] = RGB_BUTTON_MAP;
#endif

#define _MAP_LED(x) _MAKE_MAPPER(x)
#define _MAKE_MAPPER(x) MAP_LED_##x
#define MAP_LED_RGB { c1 = r; c2 = g; c3 = b; }
#define MAP_LED_GRB { c1 = g; c2 = r; c3 = b; }

#define REMAP_BUTTON_RGB _MAP_LED(BUTTON_RGB_ORDER)
#define REMAP_TT_RGB _MAP_LED(TT_RGB_ORDER)

static inline uint32_t _rgb32(uint32_t c1, uint32_t c2, uint32_t c3, bool gamma_fix)
{
    if (gamma_fix) {
        c1 = ((c1 + 1) * (c1 + 1) - 1) >> 8;
        c2 = ((c2 + 1) * (c2 + 1) - 1) >> 8;
        c3 = ((c3 + 1) * (c3 + 1) - 1) >> 8;
    }
    
    return (c1 << 16) | (c2 << 8) | (c3 << 0);    
}

uint32_t rgb32(uint32_t r, uint32_t g, uint32_t b, bool gamma_fix)
{
#if BUTTON_RGB_ORDER == GRB
    return _rgb32(g, r, b, gamma_fix);
#else
    return _rgb32(r, g, b, gamma_fix);
#endif
}

uint32_t gray32(uint32_t c, bool gamma_fix)
{
    return rgb32(c, c, c, gamma_fix);
}

uint32_t rgb32_from_hsv(uint8_t h, uint8_t s, uint8_t v)
{
    uint32_t region, remainder, p, q, t;

    if (s == 0) {
        return v << 16 | v << 8 | v;
    }

    region = h / 43;
    remainder = (h % 43) * 6;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:
            return v << 16 | t << 8 | p;
        case 1:
            return q << 16 | v << 8 | p;
        case 2:
            return p << 16 | v << 8 | t;
        case 3:
            return p << 16 | q << 8 | v;
        case 4:
            return t << 16 | p << 8 | v;
        default:
            return v << 16 | p << 8 | q;
    }
}

static inline uint8_t lerp8b(uint8_t a, uint8_t b, uint8_t t)
{
    return a + (b - a) * t / 255;
}

static uint32_t lerp(uint32_t a, uint32_t b, uint8_t t)
{
    uint32_t c1 = lerp8b((a >> 16) & 0xff, (b >> 16) & 0xff, t);
    uint32_t c2 = lerp8b((a >> 8) & 0xff, (b >> 8) & 0xff, t);
    uint32_t c3 = lerp8b(a & 0xff, b & 0xff, t);
    return c1 << 16 | c2 << 8 | c3;
}

static void drive_led()
{
    static uint64_t last = 0;
    uint64_t now = time_us_64();
    if (now - last < 4000) { // no faster than 250Hz
        return;
    }
    last = now;

#ifdef AZAMAI_BUILD
    for (int i = 0; i < ARRAY_SIZE(rgb_def); i++) {
        for (int j = 0; j < rgb_def[i].member_cnt; j++) {
            rgb_member_t *member = &rgb_def[i].members[j];
            for (int k = 0; k < member->led_cnt; k++) {
                pio_sm_put_blocking(pio0, i, member->buf << 8u);
            }
        }
    }
#else
    for (int i = 0; i < ARRAY_SIZE(rgb_buf); i++) {
        int num = (i < 8) ? mai_cfg->rgb.per_button : mai_cfg->rgb.per_aux;
        for (int j = 0; j < num; j++) {
            pio_sm_put_blocking(pio0, 0, rgb_buf[i] << 8u);
        }
    }
#endif
}

static inline uint32_t apply_level(uint32_t color)
{
    unsigned r = (color >> 16) & 0xff;
    unsigned g = (color >> 8) & 0xff;
    unsigned b = color & 0xff;

    r = r * mai_cfg->color.level / 255;
    g = g * mai_cfg->color.level / 255;
    b = b * mai_cfg->color.level / 255;

    return r << 16 | g << 8 | b;
}

static void fade_ctrl()
{
    static uint64_t last = 0;
    uint64_t now = time_us_64();
    uint32_t delta_ms = (now - last) / 1000;

    if (delta_ms < 4) { // no faster than 250Hz
        return;
    }

#ifdef AZAMAI_BUILD
    for (int i = 0; i < ARRAY_SIZE(rgb_def); i++) {
        for (int j = 0; j < rgb_def[i].member_cnt; j++) {
            rgb_member_t *member = &rgb_def[i].members[j];
            if (member->fade_ctx.duration == 0) {
                continue;
            }

            member->fade_ctx.elapsed += delta_ms;
            if (member->fade_ctx.elapsed >= member->fade_ctx.duration) {
                member->fade_ctx.duration = 0;
                member->buf = member->fade_ctx.target;
                continue;
            }

            uint8_t progress = member->fade_ctx.elapsed * 255 / member->fade_ctx.duration;
            uint32_t color = lerp(member->fade_ctx.color, member->fade_ctx.target, progress);
            member->buf = apply_level(color);
        }
    }
#else
    for (int i = 0; i < ARRAY_SIZE(fade_ctx); i++) {
        if (fade_ctx[i].duration == 0) {
            continue;
        }

        fade_ctx[i].elapsed += delta_ms;
        if (fade_ctx[i].elapsed >= fade_ctx[i].duration) {
            fade_ctx[i].duration = 0;
            rgb_buf[i] = fade_ctx[i].target;
            continue;
        }

        uint8_t progress = fade_ctx[i].elapsed * 255 / fade_ctx[i].duration;
        uint32_t color = lerp(fade_ctx[i].color, fade_ctx[i].target, progress);
        rgb_buf[i] = apply_level(color);
    }
#endif

    last = now;
}

#ifdef AZAMAI_BUILD
static void set_color(unsigned pin_index, unsigned name, uint32_t color, uint8_t speed)
{
    if (pin_index >= ARRAY_SIZE(rgb_def)) {
        return;
    }

    rgb_member_t *member = NULL;
    for (int i = 0; i < rgb_def[pin_index].member_cnt; i++) {
        if (rgb_def[pin_index].members[i].name == name) {
            member = &rgb_def[pin_index].members[i];
            break;
        }
    }
    if (!member) {
        return;
    }

    if (speed > 0) {
        member->fade_ctx.target = color;
        member->fade_ctx.duration = 4095 / speed * 8;
        member->fade_ctx.elapsed = 0;
    } else {
        member->fade_ctx.color = color;
        member->fade_ctx.duration = 0;
        member->buf = apply_level(color);
    }
}
#else
static void set_color(unsigned index, uint32_t color, uint8_t speed)
{
    if (index >= ARRAY_SIZE(fade_ctx)) {
        return;
    }

    if (speed > 0) {
        fade_ctx[index].target = color;
        fade_ctx[index].duration = 4095 / speed * 8;
        fade_ctx[index].elapsed = 0;
    } else {
        fade_ctx[index].color= color;
        fade_ctx[index].duration = 0;
        rgb_buf[index] = apply_level(color);
    }
}
#endif

void rgb_set_button(unsigned index, uint32_t color, uint8_t speed)
{
#ifdef AZAMAI_BUILD
    set_color(0, index, color, speed);
#else
    if (index >= ARRAY_SIZE(button_led_map)) {
        return;
    }
    set_color(button_led_map[index], color, speed);
#endif
}

void rgb_set_cab(unsigned index, uint32_t color)
{
#ifdef AZAMAI_BUILD
    set_color(1, index, color, 0);
#else
    if (index >= 3) {
        return;
    }
    set_color(8 + index, color, 0);
#endif
}

void rgb_init()
{
    uint pio0_offset = pio_add_program(pio0, &ws2812_program);

#ifdef AZAMAI_BUILD
    for (int i = 0; i < ARRAY_SIZE(rgb_def); i++) {
        gpio_set_drive_strength(rgb_def[i].pin, GPIO_DRIVE_STRENGTH_2MA);
        ws2812_program_init(pio0, i, pio0_offset, rgb_def[i].pin, 800000, false);
    }
#else
    gpio_set_drive_strength(RGB_PIN, GPIO_DRIVE_STRENGTH_2MA);
    ws2812_program_init(pio0, 0, pio0_offset, RGB_PIN, 800000, false);
#endif
}

void rgb_update()
{
#ifdef AZAMAI_BUILD
    set_color(2, 0, aime_led_color(), 0);
#endif

    fade_ctrl();
    drive_led();
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>

#include "pico/stdio.h"
#include "pico/stdlib.h"

#include "tusb.h"

#include "mpr121.h"
#include "touch.h"
#include "button.h"
#include "config.h"
#include "save.h"
#include "cli.h"

#include "aime.h"
#include "nfc.h"

#define SENSE_LIMIT_MAX 9
#define SENSE_LIMIT_MIN -9

static void disp_rgb()
{
    printf("[RGB]\n");
    printf("  Number per button: %d, number per aux: %d\n",
            mai_cfg->rgb.per_button, mai_cfg->rgb.per_aux);
    printf("  Key on: %06lx, off: %06lx\n  Level: %d\n",
           mai_cfg->color.key_on, mai_cfg->color.key_off, mai_cfg->color.level);
}

static void print_sense_zone(const char *title, const int8_t *zones, int num)
{
    printf("   %s |", title);
    for (int i = 0; i < num; i++) {
        printf("%2d |", zones[i]);
    }
    printf("\n");
}

static void disp_sense()
{
    printf("[Sense]\n");
    printf("  Filter: %u, %u, %u\n", mai_cfg->sense.filter >> 6,
                                    (mai_cfg->sense.filter >> 4) & 0x03,
                                    mai_cfg->sense.filter & 0x07);
    printf("  Sensitivity (global: %+d):\n", mai_cfg->sense.global);
    printf("     |_1_|_2_|_3_|_4_|_5_|_6_|_7_|_8_|\n");
    print_sense_zone("A", mai_cfg->sense.zones, 8);
    print_sense_zone("B", mai_cfg->sense.zones + 8, 8);
    print_sense_zone("C", mai_cfg->sense.zones + 16, 2);
    print_sense_zone("D", mai_cfg->sense.zones + 18, 8);
    print_sense_zone("E", mai_cfg->sense.zones + 26, 8);
    printf("  Debounce (touch, release): %d, %d\n",
           mai_cfg->sense.debounce_touch, mai_cfg->sense.debounce_release);
}

static void disp_hid()
{
    printf("[HID]\n");
    const char *nkro[] = {"off", "key1", "key2"};
    printf("  Joy: %s, NKRO: %s\n", mai_cfg->hid.joy ? "ON" : "OFF",
           mai_cfg->hid.nkro <= 2 ? nkro[mai_cfg->hid.nkro] : "key1");
    if (mai_runtime.key_stuck) {
        printf("  !!! Button stuck, force JOY only !!!\n");
    }
}

static void disp_aime()
{
    printf("[AIME]\n");
    printf("  NFC Module: %s\n", nfc_module_name());
    printf("  Virtual AIC: %s\n", mai_cfg->aime.virtual_aic ? "ON" : "OFF");
    printf("  Protocol Mode: %d\n", mai_cfg->aime.mode);
}

static void disp_gpio()
{
    printf("[GPIO]\n ");
    for (int i = 0; i < 8; i++) {
        printf(" B%d:GP%d", i + 1, button_real_gpio(i));
    }
    printf("\n  Test:GP%d Svc:GP%d Nav:GP%d Coin:GP%d\n",
        button_real_gpio(8), button_real_gpio(9),
        button_real_gpio(10), button_real_gpio(11));
}

static void disp_touch()
{
#ifndef AZAMAI_BUILD
    printf("[Touch]\n");
    printf("     ADDR|_0|_1|_2|_3|_4|_5|_6|_7|_8|_9|10|11|\n");

    for (int m = 0; m < 3; m++) {
        printf("  %d: 0x%02x|", m, MPR121_BASE_ADDR + m);
        for (int chn = 0; chn < 12; chn++) {
            int key = touch_key_from_channel(m * 12 + chn);
            printf("%2s|", touch_key_name(key));
        }
        printf("\n");
    }
#endif
}

static void disp_tweak()
{
    printf("[Tweak]\n");
    printf("  Main Buttons Active-High: %s\n",
           mai_cfg->tweak.main_button_active_high ? "ON" : "OFF");
    printf("  Aux Buttons Active-High: %s\n",
           mai_cfg->tweak.aux_button_active_high ? "ON" : "OFF");
}

#define ARRAYSIZE(x) (sizeof(x) / sizeof(x[0]))

void handle_display(int argc, char *argv[])
{
    const char *usage = "Usage: display [rgb|sense|hid|gpio|touch|aime|tweak]\n";
    if (argc > 1) {
        printf(usage);
        return;
    }

    const char *choices[] = {"rgb", "sense", "hid", "gpio", "touch", "aime", "tweak"};
    static void (*disp_funcs[])() = {
        disp_rgb,
        disp_sense,
        disp_hid,
        disp_gpio,
        disp_touch,
        disp_aime,
        disp_tweak,
    };
  
    static_assert(ARRAYSIZE(choices) == ARRAYSIZE(disp_funcs),
        "Choices and disp_funcs arrays must have the same number of elements");

    if (argc == 0) {
        for (int i = 0; i < ARRAYSIZE(disp_funcs); i++) {
            disp_funcs[i]();
        }
        return;
    }

    int choice = cli_match_prefix(choices, ARRAYSIZE(choices), argv[0]);
    if (choice < 0) {
        printf(usage);
        return;
    }

    if (choice > ARRAYSIZE(disp_funcs)) {
        return;
    }

    disp_funcs[choice]();
}

static void handle_rgb(int argc, char *argv[])
{
    const char *usage = "Usage: rgb <1..16> <1..16>\n";
    if (argc != 2) {
        printf(usage);
        return;
    }

    int per_button = cli_extract_non_neg_int(argv[0], 0);
    int per_aux = cli_extract_non_neg_int(argv[1], 0);    
    if ((per_button < 1) || (per_button > 16) ||
        (per_aux < 1) || (per_aux > 16)) {
        printf(usage);
        return;
    }

    mai_cfg->rgb.per_button = per_button;
    mai_cfg->rgb.per_aux = per_aux;

    config_changed();
    disp_rgb();
}

static void handle_level(int argc, char *argv[])
{
    const char *usage = "Usage: level <0..255>\n";
    if (argc != 1) {
        printf(usage);
        return;
    }

    int level = cli_extract_non_neg_int(argv[0], 0);
    if ((level < 0) || (level > 255)) {
        printf(usage);
        return;
    }

    mai_cfg->color.level = level;
    config_changed();
    disp_rgb();
}

static void handle_stat(int argc, char *argv[])
{
#ifndef AZAMAI_BUILD
    if (argc == 0) {
        for (int col = 0; col < 4; col++) {
            printf(" %2dA |", col * 4 + 1);
            for (int i = 0; i < 4; i++) {
                printf("%6u|", touch_count(col * 8 + i * 2));
            }
            printf("\n   B |");
            for (int i = 0; i < 4; i++) {
                printf("%6u|", touch_count(col * 8 + i * 2 + 1));
            }
            printf("\n");
        }
    } else if ((argc == 1) &&
               (strncasecmp(argv[0], "reset", strlen(argv[0])) == 0)) {
        touch_reset_stat();
    } else {
        printf("Usage: stat [reset]\n");
    }
#endif
}

static void handle_hid(int argc, char *argv[])
{
    const char *usage = "Usage: hid <joy|key1|key2>\n";
    if (argc != 1) {
        printf(usage);
        return;
    }

    const char *choices[] = {"joy", "key1", "key2"};
    int match = cli_match_prefix(choices, 3, argv[0]);
    if (match < 0) {
        printf(usage);
        return;
    }

    switch (match) {
            break;
        case 1:
            mai_cfg->hid.joy = 0;
            mai_cfg->hid.nkro = 1;
            break;
        case 2:
            mai_cfg->hid.joy = 0;
            mai_cfg->hid.nkro = 2;
            break;
        case 0:
        default:
            mai_cfg->hid.joy = 1;
            mai_cfg->hid.nkro = 0;
            break;
    }
    config_changed();
    disp_hid();
}

static void handle_filter(int argc, char *argv[])
{
    const char *usage = "Usage: filter <first> <second> [interval]\n"
                        "    first: First iteration [0..3]\n"
                        "   second: Second iteration [0..3]\n"
                        " interval: Interval of second iterations [0..7]\n";
    if ((argc < 2) || (argc > 3)) {
        printf(usage);
        return;
    }

    int ffi = cli_extract_non_neg_int(argv[0], 0);
    int sfi = cli_extract_non_neg_int(argv[1], 0);
    int intv = mai_cfg->sense.filter & 0x07;
    if (argc == 3) {
        intv = cli_extract_non_neg_int(argv[2], 0);
    }

    if ((ffi < 0) || (ffi > 3) || (sfi < 0) || (sfi > 3) ||
        (intv < 0) || (intv > 7)) {
        printf(usage);
        return;
    }

    mai_cfg->sense.filter = (ffi << 6) | (sfi << 4) | intv;

#ifndef AZAMAI_BUILD
    touch_update_config();
#endif
    config_changed();
    disp_sense();
}

static int8_t *extract_key(const char *param)
{
    if (strlen(param) != 2) {
        return NULL;
    }

    int zone = param[0] - 'A';
    int id = param[1] - '1';

    if (zone < 0 || zone > 4 || id < 0 || id > 7) {
        return NULL;
    }
    if ((zone == 2) && (id > 1)) {
        return NULL; // C1 and C2 only
    }

    const int offsets[] = { 0, 8, 16, 18, 26 };

    return &mai_cfg->sense.zones[offsets[zone] + id];
}

static void sense_do_op(int8_t *target, char op)
{
    if (op == '+') {
        if (*target < SENSE_LIMIT_MAX) {
            (*target)++;
        }
    } else if (op == '-') {
        if (*target > SENSE_LIMIT_MIN) {
            (*target)--;
        }
    } else if (op == '0') {
        *target = 0;
    }
}

static void handle_sense(int argc, char *argv[])
{
    const char *usage = "Usage: sense [key|*] <+|-|0>\n"
                        "Example:\n"
                        "  >sense +\n"
                        "  >sense -\n"
                        "  >sense A3 +\n"
                        "  >sense C1 -\n"
                        "  >sense * 0\n";
    if ((argc < 1) || (argc > 2)) {
        printf(usage);
        return;
    }

    const char *op = argv[argc - 1];
    if ((strlen(op) != 1) || !strchr("+-0", op[0])) {
        printf(usage);
        return;
    }

    if (argc == 1) {
        sense_do_op(&mai_cfg->sense.global, op[0]);
    } else {
        if (strcmp(argv[0], "*") == 0) {
            for (int i = 0; i < sizeof(mai_cfg->sense.zones); i++) {
                sense_do_op(&mai_cfg->sense.zones[i], op[0]);
            }
        } else {
            int8_t *key = extract_key(argv[0]);
            if (!key) {
                printf(usage);
                return;
            }
            sense_do_op(key, op[0]);
        }
    }

#ifndef AZAMAI_BUILD
    touch_update_config();
#endif
    config_changed();
    disp_sense();
}

static void handle_debounce(int argc, char *argv[])
{
    const char *usage = "Usage: debounce <touch> [release]\n"
                        "  touch, release: 0..7\n";
    if ((argc < 1) || (argc > 2)) {
        printf(usage);
        return;
    }

    int touch = mai_cfg->sense.debounce_touch;
    int release = mai_cfg->sense.debounce_release;
    if (argc >= 1) {
        touch = cli_extract_non_neg_int(argv[0], 0);
    }
    if (argc == 2) {
        release = cli_extract_non_neg_int(argv[1], 0);
    }

    if ((touch < 0) || (release < 0) ||
        (touch > 7) || (release > 7)) {
        printf(usage);
        return;
    }

    mai_cfg->sense.debounce_touch = touch;
    mai_cfg->sense.debounce_release = release;

#ifndef AZAMAI_BUILD
    touch_update_config();
#endif
    config_changed();
    disp_sense();
}

#ifndef AZAMAI_BUILD
static void print_raw_zones(const char *title, const uint16_t *raw, int num)
{
    printf(" %s |", title);
    for (int i = 0; i < num; i++) {
        printf(" %3d |", raw[i]);
    }
    printf("\n");
}
#endif

static void handle_raw()
{
#ifndef AZAMAI_BUILD
    printf("Touch raw readings:\n");
    const uint16_t *raw = touch_raw();
    printf("   Sensor: 0: %s, 1: %s 2: %s\n",
            touch_sensor_ok(0) ? "OK" : "ERR",
            touch_sensor_ok(1) ? "OK" : "ERR",
            touch_sensor_ok(2) ? "OK" : "ERR");

    printf("   |__1__|__2__|__3__|__4__|__5__|__6__|__7__|__8__|\n");
    print_raw_zones("A", raw, 8);
    print_raw_zones("B", raw + 8, 8);
    print_raw_zones("C", raw + 16, 2);
    print_raw_zones("D", raw + 18, 8);
    print_raw_zones("E", raw + 26, 8);
#endif
}

static void handle_whoami()
{
    const char *msg[] = {"\nThis is Command Line port.\n", "\nThis is Touch port.\n", "\nThis is LED port.\n"};
    for (int i = 0; i < 3; i++) {
        tud_cdc_n_write(i, msg[i], strlen(msg[i]));
        tud_cdc_n_write_flush(i);
    }
}

static void handle_save()
{
    save_request(true);
}

static void handle_gpio(int argc, char *argv[])
{
    const char *usage = "Usage: gpio main <gpio1> <gpio2> ... <gpio8>\n"
                        "       gpio <test|service|navigate|coin> <gpio>\n"
                        "       gpio reset\n"
                        "  gpio: 0..29\n";
    if (argc == 1) {
        if (strcasecmp(argv[0], "reset") != 0) {
            printf(usage);
            return;
        }
        for (int i = 0; i < sizeof(mai_cfg->alt.buttons); i++) {
            mai_cfg->alt.buttons[i] = 0xff;
        }
    } else if (argc == 9) {
        const char *choices[] = {"main"};
        if (cli_match_prefix(choices, 1, argv[0]) < 0) {
            printf(usage);
            return;
        }
        uint8_t gpio_main[8];
        for (int i = 0; i < 8; i++) {
            int gpio = cli_extract_non_neg_int(argv[i + 1], 0);
            if ((gpio < 0) || (gpio > 29)) {
                printf(usage);
                return;
            }
            gpio_main[i] = gpio;
        }
        for (int i = 0; i < 8; i++) {
            int gpio = gpio_main[i];
            bool is_default = (gpio == button_default_gpio(i));
            mai_cfg->alt.buttons[i] = is_default ? 0xff : gpio;
        }
    } else if (argc == 2) {
        const char *choices[] = {"test", "service", "navigate", "coin"};
        const uint8_t button_pos[] = {8, 9, 10, 11};
        int match = cli_match_prefix(choices, 4, argv[0]);
        uint8_t gpio = cli_extract_non_neg_int(argv[1], 0);
        if ((match < 0) || (gpio > 29)) {
            printf(usage);
            return;
        }
        int index = button_pos[match];
        bool is_default = (gpio == button_default_gpio(index));
        mai_cfg->alt.buttons[index] = is_default ? 0xff : gpio;
    } else {
        printf(usage);
        return;
    }
    config_changed();
    button_init(); // Re-init the buttons
    disp_gpio();
}

#ifndef AZAMAI_BUILD
static void detect_touch()
{
    bool touched = false;
    for (int i = 0; i < 34; i++) {
        if (touch_touched(i)) {
            touched = true;
            printf("Touched: %s", touch_key_name(i));
            uint8_t pad = touch_key_channel(i);
            if (pad >= 0) {
                printf(" (Sensor %d, Electrode %d)\n", pad / 12, pad % 12 + 1);
            } else {
                printf(" (nil)\n");
            }
        }
    }
    if (!touched) {
        printf("No touch detected.\n");
    }
}
#endif

#ifndef AZAMAI_BUILD
static bool set_touch_map(int argc, char *argv[])
{
    if (argc != 3) {
        return false;
    }
    if (strlen(argv[2]) != 2) {
        return false;
    }
    int sensor = cli_extract_non_neg_int(argv[0], 0);
    int channel = cli_extract_non_neg_int(argv[1], 0);
    int key = touch_key_by_name(argv[2]);

    if ((sensor < 0) || (sensor > 2) ||
        (channel < 0) || (channel > 11) ||
        (key < 0)) {
        return false;
    }
    touch_set_map(sensor * 12 + channel, key);
    return true;
}
#endif

static void handle_touch(int argc, char *argv[])
{
#ifndef AZAMAI_BUILD
    const char *usage = "Usage: touch [<sensor> <channel> <key>]\n"
                        "  sensor: 0..2\n"
                        " channel: 0..11\n"
                        "     key: A1, C2, E5, etc. XX means Not Connected.)\n";
    if (argc == 0) {
        detect_touch();
    } else if (set_touch_map(argc, argv)) {
        disp_touch();
    } else {
        printf(usage);
    }
#endif
}


static bool handle_aime_mode(const char *mode)
{
    if (strcmp(mode, "0") == 0) {
        mai_cfg->aime.mode = 0;
    } else if (strcmp(mode, "1") == 0) {
        mai_cfg->aime.mode = 1;
    } else {
        return false;
    }
    aime_set_mode(mai_cfg->aime.mode);
    config_changed();
    return true;
}

static bool handle_aime_virtual(const char *onoff)
{
    if (strcasecmp(onoff, "on") == 0) {
        mai_cfg->aime.virtual_aic = 1;
    } else if (strcasecmp(onoff, "off") == 0) {
        mai_cfg->aime.virtual_aic = 0;
    } else {
        return false;
    }
    aime_virtual_aic(mai_cfg->aime.virtual_aic);
    config_changed();
    return true;
}

static void handle_aime(int argc, char *argv[])
{
    const char *usage = "Usage:\n"
                        "    aime mode <0|1>\n"
                        "    aime virtual <on|off>\n";
    if (argc != 2) {
        printf("%s", usage);
        return;
    }

    const char *commands[] = { "mode", "virtual" };
    int match = cli_match_prefix(commands, 2, argv[0]);
    
    bool ok = false;
    if (match == 0) {
        ok = handle_aime_mode(argv[1]);
    } else if (match == 1) {
        ok = handle_aime_virtual(argv[1]);
    }

    if (ok) {
        disp_aime();
    } else {
        printf("%s", usage);
    }
}

static void handle_tweak(int argc, char *argv[])
{
    const char *usage = "Usage: tweak <option> <on|off>\n"
                        "Options:\n"
                        "    main_button_active_high\n"
                        "    aux_button_active_high\n";
    if (argc != 2) {
        printf(usage);
        return;
    }

    const char *options[] = {
        "main_button_active_high",
        "aux_button_active_high",
    };

    const char *switches[] = { "on", "off" };

    int option = cli_match_prefix(options, 2, argv[0]);
    int on_off = cli_match_prefix(switches, 2, argv[1]);
    if ((option < 0) || (on_off < 0)) {
        printf(usage);
        return;
    }

    bool active = on_off == 0 ? true : false;
    if (option == 0) {
        mai_cfg->tweak.main_button_active_high = active;
    } else if (option == 1) {
        mai_cfg->tweak.aux_button_active_high = active;
    }

    config_changed();
    disp_tweak();
}

void commands_init()
{
    cli_register("display", handle_display, "Display all config.");
    cli_register("rgb", handle_rgb, "Set RGB LED number for main button and aux buttons.");
    cli_register("level", handle_level, "Set LED brightness level.");
    cli_register("stat", handle_stat, "Display or reset statistics.");
    cli_register("hid", handle_hid, "Set HID mode.");
    cli_register("filter", handle_filter, "Set pre-filter config.");
    cli_register("sense", handle_sense, "Set sensitivity config.");
    cli_register("debounce", handle_debounce, "Set debounce config.");
    cli_register("raw", handle_raw, "Show key raw readings.");
    cli_register("whoami", handle_whoami, "Identify each com port.");
    cli_register("save", handle_save, "Save config to flash.");
    cli_register("gpio", handle_gpio, "Set GPIO pins for buttons.");
    cli_register("touch", handle_touch, "Custimze touch mapping.");
    cli_register("tweak", handle_tweak, "Miscellaneous tweak options.");
    cli_register("factory", config_factory_reset, "Reset everything to default.");
    cli_register("aime", handle_aime, "AIME settings.");
}

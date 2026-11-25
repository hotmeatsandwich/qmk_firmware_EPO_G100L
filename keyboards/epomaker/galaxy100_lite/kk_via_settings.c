// SPDX-License-Identifier: GPL-2.0-or-later
#include "keyboard_common.h"

#ifdef KK_VIA_SETTINGS
#    include <stddef.h>
#    include "crc.h"
#    include "eeconfig.h"
#    include "eeprom.h"
#    include "kk_via_settings.h"
#    if defined(VIA_ENABLE) && defined(RAW_HID_ORDERED_RESPONSES)
#        include "raw_hid.h"
#        include "usb_descriptor.h"
#        include "usb_device_state.h"
#    endif

#    define SETTINGS_MAGIC 0x4B04U
#    define SETTINGS_MAGIC_FIXED_INDICATOR_FADE 0x4B03U
#    define SETTINGS_MAGIC_SEPARATE_FADES 0x4B02U
#    define SETTINGS_MAGIC_CUSTOM_COLORS 0x4B01U
#    define SETTINGS_ADDR (DYNAMIC_KEYMAP_EEPROM_MAX_ADDR + 1U)
#    define FADE_MAX_MS 2000U
#    define INDICATOR_MASK ((1U << KK_IND_COUNT) - 1U)

enum {
    LEGACY_BRIGHTNESS_SLOT = 11,
    LEGACY_INDICATOR_SLOTS = 13,
};

typedef struct {
    uint8_t h;
    uint8_t s;
} kk_hs_t;

typedef struct {
    uint16_t gesture[KK_GESTURE_COUNT];
    uint16_t behavior_ms[KK_BEHAVIOR_COUNT];
    uint16_t enabled_indicators;
    hsv_t    indicator[KK_IND_COUNT];
    kk_hs_t  battery_low;
    kk_hs_t  battery_medium;
    uint8_t  checksum;
    uint16_t magic;
} kk_settings_t;

typedef struct {
    uint16_t gesture[KK_GESTURE_COUNT];
    uint16_t fade_ms[3];
    uint16_t enabled_indicators;
    hsv_t    indicator[LEGACY_INDICATOR_SLOTS];
    uint8_t  checksum;
    uint16_t magic;
} kk_legacy_settings_t;

_Static_assert(sizeof(kk_settings_t) == 64U && SETTINGS_ADDR + sizeof(kk_settings_t) <= EEPROM_SIZE, "VIA settings must fit above the macro region");
_Static_assert(sizeof(kk_legacy_settings_t) == sizeof(kk_settings_t) && offsetof(kk_legacy_settings_t, magic) == offsetof(kk_settings_t, magic), "Legacy VIA settings layout changed");

static const kk_settings_t default_settings = {
    .behavior_ms = {
        [KK_BEHAVIOR_FADE]           = KK_BACKLIGHT_FADE_MS,
        [KK_BEHAVIOR_INDICATOR_FADE] = KK_INDICATOR_FADE_MS,
        [KK_BEHAVIOR_CLICK_WINDOW]   = KK_ENCODER_CLICK_MS,
        [KK_BEHAVIOR_HOLD_TIME]      = KK_ENCODER_HOLD_MS,
    },
    .enabled_indicators = INDICATOR_MASK,
    .indicator = {
        [KK_IND_CAPS]       = {HSV_WHITE},
        [KK_IND_NUM]        = {HSV_WHITE},
        [KK_IND_WIN]        = {HSV_WHITE},
        [KK_IND_BLE1]       = {HSV_BLUE},
        [KK_IND_BLE2]       = {HSV_BLUE},
        [KK_IND_BLE3]       = {HSV_BLUE},
        [KK_IND_24G]        = {HSV_GREEN},
        [KK_IND_USB]        = {HSV_WHITE},
        [KK_IND_BATTERY]    = {HSV_GREEN},
        [KK_IND_BRIGHTNESS] = {HSV_WHITE},
    },
    .battery_low    = {.h = 0, .s = UINT8_MAX},
    .battery_medium = {.h = 43, .s = UINT8_MAX},
};

static kk_settings_t settings;
static bool          settings_dirty;
static uint32_t      settings_activity;
static uint16_t      indicator_color_updates;
static uint16_t      rendered_color_updates;
#    ifdef RGB_MATRIX_DEFER_EEPROM_WRITE
static rgb_config_t last_rgb_config;
#    endif
#    ifdef VIA_ENABLE
static bool settings_save_requested;
#    endif

static void reset_settings(void) {
    settings       = default_settings;
    settings_dirty = false;
    indicator_color_updates = 0;
#    ifdef VIA_ENABLE
    settings_save_requested = false;
#    endif
}

void kk_via_settings_load(void) {
    union {
        kk_settings_t        current;
        kk_legacy_settings_t legacy;
    } stored;
    eeprom_read_block(&stored, (const void *)SETTINGS_ADDR, sizeof(stored));
    reset_settings();
    settings_activity = timer_read32();
#    ifdef RGB_MATRIX_DEFER_EEPROM_WRITE
    last_rgb_config = rgb_matrix_config;
#    endif
    const kk_legacy_settings_t *legacy = &stored.legacy;
    if ((stored.current.magic == SETTINGS_MAGIC || stored.current.magic == SETTINGS_MAGIC_FIXED_INDICATOR_FADE) &&
        stored.current.checksum == crc8(&stored.current, offsetof(kk_settings_t, checksum))) {
        settings = stored.current;
        if (stored.current.magic == SETTINGS_MAGIC) {
            return;
        }
        settings.behavior_ms[KK_BEHAVIOR_INDICATOR_FADE] = KK_INDICATOR_FADE_MS;
    } else if ((legacy->magic == SETTINGS_MAGIC_SEPARATE_FADES || legacy->magic == SETTINGS_MAGIC_CUSTOM_COLORS) &&
               legacy->checksum == crc8(legacy, offsetof(kk_legacy_settings_t, checksum))) {
        memcpy(settings.gesture, legacy->gesture, sizeof(settings.gesture));
        memcpy(settings.indicator, legacy->indicator, KK_IND_BRIGHTNESS * sizeof(hsv_t));
        settings.behavior_ms[KK_BEHAVIOR_FADE] = legacy->fade_ms[0];
        settings.indicator[KK_IND_BRIGHTNESS]  = legacy->indicator[LEGACY_BRIGHTNESS_SLOT];
        const hsv_t old_default = {HSV_BLUE};
        if (settings.indicator[KK_IND_BRIGHTNESS].h == old_default.h && settings.indicator[KK_IND_BRIGHTNESS].s == old_default.s) {
            settings.indicator[KK_IND_BRIGHTNESS].h = 0;
            settings.indicator[KK_IND_BRIGHTNESS].s = 0;
        }
        if (legacy->magic == SETTINGS_MAGIC_SEPARATE_FADES) {
            settings.enabled_indicators = (legacy->enabled_indicators & ((1U << KK_IND_BRIGHTNESS) - 1U)) |
                                          (((legacy->enabled_indicators >> LEGACY_BRIGHTNESS_SLOT) & 1U) << KK_IND_BRIGHTNESS);
        }
    } else {
        return;
    }
    settings_dirty = true;
#    ifdef VIA_ENABLE
    settings_save_requested = true;
#    endif
}

void eeconfig_init_kb(void) {
    reset_settings();
    eeconfig_init_user();
}

uint16_t kk_gesture_keycode(kk_gesture_t gesture) {
    uint16_t keycode = settings.gesture[gesture];
    return keycode > KC_TRNS ? keycode : KC_NO;
}

uint16_t kk_behavior_duration(kk_behavior_t behavior) {
    return settings.behavior_ms[behavior];
}

bool kk_indicator_enabled(kk_indicator_t indicator) {
    return (settings.enabled_indicators & (1U << indicator)) != 0;
}

rgb_t kk_indicator_color(kk_indicator_t indicator, rgb_t color) {
    hsv_t   style = settings.indicator[indicator];
    uint8_t intensity = MAX(MAX(color.r, color.g), color.b);
    style.v = ((uint16_t)intensity * (style.v + 1U)) >> 8;
    return hsv_to_rgb_nocie(style);
}

rgb_t kk_battery_color(uint8_t percent) {
    hsv_t style = settings.indicator[KK_IND_BATTERY];
    if (percent <= BATTERY_MED_THRESHOLD) {
        kk_hs_t color = percent <= BATTERY_LOW_THRESHOLD ? settings.battery_low : settings.battery_medium;
        style.h = color.h;
        style.s = color.s;
    }
    return hsv_to_rgb_nocie(style);
}

uint8_t kk_indicator_color_flags(kk_indicator_t indicator) {
#    ifdef KK_INDICATOR_FADE
    uint16_t update = indicator_color_updates & (1U << indicator);
    rendered_color_updates |= update;
    return update ? KK_INDICATOR_LIVE_COLOR : 0;
#    else
    (void)indicator;
    return 0;
#    endif
}

void kk_indicator_begin_color_frame(void) {
    rendered_color_updates = 0;
}

void kk_indicator_end_color_frame(void) {
    indicator_color_updates &= ~rendered_color_updates;
}

#    ifdef VIA_ENABLE
#        include "via.h"

enum {
    KK_VIA_GESTURES   = 6,
    KK_VIA_BEHAVIOR   = 7,
    KK_VIA_INDICATORS = 8,
};

enum {
    KK_VIA_IND_BRIGHTNESS,
    KK_VIA_IND_COLOR,
    KK_VIA_IND_ENABLED,
    KK_VIA_IND_FIELDS,
};

enum {
    KK_VIA_BATTERY_LOW_COLOR    = 40,
    KK_VIA_BATTERY_MEDIUM_COLOR = 41,
    KK_VIA_BATTERY_LEVEL        = 42,
};

enum {
    KK_BATTERY_CHARGING = 1,
    KK_BATTERY_CHARGED  = 2,
};

#        ifdef RAW_HID_ORDERED_RESPONSES
enum {
    VIA_UI_SYNC_REQUEST          = 0x16,
    VIA_UI_SYNC_VERSION          = 0x01,
    VIA_UI_SYNC_BY_CHANNEL_VALUE = 0x01,
};

static bool     battery_level_requested;
static uint16_t battery_level_reported;
#        endif

static uint16_t battery_level_snapshot(void) {
    uint8_t state = es_stdby_pin_state;
    uint8_t level = MIN(Keyboard_Info.Batt_Number, 100);
    if (state == KK_BATTERY_CHARGED) {
        level = 100;
    } else if (state == KK_BATTERY_CHARGING) {
        level = MIN(level, 99);
    }
    return ((uint16_t)state << 8) | level;
}

static void write_battery_level(uint8_t *out, uint16_t snapshot) {
    uint8_t level = (uint8_t)snapshot;
    uint8_t state = snapshot >> 8;
    if (level == 100) {
        *out++ = '1';
    }
    if (level >= 10) {
        *out++ = '0' + (level / 10) % 10;
    }
    *out++ = '0' + level % 10;
    *out++ = '%';
    if (state == KK_BATTERY_CHARGING) {
        memcpy(out, " (Charging)", sizeof(" (Charging)"));
    } else if (state == KK_BATTERY_CHARGED) {
        memcpy(out, " (Charged)", sizeof(" (Charged)"));
    } else {
        *out = '\0';
    }
}

#        ifdef RAW_HID_ORDERED_RESPONSES
static void sync_battery_level(void) {
    if (!battery_level_requested) {
        return;
    }
    usb_configure_state_t state = usb_device_state_get_configure_state();
    if (state != USB_DEVICE_STATE_CONFIGURED) {
        if (state != USB_DEVICE_STATE_SUSPEND) {
            battery_level_requested = false;
        }
        return;
    }

    uint16_t snapshot = battery_level_snapshot();
    if (snapshot == battery_level_reported || timer_elapsed32(settings_activity) < KK_VIA_SAVE_IDLE_MS || !raw_hid_queues_idle()) {
        return;
    }

    uint8_t report[RAW_EPSIZE] = {VIA_UI_SYNC_REQUEST, VIA_UI_SYNC_VERSION, VIA_UI_SYNC_BY_CHANNEL_VALUE, 1, KK_VIA_INDICATORS, KK_VIA_BATTERY_LEVEL};
    raw_hid_send(report, sizeof(report));
    battery_level_reported = snapshot;
}
#        endif

static void save_settings(void) {
    if (!settings_dirty) {
        return;
    }
    uint16_t *magic_addr = (uint16_t *)(SETTINGS_ADDR + offsetof(kk_settings_t, magic));
    settings.checksum    = crc8(&settings, offsetof(kk_settings_t, checksum));
    eeprom_update_word(magic_addr, 0);
    eeprom_update_block(&settings, (void *)SETTINGS_ADDR, offsetof(kk_settings_t, magic));
    eeprom_update_word(magic_addr, SETTINGS_MAGIC);
    settings_dirty = false;
}

static void transfer_u8(bool set, uint8_t *data, uint8_t *value) {
    if (set) {
        settings_dirty |= *value != *data;
        *value = *data;
    } else {
        *data = *value;
    }
}

static void transfer_u16(bool set, uint8_t *data, uint16_t *value, uint16_t min, uint16_t max) {
    if (set) {
        uint16_t requested = MAX(min, MIN(((uint16_t)data[0] << 8) | data[1], max));
        settings_dirty |= *value != requested;
        *value = requested;
    } else {
        data[0] = *value >> 8;
        data[1] = *value;
    }
}

static void transfer_color(bool set, uint8_t *data, kk_indicator_t indicator, uint8_t *h, uint8_t *s) {
    if (set && (*h != data[0] || *s != data[1])) {
        uint16_t mask = 1U << indicator;
        indicator_color_updates |= mask;
        // A HID edit can arrive after rendering, just before the flush.
        rendered_color_updates &= ~mask;
    }
    transfer_u8(set, data, h);
    transfer_u8(set, data + 1, s);
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    if (length == 0) {
        return;
    }
    if (length >= 2 && data[1] >= KK_VIA_GESTURES && data[1] <= KK_VIA_INDICATORS) {
        if (data[0] == id_custom_save) {
            settings_save_requested |= settings_dirty;
            return;
        }
        if (length >= 4 && data[2] && (data[0] == id_custom_get_value || data[0] == id_custom_set_value)) {
            bool    set   = data[0] == id_custom_set_value;
            uint8_t index = data[2] - 1;
            if (data[1] == KK_VIA_GESTURES && index < KK_GESTURE_COUNT && length >= 5) {
                transfer_u16(set, data + 3, &settings.gesture[index], 0, UINT16_MAX);
                return;
            }
            if (data[1] == KK_VIA_BEHAVIOR && length >= 5) {
                switch (data[2]) {
                    case 1:
                    case 2: // Legacy Fade Out ID.
                        transfer_u16(set, data + 3, &settings.behavior_ms[KK_BEHAVIOR_FADE], 0, FADE_MAX_MS);
                        return;
                    case 3:
                        transfer_u16(set, data + 3, &settings.behavior_ms[KK_BEHAVIOR_INDICATOR_FADE], 0, FADE_MAX_MS);
                        return;
                    case 5:
                        transfer_u16(set, data + 3, &settings.behavior_ms[KK_BEHAVIOR_CLICK_WINDOW], 50, 1000);
                        return;
                    case 6:
                        transfer_u16(set, data + 3, &settings.behavior_ms[KK_BEHAVIOR_HOLD_TIME], 100, 2000);
                        return;
                }
            }
            if (data[1] == KK_VIA_INDICATORS) {
                if (!set && data[2] == KK_VIA_BATTERY_LEVEL && length >= 3 + sizeof("100% (Charging)")) {
                    uint16_t snapshot = battery_level_snapshot();
                    write_battery_level(data + 3, snapshot);
#        ifdef RAW_HID_ORDERED_RESPONSES
                    battery_level_reported  = snapshot;
                    battery_level_requested = true;
#        endif
                    return;
                }
                if (length >= 5 && (data[2] == KK_VIA_BATTERY_LOW_COLOR || data[2] == KK_VIA_BATTERY_MEDIUM_COLOR)) {
                    kk_hs_t *color = data[2] == KK_VIA_BATTERY_LOW_COLOR ? &settings.battery_low : &settings.battery_medium;
                    transfer_color(set, data + 3, KK_IND_BATTERY, &color->h, &color->s);
                    return;
                }
                uint8_t indicator = index / KK_VIA_IND_FIELDS;
                if (indicator == LEGACY_BRIGHTNESS_SLOT) {
                    indicator = KK_IND_BRIGHTNESS;
                } else if (indicator > KK_IND_BATTERY) {
                    data[0] = id_unhandled;
                    return;
                }
                uint16_t mask = 1U << indicator;
                hsv_t  *style     = &settings.indicator[indicator];
                switch (index % KK_VIA_IND_FIELDS) {
                    case KK_VIA_IND_BRIGHTNESS:
                        transfer_u8(set, data + 3, &style->v);
                        return;
                    case KK_VIA_IND_COLOR:
                        if (length >= 5) {
                            transfer_color(set, data + 3, indicator, &style->h, &style->s);
                            return;
                        }
                        break;
                    case KK_VIA_IND_ENABLED:
                        if (set) {
                            bool enabled = data[3] != 0;
                            settings_dirty |= enabled != kk_indicator_enabled(indicator);
                            settings.enabled_indicators = (settings.enabled_indicators & ~mask) | (enabled ? mask : 0);
                        } else {
                            data[3] = kk_indicator_enabled(indicator);
                        }
                        return;
                }
            }
        }
    }
    data[0] = id_unhandled;
}

bool via_command_kb(uint8_t *data, uint8_t length) {
    settings_activity = timer_read32();
#        ifdef KK_BRIGHTNESS_INDICATOR_MS
    if (length >= 4 && data[0] == id_custom_set_value && data[1] == id_qmk_rgb_matrix_channel && data[2] == id_qmk_rgb_matrix_brightness) {
        kb_show_brightness_preview();
    }
#        endif
    return false;
}
#    endif

void kk_via_settings_task(void) {
#    if defined(VIA_ENABLE) && defined(RAW_HID_ORDERED_RESPONSES)
    sync_battery_level();
#    endif
    bool pending = false;
#    ifdef RGB_MATRIX_DEFER_EEPROM_WRITE
    if (last_rgb_config.raw != rgb_matrix_config.raw) {
        last_rgb_config   = rgb_matrix_config;
        settings_activity = timer_read32();
    }
    pending |= rgb_matrix_has_pending_eeprom_write();
#    endif
#    ifdef VIA_ENABLE
    pending |= settings_save_requested;
#    endif
    if (!pending || timer_elapsed32(settings_activity) < KK_VIA_SAVE_IDLE_MS || rgb_matrix_dma_is_busy()) {
        return;
    }
#    if defined(VIA_ENABLE) && defined(RAW_HID_ORDERED_RESPONSES)
    if (!raw_hid_queues_idle()) {
        return;
    }
#    endif
#    ifdef RGB_MATRIX_DEFER_EEPROM_WRITE
    rgb_matrix_flush_pending_eeprom();
#    endif
#    ifdef VIA_ENABLE
    if (settings_save_requested) {
        settings_save_requested = false;
        save_settings();
    }
#    endif
}
#endif

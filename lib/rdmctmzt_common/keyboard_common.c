/* Copyright 2025 Carlos Eduardo de Paula <carlosedp@gmail.com>
 * Copyright 2025 EPOMAKER <https://github.com/Epomaker>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "keyboard_common.h"

#ifdef KK_BRIGHTNESS_REPEAT_DELAY_MS
#    include "process_rgb_matrix.h"
#endif

#ifdef KK_VIA_SETTINGS
#    include "kk_via_settings.h"
#else
#    define kk_indicator_color(indicator, ...) (__VA_ARGS__)
#    define kk_indicator_color_flags(indicator) 0
#    define kk_indicator_enabled(indicator) true
#endif

// ============================================================================
// State Variables
// ============================================================================

// Key state tracking
bool Key_Fn_Status = false;

// Battery display state
bool    User_Key_Batt_Num_Show = false;
uint8_t User_Key_Batt_Count    = 0;

#ifdef KK_BRIGHTNESS_INDICATOR_MS
static bool     brightness_indicator_active;
static uint32_t brightness_indicator_started;

void kb_show_brightness_preview(void) {
    if (kk_indicator_enabled(KK_IND_BRIGHTNESS) && rgb_matrix_is_enabled()) {
        brightness_indicator_active  = true;
        brightness_indicator_started = timer_read32();
    }
}
#endif

#ifdef KK_BRIGHTNESS_REPEAT_DELAY_MS
static struct {
    keypos_t key;
    uint16_t keycode;
    uint32_t next_at;
} brightness_repeat;

static void kb_step_brightness(uint16_t keycode) {
    keyrecord_t step = {0};
#    ifdef RGB_TRIGGER_ON_KEYDOWN
    step.event.pressed = true;
#    endif
#    ifdef KK_BRIGHTNESS_INDICATOR_MS
    kb_show_brightness_preview();
#    endif
    process_rgb_matrix(keycode, &step);
}

static void kb_brightness_repeat_task(void) {
    if (!brightness_repeat.keycode) {
        return;
    }

    keypos_t key = brightness_repeat.key;
    if (!matrix_is_on(key.row, key.col) || !rgb_matrix_is_enabled() || rgb_matrix_get_suspend_state() ||
        keymap_key_to_keycode(layer_switch_get_layer(key), key) != brightness_repeat.keycode) {
        brightness_repeat.keycode = 0;
        return;
    }

    uint32_t now = timer_read32();
    if (timer_expired32(now, brightness_repeat.next_at)) {
        kb_step_brightness(brightness_repeat.keycode);
        last_matrix_activity_trigger();
        brightness_repeat.next_at = now + KK_BRIGHTNESS_REPEAT_INTERVAL_MS;
    }
}
#endif

// LED feedback blink counters
uint8_t Led_Point_Count     = 0;
uint8_t Mac_Win_Point_Count = 0;

// LED test mode state
bool    Test_Led    = false;
uint8_t Test_Colour = 0;

// Caps Lock state tracking for wireless modes
static bool local_caps_lock_state = false;

// EEPROM reset visual feedback
static uint8_t  reset_key_led_index = 0;
static uint16_t reset_blink_timer   = 0;
bool            reset_hold_phase    = false;

// ============================================================================
// Matrix Delay Functions (empty implementations for this platform)
// ============================================================================

void matrix_io_delay(void) {}

void matrix_output_select_delay(void) {}

void matrix_output_unselect_delay(uint8_t line, bool key_pressed) {}

// ============================================================================
// Helper Functions
// ============================================================================

bool kb_is_usb_mode(void) {
    return Keyboard_Info.Key_Mode == QMK_USB_MODE;
}

bool kb_get_caps_lock_state(void) {
    if (kb_is_usb_mode()) {
        return host_keyboard_led_state().caps_lock && Usb_If_Ok_Led;
    }
    return local_caps_lock_state;
}

void kb_set_led_color(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
#ifdef KK_INDICATOR_FADE
    kk_indicator_set_color(index, (rgb_t){r, g, b}, 0, 0);
#else
    rgb_matrix_set_color(index, r, g, b);
#endif
}

void kb_led_off(uint8_t index) {
    kb_set_led_color(index, COLOR_OFF);
}

static void kb_draw_indicator(uint8_t index, bool on, uint16_t phase_ms, rgb_t color, uint8_t flags) {
#ifdef KK_INDICATOR_FADE
    if (on) {
        kk_indicator_set_color(index, color, phase_ms, flags);
    } else {
        kk_indicator_clear_request(index);
    }
#else
    (void)phase_ms;
    (void)flags;
    color = on ? color : (rgb_t){COLOR_OFF};
    kb_set_led_color(index, color.r, color.g, color.b);
#endif
}

static bool kb_prepare_mode_indicator(uint8_t index, rgb_t *color, uint8_t *flags) {
#ifdef KK_VIA_SETTINGS
    kk_indicator_t indicator;
    switch (index) {
        case LED_BLE_1_INDEX:
            indicator = KK_IND_BLE1;
            break;
        case LED_BLE_2_INDEX:
            indicator = KK_IND_BLE2;
            break;
        case LED_BLE_3_INDEX:
            indicator = KK_IND_BLE3;
            break;
        case LED_2P4G_INDEX:
            indicator = KK_IND_24G;
            break;
        case LED_USB_INDEX:
            indicator = KK_IND_USB;
            break;
        default:
            return false;
    }
    if (!kk_indicator_enabled(indicator)) {
        return false;
    }
    *color = kk_indicator_color(indicator, *color);
    *flags = kk_indicator_color_flags(indicator);
#else
    (void)index;
    (void)color;
    *flags = 0;
#endif
    return true;
}

static void kb_set_mode_indicator(uint8_t index, rgb_t color) {
    uint8_t flags;
    if (kb_prepare_mode_indicator(index, &color, &flags)) {
        kb_draw_indicator(index, true, 0, color, flags);
    }
}

static void kb_blink_led(uint8_t index, bool on, uint16_t phase_ms, rgb_t color) {
    kb_draw_indicator(index, on, phase_ms, color, 0);
}

static void kb_set_all_indicator_leds(rgb_t color, uint16_t phase_ms) {
#ifdef KK_INDICATOR_FADE
    kk_indicators_fill(color, phase_ms);
#else
    (void)phase_ms;
    rgb_matrix_driver_set_color_all(color.r, color.g, color.b);
#endif
}

void kb_get_battery_color(uint8_t percent, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (percent <= BATTERY_LOW_THRESHOLD) {
        *r = 255;
        *g = 0;
        *b = 0;
    } else if (percent <= BATTERY_MED_THRESHOLD) {
        *r = 255;
        *g = 255;
        *b = 0;
    } else {
        *r = 0;
        *g = 255;
        *b = 0;
    }
}

void kb_switch_to_ble_channel(uint8_t channel, uint8_t spi_command, bool *key_status) {
    if (Keyboard_Info.Key_Mode != QMK_BLE_MODE || Keyboard_Info.Ble_Channel != channel) {
        Keyboard_Info.Key_Mode    = QMK_BLE_MODE;
        Keyboard_Info.Ble_Channel = channel;
        Spi_Send_Commad(spi_command);
        Save_Flash_Set();
        Led_Rf_Pair_Flg      = true;
        Show_Mode_Indicator  = true;
        Mode_Indicator_Timer = timer_read();
    }
    *key_status = true;
}

// ============================================================================
// LED Display Functions
// ============================================================================

void kb_led_rf_mode_show(void) {
    uint8_t Temp_Colour = 0, Led_Index = 0;
    if (Keyboard_Info.Key_Mode == QMK_BLE_MODE) {
        if (Keyboard_Info.Ble_Channel == QMK_BLE_CHANNEL_1) {
            Temp_Colour = 5;
            Led_Index   = LED_BLE_1_INDEX;
        } else if (Keyboard_Info.Ble_Channel == QMK_BLE_CHANNEL_2) {
            Temp_Colour = 5;
            Led_Index   = LED_BLE_2_INDEX;
        } else if (Keyboard_Info.Ble_Channel == QMK_BLE_CHANNEL_3) {
            Temp_Colour = 5;
            Led_Index   = LED_BLE_3_INDEX;
        }
    } else if (Keyboard_Info.Key_Mode == QMK_2P4G_MODE) {
        Temp_Colour = 3;
        Led_Index   = LED_2P4G_INDEX;
    }

    rgb_t   color = {Led_Colour_Tab[Temp_Colour][0], Led_Colour_Tab[Temp_Colour][1], Led_Colour_Tab[Temp_Colour][2]};
    uint8_t flags;
    bool    show = kb_prepare_mode_indicator(Led_Index, &color, &flags);
    if (Keyboard_Status.System_Connect_Status == KB_MODE_CONNECT_PAIR) {
        if (show) {
            kb_draw_indicator(Led_Index, Systick_Led_Count < 10, 100, color, flags);
        }

        if (Systick_Led_Count >= 20) {
            Systick_Led_Count = 0;
        }
    } else if (Keyboard_Status.System_Connect_Status == KB_MODE_CONNECT_RETURN) {
        if (show) {
            kb_draw_indicator(Led_Index, Systick_Led_Count < 25, 250, color, flags);
        }

        if (Systick_Led_Count >= 50) {
            Systick_Led_Count = 0;
        }
    } else {
        if (show) {
            kb_draw_indicator(Led_Index, true, 0, color, flags);
        }

        if (Systick_Led_Count >= 240) {
            Systick_Led_Count = 0;
            Led_Rf_Pair_Flg   = false;
            if (Keyboard_Info.Key_Mode == QMK_BLE_MODE) {
                User_Batt_Send_Spi = true;
            }
        }
    }
}

void kb_led_power_low_show(void) {
#ifdef KK_INDICATOR_FADE
    kb_set_all_indicator_leds((rgb_t){COLOR_OFF}, 0);
#else
    for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; i++) {
        rgb_matrix_set_color(i, 0, 0, 0);
    }
#endif

#ifdef KK_GALAXY100_LITE
    kb_blink_led(LED_BATT_INDEX, Systick_Led_Count < 25, 250, (rgb_t){COLOR_RED});
#else
    kb_blink_led(LED_BATT_INDEX, Systick_Led_Count < 25, 250, (rgb_t){U_PWM, 0, 0});
#endif

    if (Systick_Led_Count >= 50) {
        Systick_Led_Count = 0;
    }
}

void kb_led_point_flash_show(void) {
    bool on = Systick_Led_Count < 25;
    if (Led_Point_Count) {
        kb_set_all_indicator_leds(on ? (rgb_t){COLOR_WHITE} : (rgb_t){COLOR_OFF}, 250);
    } else if (Mac_Win_Point_Count) {
        kb_blink_led(LED_CAP_INDEX, on, 250, (rgb_t){U_PWM, U_PWM, 0});
    }

    if (Systick_Led_Count >= 50) {
        Systick_Led_Count = 0;

        if (Led_Point_Count) {
            Led_Point_Count--;
        } else if (Mac_Win_Point_Count) {
            Mac_Win_Point_Count--;
        }
    }
}

static void kb_show_number_row_level(uint8_t count, rgb_t filled, rgb_t empty, uint8_t flags) {
    for (uint8_t i = 0; i < BATTERY_LED_COUNT; i++) {
        rgb_t color = i < count ? filled : empty;
        kb_draw_indicator(Led_Batt_Index_Tab[i], true, 0, color, flags);
    }
}

static void kb_show_battery_level(bool clear_unused) {
    uint8_t count = MIN((Keyboard_Info.Batt_Number * BATTERY_LED_COUNT) / 100, BATTERY_LED_COUNT);
#ifdef KK_VIA_SETTINGS
    rgb_t color = kk_battery_color(Keyboard_Info.Batt_Number);
#else
    rgb_t   color;
    kb_get_battery_color(Keyboard_Info.Batt_Number, &color.r, &color.g, &color.b);
#endif
    if (clear_unused) {
        kb_show_number_row_level(count, color, (rgb_t){COLOR_OFF}, kk_indicator_color_flags(KK_IND_BATTERY));
    } else {
        for (uint8_t i = 0; i < count; i++) {
            kb_set_led_color(Led_Batt_Index_Tab[i], color.r, color.g, color.b);
        }
    }
}

#ifdef KK_BRIGHTNESS_INDICATOR_MS
static void kb_show_brightness_level(void) {
    uint8_t value = rgb_matrix_get_val();
    uint8_t count = (value * BATTERY_LED_COUNT) / RGB_MATRIX_MAXIMUM_BRIGHTNESS;
    // Keep the first segment visible at any nonzero brightness.
    rgb_t track = {U_PWM / 16, U_PWM / 16, U_PWM / 16};
    kb_show_number_row_level(MAX(count, value != 0), kk_indicator_color(KK_IND_BRIGHTNESS, (rgb_t){COLOR_WHITE}), kk_indicator_color(KK_IND_BRIGHTNESS, track),
                             kk_indicator_color_flags(KK_IND_BRIGHTNESS));
}
#endif

void kb_led_batt_number_show(void) {
    if (es_stdby_pin_state == 1) {
        // Charging animation - wave effect
        if (Batt_Led_Count >= 2) {
            Batt_Led_Count      = 0;
            User_Key_Batt_Count = (User_Key_Batt_Count > 3) ? (User_Key_Batt_Count - 3) : 127;
        }

        uint8_t wave_offset = User_Key_Batt_Count;
#ifdef KK_INDICATOR_FADE
        uint8_t flags = KK_INDICATOR_ANIMATE | kk_indicator_color_flags(KK_IND_BATTERY);
#endif
        for (uint8_t i = 0; i < BATTERY_LED_COUNT; i++) {
            uint8_t brightness = led_wave_pwm(wave_offset);
#ifdef KK_INDICATOR_FADE
            rgb_t color = kk_indicator_color(KK_IND_BATTERY, (rgb_t){0, brightness, 0});
            kk_indicator_set_color(Led_Batt_Index_Tab[i], color, 0, flags);
#else
            kb_set_led_color(Led_Batt_Index_Tab[i], 0, brightness, 0);
#endif
            wave_offset = (wave_offset + 8) % 128;
        }
    } else if (es_stdby_pin_state == 2) {
        // Fully charged - solid green
        kb_show_number_row_level(BATTERY_LED_COUNT, kk_indicator_color(KK_IND_BATTERY, (rgb_t){COLOR_GREEN}), (rgb_t){COLOR_OFF}, kk_indicator_color_flags(KK_IND_BATTERY));
    } else {
#ifdef KK_GALAXY100_LITE
        kb_show_battery_level(true);
#else
        kb_show_battery_level(false);
#endif
    }
}

void kb_user_point_show(void) {
    if (Led_Point_Count || Mac_Win_Point_Count) {
        kb_led_point_flash_show();
        return;
    }

    Systick_Led_Count = 0;

    kb_update_state_indicators();
}

void kb_user_test_colour_show(void) {
    uint8_t Test_R = 0, Test_G = 0, Test_B = 0;
    switch (Test_Colour) {
        case 0:
            Test_R = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            Test_G = 0;
            Test_B = 0;
            break;
        case 1:
            Test_R = 0;
            Test_G = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            Test_B = 0;
            break;
        case 2:
            Test_R = 0;
            Test_G = 0;
            Test_B = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            break;
        case 3:
            Test_R = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            Test_G = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            Test_B = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            break;
        default:
            Test_R = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            Test_G = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            Test_B = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
            break;
    }
    kb_set_all_indicator_leds((rgb_t){Test_R, Test_G, Test_B}, 0);
}

void kb_update_connection_indicator(void) {
    switch (Keyboard_Info.Key_Mode) {
        case QMK_BLE_MODE:
            kb_set_led_color(LED_CONNECTION_INDEX, COLOR_BLUE);
            break;
        case QMK_2P4G_MODE:
            kb_set_led_color(LED_CONNECTION_INDEX, COLOR_GREEN);
            break;
        case QMK_USB_MODE:
            kb_set_led_color(LED_CONNECTION_INDEX, COLOR_WHITE);
            break;
        default:
            kb_led_off(LED_CONNECTION_INDEX);
            break;
    }
}

void kb_update_state_indicators(void) {
    // Only override LED color when state is active
    // When inactive, let RGB Matrix handle the normal color
    if (kk_indicator_enabled(KK_IND_CAPS) && kb_get_caps_lock_state()) {
        kb_draw_indicator(LED_CAP_INDEX, true, 0, kk_indicator_color(KK_IND_CAPS, (rgb_t){COLOR_WHITE}), kk_indicator_color_flags(KK_IND_CAPS));
    }

#if defined(KK_INDICATOR_FADE) && defined(LED_NUM_INDEX)
    if (kk_indicator_enabled(KK_IND_NUM) && (!kb_is_usb_mode() || Usb_If_Ok_Led) && host_keyboard_led_state().num_lock) {
        kb_draw_indicator(LED_NUM_INDEX, true, 0, kk_indicator_color(KK_IND_NUM, (rgb_t){COLOR_WHITE}), kk_indicator_color_flags(KK_IND_NUM));
    }
#endif

    if (kk_indicator_enabled(KK_IND_WIN) && Keyboard_Info.Win_Lock) {
        kb_draw_indicator(LED_WIN_L_INDEX, true, 0, kk_indicator_color(KK_IND_WIN, (rgb_t){COLOR_WHITE}), kk_indicator_color_flags(KK_IND_WIN));
    }
}

void kb_show_current_connection_mode(void) {
    // Highlight the current connection mode key when Fn is held
    switch (Keyboard_Info.Key_Mode) {
        case QMK_BLE_MODE:
            switch (Keyboard_Info.Ble_Channel) {
                case QMK_BLE_CHANNEL_1:
                    kb_set_mode_indicator(LED_BLE_1_INDEX, (rgb_t){COLOR_BLUE});
                    break;
                case QMK_BLE_CHANNEL_2:
                    kb_set_mode_indicator(LED_BLE_2_INDEX, (rgb_t){COLOR_BLUE});
                    break;
                case QMK_BLE_CHANNEL_3:
                    kb_set_mode_indicator(LED_BLE_3_INDEX, (rgb_t){COLOR_BLUE});
                    break;
                default:
                    break;
            }
            break;
        case QMK_2P4G_MODE:
            kb_set_mode_indicator(LED_2P4G_INDEX, (rgb_t){COLOR_GREEN});
            break;
        case QMK_USB_MODE:
            kb_set_mode_indicator(LED_USB_INDEX, (rgb_t){COLOR_WHITE});
            break;
        default:
            break;
    }
}

void kb_toggle_all_leds(void) {
    bool enabled = !rgb_matrix_is_enabled();

    if (enabled) {
        rgb_matrix_enable();
    } else {
        rgb_matrix_disable();
    }

#if LOGO_LED_ENABLE
    Keyboard_Info.Logo_On_Off = enabled;
    Logo_Led_Update();
#endif
#if SIDE_LED_ENABLE
    Keyboard_Info.Side_On_Off = enabled;
    Side_Led_Update();
#endif

    Save_Flash_Set();
}

void kb_show_reset_progress(void) {
    rgb_t color = {COLOR_RED};
    if (reset_hold_phase) {
        // After 3 seconds of blinking, hold solid red for 1 second before reset
        kb_draw_indicator(reset_key_led_index, true, 0, color, 0);
    } else {
        // Blink the reset key LED red every 250ms while EE_CLR is held
        if (timer_elapsed(reset_blink_timer) > 250) {
            reset_blink_timer = timer_read();
        }

        // Blink: on for first 125ms, off for next 125ms
        kb_blink_led(reset_key_led_index, timer_elapsed(reset_blink_timer) < 125, 125, color);
    }
}

// ============================================================================
// QMK Callback Functions - Common implementations
// ============================================================================

bool kb_rgb_matrix_indicators_common(uint8_t led_min, uint8_t led_max) {
#ifndef KK_INDICATOR_FADE
    // Show EEPROM reset progress (blinking) when EE_CLR is held
    if (Key_Reset_Status) {
        kb_show_reset_progress();
    }
#endif

#if LED_CONNECTION_INDICATOR_ENABLE
    kb_update_connection_indicator();
#endif
    kb_update_state_indicators();

#ifdef KK_GALAXY100_LITE
    bool show_battery = User_Key_Batt_Num_Show && kk_indicator_enabled(KK_IND_BATTERY);
    if (Key_Fn_Status || show_battery || Show_Mode_Indicator) {
        kb_show_current_connection_mode();
    }

#ifdef KK_BRIGHTNESS_INDICATOR_MS
    if (brightness_indicator_active) {
        brightness_indicator_active = kk_indicator_enabled(KK_IND_BRIGHTNESS) && rgb_matrix_is_enabled() && timer_elapsed32(brightness_indicator_started) < KK_BRIGHTNESS_INDICATOR_MS;
    }
#endif

    if (User_Power_Low) {
        kb_led_power_low_show();
    } else if (Test_Led) {
        kb_user_test_colour_show();
    } else {
        bool show_radio = Led_Rf_Pair_Flg && !kb_is_usb_mode();
        if (show_battery) {
            if (show_radio) {
                kb_show_battery_level(true);
            } else {
                kb_led_batt_number_show();
            }
#ifdef KK_BRIGHTNESS_INDICATOR_MS
        } else if (brightness_indicator_active) {
            kb_show_brightness_level();
#endif
        } else if (!show_radio) {
            kb_user_point_show();
        }
        if (show_radio) {
            kb_led_rf_mode_show();
        }
    }

#ifdef KK_INDICATOR_FADE
    // Show reset feedback over other indicators on the same key.
    if (Key_Reset_Status && !User_Power_Low && !Test_Led) {
        kb_show_reset_progress();
    }
#endif
#else
    if (Key_Fn_Status) {
        kb_show_current_connection_mode();
    }
    if (User_Key_Batt_Num_Show || Show_Mode_Indicator) {
        kb_show_current_connection_mode();
        if (User_Key_Batt_Num_Show) {
            kb_show_battery_level(true);
        }
    }

    if (User_Power_Low) {
        kb_led_power_low_show();
    } else if (Test_Led) {
        kb_user_test_colour_show();
    } else if (Led_Rf_Pair_Flg && (Keyboard_Info.Key_Mode != QMK_USB_MODE)) {
        kb_led_rf_mode_show();
    } else if (User_Key_Batt_Num_Show) {
        kb_led_batt_number_show();
    } else {
        kb_user_point_show();
    }
#endif

    return false;
}

void kb_notify_usb_device_state_change(struct usb_device_state usb_device_state) {
    if (Keyboard_Info.Key_Mode == QMK_USB_MODE) {
        if (usb_device_state.configure_state == USB_DEVICE_STATE_CONFIGURED) {
            Usb_If_Ok_Led = true;
        } else {
            Usb_If_Ok_Led = false;
        }
    } else {
        Usb_If_Ok_Led = false;
    }
}

bool kb_led_update(led_t led_state) {
    // Track Caps Lock state locally for all modes
    local_caps_lock_state = led_state.caps_lock;

    // In wireless modes, we need to sync the LED state with the wireless module
    if (Keyboard_Info.Key_Mode != QMK_USB_MODE) {
        // Update the internal LED status for wireless modes
        // Bit 1 (0x02) represents Caps Lock status
        if (led_state.caps_lock) {
            Keyboard_Status.System_Led_Status |= 0x02; // Set Caps Lock bit
        } else {
            Keyboard_Status.System_Led_Status &= ~0x02; // Clear Caps Lock bit
        }
    }

    // Force immediate LED update by triggering a refresh
    // This ensures our changes take effect immediately
    rgb_matrix_set_flags(LED_FLAG_ALL);

    return true;
}

// When RGB Matrix is disabled (RM_TOGG), QMK renders the "none" effect, skips
// the indicator callbacks and stops flushing the LED driver after one frame.
// Draw the indicators ourselves and flush periodically, so the connection,
// Caps Lock and battery indicators keep working and the logo/side zone
// animations keep running.
static void kb_render_leds_while_matrix_off(void) {
    static uint16_t last_flush = 0;

    if (rgb_matrix_is_enabled() || rgb_matrix_get_suspend_state()) {
        return;
    }
    if (timer_elapsed(last_flush) < RGB_MATRIX_LED_FLUSH_LIMIT) {
        return;
    }
    last_flush = timer_read();

    rgb_matrix_set_color_all(COLOR_OFF);
    rgb_matrix_indicators_advanced_kb(0, RGB_MATRIX_LED_COUNT);
#if LOGO_LED_ENABLE
    Logo_Led_Update();
#endif
#if SIDE_LED_ENABLE
    Side_Led_Update();
#endif
    rgb_matrix_update_pwm_buffers();
}

void kb_housekeeping_task(void) {
    if (Show_Mode_Indicator && timer_elapsed(Mode_Indicator_Timer) > MODE_INDICATOR_TIMEOUT) {
        Show_Mode_Indicator = false;
    }

    Usb_Auto_Switch_Task();

#ifdef KK_BRIGHTNESS_REPEAT_DELAY_MS
    kb_brightness_repeat_task();
#endif

#if LOGO_LED_ENABLE
    // Update logo LEDs independently of RGB matrix state
    // This allows logo LEDs to work even when per-key RGB is disabled
    Logo_Led_Update();
#endif
#if SIDE_LED_ENABLE
    Side_Led_Update();
#endif

    kb_render_leds_while_matrix_off();

    // Handle EEPROM reset request
    if (Keyboard_Reset) {
        Keyboard_Reset = false;

        // Reset custom keyboard settings to defaults
        Keyboard_Info.Key_Mode     = INIT_WORK_MODE;
        Keyboard_Info.Ble_Channel  = INIT_BLE_CHANNEL;
        Keyboard_Info.Batt_Number  = INIT_BATT_NUMBER;
        Keyboard_Info.Nkro         = INIT_ALL_KEY;
        Keyboard_Info.Mac_Win_Mode = INIT_WIN_MODE;
        Keyboard_Info.Win_Lock     = INIT_WIN_NLOCK;
#if LOGO_LED_ENABLE
        Keyboard_Info.Logo_On_Off     = 1;
        Keyboard_Info.Logo_Mode       = LOGO_MODE_FIXED_WAVE; // Fixed wave, default color #8610FF
        Keyboard_Info.Logo_Hue        = 191;
        Keyboard_Info.Logo_Saturation = 239;
        Keyboard_Info.Logo_Brightness = 180;
        Keyboard_Info.Logo_Speed      = 2;
#endif
#if SIDE_LED_ENABLE
        Keyboard_Info.Side_On_Off     = 1;
        Keyboard_Info.Side_Mode       = SIDE_MODE_LIGHT; // Light (solid), default color #8610FF
        Keyboard_Info.Side_Hue        = 191;
        Keyboard_Info.Side_Saturation = 239;
        Keyboard_Info.Side_Brightness = 180;
        Keyboard_Info.Side_Speed      = 2;
#endif
        // Save the reset keyboard info to EEPROM
        eeprom_write_block_user((void *)&Keyboard_Info.Key_Mode, 0, sizeof(Keyboard_Info_t));

        // Clear and reinitialize QMK's EEPROM (RGB, keymap, etc.)
        eeconfig_init();

        // Reset the keyboard
        soft_reset_keyboard();
    }

    es_chibios_user_idle_loop_hook();
}

void kb_board_init(void) {
    es_ble_spi_init();          // SPI initialization
    User_Adc_Init();            // ADC initialization
    eeprom_driver_init();       // EEPROM initialization
    rgb_matrix_driver_init();   // PWM DMA initialization
    Init_Gpio_Infomation();     // GPIO initialization
    Init_Keyboard_Infomation(); // Keyboard info initialization
    Init_Batt_Infomation();     // Battery info initialization
    User_Systime_Init();        // System timer initialization

    if (Keyboard_Info.Key_Mode == QMK_USB_MODE) {
        User_Usb_Init();
        Led_Rf_Pair_Flg = false;
    } else {
        Usb_Disconnect();
    }

    Init_Spi_Power_Up    = true;
    Init_Spi_100ms_Delay = 0;
    Spi_Interval         = SPI_DELAY_RF_TIME;
    NVIC_SetPriority(PendSV_IRQn, 3);
    NVIC_SetPriority(SysTick_IRQn, 3);

    Usb_If_Ok_Led  = false;
    Led_Power_Up   = true;
    Emi_Test_Start = false;
    Keyboard_Reset = false;
}

void kb_keyboard_post_init(void) {
    if (keymap_config.nkro != Keyboard_Info.Nkro) {
        keymap_config.nkro = Keyboard_Info.Nkro;
    }

    if (Keyboard_Info.Mac_Win_Mode == INIT_MAC_MODE) {
        layer_on(1);
    }
}

// ============================================================================
// Keycode Processing
// ============================================================================

bool kb_process_record_common(uint16_t keycode, keyrecord_t *record) {
    Usb_Change_Mode_Delay  = 0;
    Usb_Change_Mode_Wakeup = false;

    // Exit test mode on any key except allowed keys
    if (Test_Led && keycode != KC_SPC && keycode != MO(2) && keycode != MO(3) && keycode != KC_LCTL) {
        Test_Led = false;
    }

    switch (keycode) {
        case QMK_KB_MODE_2P4G: { // 2.4G
            if (record->event.pressed) {
                Key_2p4g_Status = true;
                Usb_Disconnect();
                if (Keyboard_Info.Key_Mode != QMK_2P4G_MODE) {
                    Keyboard_Info.Key_Mode = QMK_2P4G_MODE;
                    Spi_Send_Commad(USER_SWITCH_2P4G_MODE);
                    Save_Flash_Set();
                    Led_Rf_Pair_Flg = true;
                    // Show mode indicator for 1 second
                    Show_Mode_Indicator  = true;
                    Mode_Indicator_Timer = timer_read();
                }
            } else {
                Key_2p4g_Status = false;
            }
            Time_3s_Count = 0;
        }
            return true;
        case QMK_KB_MODE_BLE1:
            if (record->event.pressed) {
                Usb_Disconnect();
                kb_switch_to_ble_channel(QMK_BLE_CHANNEL_1, USER_SWITCH_BLE_1_MODE, &Key_Ble_1_Status);
            } else {
                Key_Ble_1_Status = false;
            }
            Time_3s_Count = 0;
            return true;

        case QMK_KB_MODE_BLE2:
            if (record->event.pressed) {
                Usb_Disconnect();
                kb_switch_to_ble_channel(QMK_BLE_CHANNEL_2, USER_SWITCH_BLE_2_MODE, &Key_Ble_2_Status);
            } else {
                Key_Ble_2_Status = false;
            }
            Time_3s_Count = 0;
            return true;

        case QMK_KB_MODE_BLE3:
            if (record->event.pressed) {
                Usb_Disconnect();
                kb_switch_to_ble_channel(QMK_BLE_CHANNEL_3, USER_SWITCH_BLE_3_MODE, &Key_Ble_3_Status);
            } else {
                Key_Ble_3_Status = false;
            }
            Time_3s_Count = 0;
            return true;
        case QMK_KB_MODE_USB: {
            if (record->event.pressed) {
                if (Keyboard_Info.Key_Mode != QMK_USB_MODE) {
                    Keyboard_Info.Key_Mode = QMK_USB_MODE;
                    Spi_Send_Commad(USER_SWITCH_USB_MODE);
                    es_restart_usb_driver();
                    Save_Flash_Set();
                    Led_Rf_Pair_Flg = false;
                    // Show mode indicator for 1 second
                    Show_Mode_Indicator  = true;
                    Mode_Indicator_Timer = timer_read();
                }
            }
        }
            return true;
        case QMK_BATT_NUM: {
            if (record->event.pressed) {
                User_Key_Batt_Num_Show = true;
                User_Key_Batt_Count    = 0;
            } else {
                User_Key_Batt_Num_Show = false;
                User_Key_Batt_Count    = 0;
            }
        }
            return true;
        case QMK_WIN_LOCK: {
            if (!record->event.pressed) {
                if (Keyboard_Info.Mac_Win_Mode == INIT_MAC_MODE) {
                    if (Keyboard_Info.Win_Lock == INIT_WIN_LOCK) {
                        Keyboard_Info.Win_Lock = INIT_WIN_NLOCK;
                        Save_Flash_Set();
                    }
                } else {
                    if (Keyboard_Info.Win_Lock == INIT_WIN_NLOCK) {
                        Keyboard_Info.Win_Lock = INIT_WIN_LOCK;
                        unregister_code(KC_LGUI);
                        unregister_code(KC_RGUI);
                        unregister_code(KC_APP);
                    } else {
                        Keyboard_Info.Win_Lock = INIT_WIN_NLOCK;
                    }
                    Save_Flash_Set();
                }
            }
        }
            return true;
        case QMK_KB_SIX_N_CH: {
            if (record->event.pressed) {
                if (keymap_config.nkro) {
                    es_change_qmk_nkro_mode_disable();
                    Mac_Win_Point_Count = 3;
                } else {
                    es_change_qmk_nkro_mode_enable();
                    Led_Point_Count = 3;
                }
            }
        }
            return true;
        case QMK_RGB_TOGGLE:
            if (!record->event.pressed) {
                kb_toggle_all_leds();
            }
            return true;
        case QMK_TEST_COLOUR: {
            if (!record->event.pressed) {
                if (Test_Led == false) {
                    Test_Led    = true;
                    Test_Colour = 0;
                }
            }
        }
            return true;
        case KC_SPC: {
            if (!record->event.pressed) {
                if (Test_Led) {
                    Test_Colour++;
                    if (Test_Colour >= 4) {
                        Test_Colour = 0;
                    }
                }
            }
        }
            return true;
        case KC_LGUI: { // key_win_l
            if (Keyboard_Info.Win_Lock) {
                record->event.pressed = false;
            }
        }
            return true;
        case KC_RGUI: { // key_win_r
            if (Keyboard_Info.Win_Lock) {
                record->event.pressed = false;
            }
        }
            return true;
        case KC_APP: { // key_app
            if (Keyboard_Info.Win_Lock) {
                record->event.pressed = false;
            }
        }
            return true;
        case RM_VALU:
        case RM_VALD:
#ifdef KK_BRIGHTNESS_REPEAT_DELAY_MS
            if (IS_KEYEVENT(record->event)) {
                if (record->event.pressed) {
                    brightness_repeat.key     = record->event.key;
                    brightness_repeat.keycode = keycode;
                    brightness_repeat.next_at = timer_read32() + KK_BRIGHTNESS_REPEAT_DELAY_MS;
                    kb_step_brightness(keycode);
                } else if (KEYEQ(brightness_repeat.key, record->event.key)) {
                    brightness_repeat.keycode = 0;
                }
                return false;
            }
#endif
#ifdef KK_BRIGHTNESS_INDICATOR_MS
#    ifdef RGB_TRIGGER_ON_KEYDOWN
            if (record->event.pressed) {
#    else
            if (!record->event.pressed) {
#    endif
                kb_show_brightness_preview();
            }
#else
            if (!record->event.pressed && (keycode == RM_VALU ? rgb_matrix_get_val() >= RGB_MATRIX_MAXIMUM_BRIGHTNESS : rgb_matrix_get_val() == 0)) {
                Led_Point_Count = 3;
            }
#endif
            return true;
        case RM_SPDU: {
            if (!record->event.pressed) {
                if (rgb_matrix_get_speed() >= 255) {
                    Led_Point_Count = 3;
                }
            }
        }
            return true;
        case RM_SPDD: {
            if (!record->event.pressed) {
                if (rgb_matrix_get_speed() <= 0) {
                    Led_Point_Count = 3;
                }
            }
        }
            return true;
        case MO(2): { // FN
            if (record->event.pressed) {
                Key_Fn_Status = true;
            } else {
                Key_Fn_Status = false;
            }
        }
            return true;
        case MO(3): { // FN
            if (record->event.pressed) {
                Key_Fn_Status = true;
            } else {
                Key_Fn_Status = false;
            }
        }
            return true;
        case EE_CLR: {
            if (record->event.pressed) {
                Key_Reset_Status      = true;
                record->event.pressed = false;
                // Get the LED index for this key position and start blink timer
                reset_key_led_index = g_led_config.matrix_co[record->event.key.row][record->event.key.col];
                reset_blink_timer   = timer_read();
                reset_hold_phase    = false;
            } else {
                Key_Reset_Status = false;
                reset_hold_phase = false;
            }
            Func_Time_3s_Count = 0;
        }
            return true;
        case QMK_DEBUG_SWITCH: {
            if (record->event.pressed) {
                Debug_Mode_Switch_Position();
            }
        }
            return true;
        case QMK_MAC_WIN_CH:
            if (!record->event.pressed) {
                // Release modifier keys to prevent stuck keys
                unregister_code(KC_LALT);
                unregister_code(KC_LGUI);
                unregister_code(KC_RALT);
                unregister_code(KC_RGUI);
                unregister_code(KC_APP);

                if (Keyboard_Info.Mac_Win_Mode == INIT_WIN_MODE) {
                    Keyboard_Info.Mac_Win_Mode = INIT_MAC_MODE;
                    Keyboard_Info.Win_Lock     = INIT_WIN_NLOCK;
                    Mac_Win_Point_Count        = 3;
                    layer_on(1);
                } else {
                    Keyboard_Info.Mac_Win_Mode = INIT_WIN_MODE;
                    Mac_Win_Point_Count        = 1;
                    layer_off(1);
                }
                Save_Flash_Set();
            }
            return true;

        default:
            return true;
    }
}

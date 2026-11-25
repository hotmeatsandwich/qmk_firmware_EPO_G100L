// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "color.h"

typedef enum {
    KK_GESTURE_HOLD,
    KK_GESTURE_DOUBLE_CLICK,
    KK_GESTURE_TRIPLE_CLICK,
    KK_GESTURE_HOLD_CCW,
    KK_GESTURE_HOLD_CW,
    KK_GESTURE_DOUBLE_HOLD_CCW,
    KK_GESTURE_DOUBLE_HOLD_CW,
    KK_GESTURE_COUNT,
} kk_gesture_t;

typedef enum {
    KK_BEHAVIOR_FADE,
    KK_BEHAVIOR_INDICATOR_FADE,
    // Keep slot 2 reserved to preserve the layout of saved settings.
    KK_BEHAVIOR_CLICK_WINDOW = 3,
    KK_BEHAVIOR_HOLD_TIME,
    KK_BEHAVIOR_COUNT,
} kk_behavior_t;

typedef enum {
    KK_IND_CAPS,
    KK_IND_NUM,
    KK_IND_WIN,
    KK_IND_BLE1,
    KK_IND_BLE2,
    KK_IND_BLE3,
    KK_IND_24G,
    KK_IND_USB,
    KK_IND_BATTERY,
    KK_IND_BRIGHTNESS,
    KK_IND_COUNT,
} kk_indicator_t;

void kk_via_settings_load(void);
void kk_via_settings_task(void);
uint16_t kk_gesture_keycode(kk_gesture_t gesture);
uint16_t kk_behavior_duration(kk_behavior_t behavior);
bool kk_indicator_enabled(kk_indicator_t indicator);
rgb_t kk_indicator_color(kk_indicator_t indicator, rgb_t color);
rgb_t kk_battery_color(uint8_t percent);
uint8_t kk_indicator_color_flags(kk_indicator_t indicator);
void kk_indicator_begin_color_frame(void);
void kk_indicator_end_color_frame(void);

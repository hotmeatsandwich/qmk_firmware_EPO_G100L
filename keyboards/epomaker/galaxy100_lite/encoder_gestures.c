// SPDX-License-Identifier: GPL-2.0-or-later

#include "encoder_gestures.h"
#include "keyboard_common.h"
#include "kk_via_settings.h"

#ifdef KK_GALAXY100_LITE
typedef enum {
    ENCODER_IDLE,
    ENCODER_PRESSED,
    ENCODER_AWAITING_CLICK,
    ENCODER_TURNING,
    ENCODER_CANCELED,
} encoder_gesture_state_t;

static encoder_gesture_state_t state;
static uint32_t                button_changed_at;
static uint16_t                connection;
static uint16_t                button_keycode;
static uint16_t                turn_keycode;
static uint8_t                 clicks;

static uint16_t encoder_connection(void) {
    return ((uint16_t)Keyboard_Info.Key_Mode << 8) | (Keyboard_Info.Key_Mode == QMK_BLE_MODE ? Keyboard_Info.Ble_Channel : 0);
}

bool kk_encoder_gesture_button(keypos_t key) {
    return key.row == 5 && key.col == 3;
}

void kk_encoder_gestures_reset(void) {
    state  = ENCODER_IDLE;
    clicks = 0;
}

static void send_keycode(uint16_t keycode, bool pressed) {
    if (keycode <= KC_TRNS) {
        return;
    }
    keyrecord_t record = {
        .event   = MAKE_COMBOEVENT(pressed),
        .keycode = keycode,
#ifndef NO_ACTION_TAPPING
        .tap.count = 1,
#endif
    };
    record.event.key = MAKE_KEYPOS(5, 3);
    process_record(&record);
}

static void finish_button(uint16_t keycode) {
    if (keycode == KC_NO) {
        keycode = button_keycode;
    }
    kk_encoder_gestures_reset();
    send_keycode(keycode, true);
#if TAP_CODE_DELAY > 0
    wait_ms(TAP_CODE_DELAY);
#endif
    send_keycode(keycode, false);
}

static void finish_clicks(void) {
    uint16_t keycode = KC_NO;
    if (clicks >= 2) {
        keycode = kk_gesture_keycode(clicks == 2 ? KK_GESTURE_DOUBLE_CLICK : KK_GESTURE_TRIPLE_CLICK);
    }
    finish_button(keycode);
}

void kk_encoder_gestures_task(void) {
    if (state == ENCODER_IDLE || state == ENCODER_CANCELED) {
        return;
    }
    if (connection != encoder_connection()) {
        state  = state == ENCODER_AWAITING_CLICK ? ENCODER_IDLE : ENCODER_CANCELED;
        clicks = 0;
    } else if (state == ENCODER_AWAITING_CLICK && timer_elapsed32(button_changed_at) >= kk_behavior_duration(KK_BEHAVIOR_CLICK_WINDOW)) {
        finish_clicks();
    }
}

static bool handle_turn(keyevent_t event) {
    state = ENCODER_TURNING;
    if (event.pressed) {
        kk_gesture_t counterclockwise = clicks == 1 ? KK_GESTURE_DOUBLE_HOLD_CCW : KK_GESTURE_HOLD_CCW;
        turn_keycode                  = kk_gesture_keycode(counterclockwise + (event.type == ENCODER_CW_EVENT));
    }
    if (turn_keycode == KC_NO) {
        return true;
    }
    send_keycode(turn_keycode, event.pressed);
    return false;
}

bool kk_encoder_gestures_process_record(uint16_t keycode, keyrecord_t *record) {
    keyevent_t event = record->event;
    if (IS_ENCODEREVENT(event) && event.key.col == 0) {
        kk_encoder_gestures_task();
        if (state == ENCODER_PRESSED || state == ENCODER_TURNING) {
            return handle_turn(event);
        }
        return true;
    }
    if (!IS_KEYEVENT(event) || !kk_encoder_gesture_button(event.key)) {
        return true;
    }

    kk_encoder_gestures_task();
    if (!event.pressed && state != ENCODER_PRESSED && state != ENCODER_TURNING && state != ENCODER_CANCELED) {
        return true;
    }
    kb_process_record_common(KC_NO, record);

    if (event.pressed) {
        if (state == ENCODER_IDLE) {
            button_keycode = keycode;
        }
        state             = ENCODER_PRESSED;
        connection        = encoder_connection();
        button_changed_at = timer_read32();
    } else if (state == ENCODER_TURNING || state == ENCODER_CANCELED) {
        kk_encoder_gestures_reset();
    } else if (!clicks && timer_elapsed32(button_changed_at) >= kk_behavior_duration(KK_BEHAVIOR_HOLD_TIME)) {
        // Commit on release so a slow press-and-turn can still replace the hold.
        finish_button(kk_gesture_keycode(KK_GESTURE_HOLD));
    } else if (++clicks == 3) {
        finish_clicks();
    } else {
        state             = ENCODER_AWAITING_CLICK;
        button_changed_at = timer_read32();
    }
    return false;
}
#endif

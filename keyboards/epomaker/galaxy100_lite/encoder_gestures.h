// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "action.h"

bool kk_encoder_gesture_button(keypos_t key);
bool kk_encoder_gestures_process_record(uint16_t keycode, keyrecord_t *record);
void kk_encoder_gestures_task(void);
void kk_encoder_gestures_reset(void);

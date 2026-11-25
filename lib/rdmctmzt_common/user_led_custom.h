/* Copyright 2024 Finalkey
 * Copyright 2024 LiWenLiu <https://github.com/LiuLiuQMK>
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
 
#pragma once
extern uint8_t Led_Colour_Tab[9][3];

#ifdef KK_GALAXY100_LITE
static inline uint8_t led_wave_pwm(uint8_t phase) {
    uint8_t step = phase & 63U;
    if (phase & 64U) {
        step = 63U - step;
    }
    return step == 63U ? UINT8_MAX : step * 4U;
}
bool rgb_matrix_dma_is_busy(void);
#else
extern uint8_t Led_Wave_Pwm_Tab[128];
#    define led_wave_pwm(phase) Led_Wave_Pwm_Tab[(phase)]
#endif

extern void rgb_matrix_driver_init(void);
extern void User_Pwm_Deinit(void);
extern void rgb_matrix_driver_flush_pwm_dma_start(void);
extern void rgb_matrix_driver_flush(void);
extern void rgb_matrix_driver_set_color(int index, uint8_t r, uint8_t g, uint8_t b);
extern void rgb_matrix_driver_set_color_all(uint8_t r, uint8_t g, uint8_t b);
extern const rgb_matrix_driver_t rgb_matrix_driver;

#if defined(KK_BACKLIGHT) || defined(KK_INDICATOR_FADE)
// Smoothstep in fixed point. 256 represents the exact endpoint.
static inline uint16_t kk_smoothstep(uint16_t elapsed, uint16_t duration) {
    if (elapsed >= duration) {
        return 256U;
    }
    uint32_t t = ((uint32_t)elapsed * 256U + duration / 2U) / duration;
    return (t * t * (768U - 2U * t) + 32768U) >> 16;
}
#endif

#if defined(KK_BACKLIGHT) || defined(KK_VIA_SETTINGS)
#    define KK_BACKLIGHT_FADE_MS 1000U
#endif

#ifdef KK_BACKLIGHT
void kk_backlight_reset(void);
// Returns true once a requested fade to sleep has reached black.
bool kk_backlight_set_sleeping(bool sleeping);
#endif

#ifdef KK_INDICATOR_FADE
enum {
    KK_INDICATOR_ANIMATE = 1U << 0,
    KK_INDICATOR_LIVE_COLOR = 1U << 1,
};

void kk_indicators_reset(void);
void kk_indicators_begin_frame(void);
void kk_indicator_set_color(uint8_t index, rgb_t color, uint16_t phase_ms, uint8_t flags);
void kk_indicator_clear_request(uint8_t index);
void kk_indicators_fill(rgb_t color, uint16_t phase_ms);
bool kk_indicators_finish_frame(bool *lit);
rgb_t kk_indicators_blend(uint8_t index, rgb_t background);
#endif

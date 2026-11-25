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
#include <stddef.h>
#include "rdmctmzt_common.h"

uint8_t Led_Colour_Tab[9][3] = {{255, 0, 0}, {255, 128, 0}, {255, 255, 0}, {0, 255, 0}, {0, 255, 255}, {0, 0, 255}, {128, 0, 255}, {255, 255, 255}};

#define ES_PWM_LED_SIZE (42)
#define ES_PWM_LED_BYTE (24)
#define ES_PWM_DMA_SIZE (ES_PWM_LED_SIZE * ES_PWM_LED_BYTE)
#define ES_PWM_DMA_TAIL 2U
#define ES_PWM_DMA_TASK_COUNT ((RGB_MATRIX_LED_COUNT + ES_PWM_LED_SIZE - 1U) / ES_PWM_LED_SIZE)
#define ES_PWM_DMA_TIMEOUT_MS 10U

#define ES_PWM_WS2812_H_VALUE (43)
#define ES_PWM_WS2812_L_VALUE (17)

_Static_assert(ES_PWM_DMA_SIZE + ES_PWM_DMA_TAIL <= 1024U, "PWM DMA task exceeds the controller's transfer count");
_Static_assert(sizeof(md_dma_channel_config_typedef) == 4U * sizeof(uint32_t), "DMA task must contain four words");

static rgb_led_t                     led_colors[RGB_MATRIX_LED_COUNT];
static uint8_t                       led_pwm_frame[RGB_MATRIX_LED_COUNT * ES_PWM_LED_BYTE + ES_PWM_DMA_TAIL];
static md_dma_channel_config_typedef led_dma_tasks[ES_PWM_DMA_TASK_COUNT];
static bool                          led_frame_dirty;
static uint32_t                      led_dma_started_at;

#ifdef RGB_MATRIX_DRIVER_BRIGHTNESS_TRANSITION_MS
#    define BRIGHTNESS_GAIN_ONE 65536UL
_Static_assert(RGB_MATRIX_DRIVER_BRIGHTNESS_TRANSITION_MS > 0 && RGB_MATRIX_DRIVER_BRIGHTNESS_TRANSITION_MS <= UINT16_MAX, "Invalid RGB brightness transition time");

static struct {
    uint32_t started_at;
    uint32_t from;
    uint32_t target;
    uint8_t  setting;
    bool     initialized;
} brightness_fade;

static uint32_t brightness_frame_gain;

static uint32_t rgb_matrix_brightness_target(uint8_t setting) {
    if (setting >= RGB_MATRIX_MAXIMUM_BRIGHTNESS) {
        return BRIGHTNESS_GAIN_ONE;
    }

    // Match QMK's brightness curve at the endpoints, and interpolate PWM linearly between them.
    uint8_t pwm = hsv_to_rgb((hsv_t){.v = setting}).r;
    uint8_t max = hsv_to_rgb((hsv_t){.v = RGB_MATRIX_MAXIMUM_BRIGHTNESS}).r;
    return max ? (pwm * BRIGHTNESS_GAIN_ONE + max / 2U) / max : 0;
}

static uint32_t rgb_matrix_brightness_gain(void) {
    uint8_t setting = rgb_matrix_get_val();
    uint32_t now = timer_read32();
    if (!brightness_fade.initialized) {
        brightness_fade.target      = rgb_matrix_brightness_target(setting);
        brightness_fade.from        = brightness_fade.target;
        brightness_fade.setting     = setting;
        brightness_fade.initialized = true;
    }

    uint32_t gain = brightness_fade.target;
    if (brightness_fade.from != brightness_fade.target) {
        uint32_t elapsed  = now - brightness_fade.started_at;
        uint32_t duration = RGB_MATRIX_DRIVER_BRIGHTNESS_TRANSITION_MS;
        if (elapsed < duration) {
            gain = (brightness_fade.from * (duration - elapsed) + brightness_fade.target * elapsed + duration / 2U) / duration;
        } else {
            brightness_fade.from = gain;
        }
    }

    if (setting != brightness_fade.setting) {
        brightness_fade.from       = gain;
        brightness_fade.target     = rgb_matrix_brightness_target(setting);
        brightness_fade.started_at = now;
        brightness_fade.setting    = setting;
    }
    return gain;
}
#endif

#ifdef KK_BACKLIGHT
#    ifdef KK_VIA_SETTINGS
#        include "kk_via_settings.h"
#    endif

#    define BACKLIGHT_PROGRESS_MAX 1000U
static struct {
    uint32_t started_at;
    uint16_t start_sof;
    uint16_t start_level;
    uint16_t level;
    uint16_t duration;
    bool     active;
    bool     sleeping;
    bool     usb_clock;
} backlight_fade;

static uint16_t backlight_frame_gain;

static uint16_t kk_backlight_read_sof(void) {
    // Sample the low byte between matching high bytes at the frame-number rollover.
    uint8_t high = USB->FRAME2;
    uint8_t low  = USB->FRAME1;
    if (high != USB->FRAME2) {
        high = USB->FRAME2;
        low  = USB->FRAME1;
    }
    return ((uint16_t)high << 8 | low) & 0x07ff;
}

static void kk_backlight_start(void) {
    backlight_fade.start_level = backlight_fade.level;
    backlight_fade.started_at  = timer_read32();
    backlight_fade.usb_clock   = !backlight_fade.sleeping && Keyboard_Info.Key_Mode == QMK_USB_MODE;
    backlight_fade.start_sof   = backlight_fade.usb_clock ? kk_backlight_read_sof() : 0;
#    ifdef KK_VIA_SETTINGS
    backlight_fade.duration = kk_behavior_duration(KK_BEHAVIOR_FADE);
#    else
    backlight_fade.duration = KK_BACKLIGHT_FADE_MS;
#    endif
    backlight_fade.active = backlight_fade.level != (backlight_fade.sleeping ? 0 : BACKLIGHT_PROGRESS_MAX);
}

static void kk_backlight_update(void) {
    if (!backlight_fade.active) {
        return;
    }

    uint32_t elapsed = timer_elapsed32(backlight_fade.started_at);
    uint16_t duration = backlight_fade.duration;
    // Use SOF while it is ticking. Before enumeration and during suspend the
    // local timer keeps the transition moving.
    if (backlight_fade.usb_clock && Keyboard_Info.Key_Mode == QMK_USB_MODE && elapsed < duration) {
        uint16_t frames = (kk_backlight_read_sof() - backlight_fade.start_sof) & 0x07ff;
        if (frames && frames + 2U >= elapsed && frames <= elapsed + 2U) {
            elapsed = frames;
        }
    }

    uint16_t step = elapsed >= duration ? BACKLIGHT_PROGRESS_MAX : (elapsed * BACKLIGHT_PROGRESS_MAX) / duration;
    if (backlight_fade.sleeping) {
        backlight_fade.level = backlight_fade.start_level - MIN(step, backlight_fade.start_level);
    } else {
        uint16_t level       = MIN(backlight_fade.start_level + step, BACKLIGHT_PROGRESS_MAX);
        backlight_fade.level = MAX(backlight_fade.level, level);
    }
    backlight_fade.active = backlight_fade.level != (backlight_fade.sleeping ? 0 : BACKLIGHT_PROGRESS_MAX);
}

void kk_backlight_reset(void) {
    memset(&backlight_fade, 0, sizeof(backlight_fade));
    led_frame_dirty = true;
    Led_Off_Start   = true;
}

bool kk_backlight_set_sleeping(bool sleeping) {
    if (sleeping != backlight_fade.sleeping) {
        kk_backlight_update();
        backlight_fade.sleeping = sleeping;
        if (sleeping && Led_Off_Start) {
            backlight_fade.level = 0;
        }
        kk_backlight_start();
    }
    if (sleeping) {
        kk_backlight_update();
    }
    return sleeping && !backlight_fade.active;
}

static uint16_t kk_backlight_gain(void) {
    // Start the power-on fade when the first frame can be sent.
    if (!backlight_fade.active && !backlight_fade.sleeping && backlight_fade.level == 0) {
        kk_backlight_start();
    }
    kk_backlight_update();

    return kk_smoothstep(backlight_fade.level, BACKLIGHT_PROGRESS_MAX);
}
#endif

static void rgb_matrix_init_dma_tasks(void) {
    for (uint8_t i = 0; i < ES_PWM_DMA_TASK_COUNT; i++) {
        md_dma_channel_config_typedef *task = &led_dma_tasks[i];
        uint16_t                       start = i * ES_PWM_DMA_SIZE;
        bool                           last = i + 1U == ES_PWM_DMA_TASK_COUNT;
        uint16_t                       size = last ? sizeof(led_pwm_frame) - start : ES_PWM_DMA_SIZE;

        task->source_data_end_address      = (uint32_t)(uintptr_t)&led_pwm_frame[start + size - 1U];
        task->destination_data_end_address = (uint32_t)(uintptr_t)&GP16C2T1->CCVAL1;
        task->control.word                 = ((uint32_t)(size - 1U) << 4) | g_es_dma_ch2alt_cfg;
        if (last) {
            // FS026 peripheral scatter-gather ends with a Basic transfer.
            task->control.md_dma_config_typedef.cycle_mode = MD_DMA_CHANNEL_CFG_MODE_BASIC;
        }
    }
}

static bool rgb_matrix_write_dma_register(volatile uint32_t *reg, uint32_t value) {
    // FS026 requires reading back the channel registers and retrying lost writes.
    for (uint8_t attempt = 0; attempt < 4; ++attempt) {
        *reg = value;
        if (*reg == value) {
            return true;
        }
    }
    return false;
}

bool rgb_matrix_dma_is_busy(void) {
    return md_dma_is_enabled_channel(DMA1, MD_DMA_CHANNEL2);
}

void rgb_matrix_driver_init(void) {
    md_rcu_enable_dma1(RCU);
    md_dma_set_configuration(DMA1, ENABLE);
    md_dma_disable_channel(DMA1, MD_DMA_CHANNEL2);

    md_rcu_enable_gp16c2t1(RCU);
    md_timer_set_auto_reload_value_arrv(GP16C2T1, 60);
    md_timer_set_output_compare1_mode_ch1mod(GP16C2T1, MD_TIMER_OUTPUTMODE_PWMMODE1);
    md_timer_set_capture_compare1_value_ccrv1(GP16C2T1, 0);
    md_timer_enable_cc1_output_cc1en(GP16C2T1);
    md_timer_enable_main_output_goen(GP16C2T1);

    md_timer_enable_output_compare1_preload_ch1pen(GP16C2T1);
    md_timer_disable_dma_upd(GP16C2T1);
    md_timer_enable_counter_cnten(GP16C2T1);

    gpio_set_pin_output(ES_PWM_DMA_IO);

    GPIOA->AFL &= 0xFFFFF0FF;
    GPIOA->AFL |= 0x00000500;

    GPIOA->MOD &= 0xFFFFFFCF;
    GPIOA->MOD |= 0x00000020;

    md_dma_set_request_peripherals(DMA1, MD_DMA_CHANNEL2, MD_DMA_PRS_GP16C2T1_UP);
    rgb_matrix_init_dma_tasks();

    led_frame_dirty = true;
#ifdef RGB_MATRIX_DRIVER_BRIGHTNESS_TRANSITION_MS
    brightness_fade.initialized = false;
#endif
#if defined(KK_BACKLIGHT) || defined(KK_INDICATOR_FADE)
    Led_Off_Start = true;
#endif
#ifdef KK_INDICATOR_FADE
    kk_indicators_reset();
#endif
}

void User_Pwm_Deinit(void) {
    md_rcu_enable_gp16c2t1_reset(RCU);
    md_rcu_disable_gp16c2t1_reset(RCU);
    md_rcu_disable_gp16c2t1(RCU);
    md_rcu_enable_dma1_reset(RCU);
    md_rcu_disable_dma1_reset(RCU);
    md_rcu_disable_dma1(RCU);

    gpio_set_pin_output(ES_PWM_DMA_IO);
    gpio_write_pin_low(ES_PWM_DMA_IO);
}

static void rgb_matrix_encode_led(uint8_t index, rgb_led_t led) {
    uint8_t *buf = led_pwm_frame + index * ES_PWM_LED_BYTE;

    // The LEDs expect each channel MSB-first, in green, red, blue order.
    const uint8_t channels[] = {led.g, led.r, led.b};
    for (uint8_t i = 0; i < sizeof(channels); i++) {
        for (uint8_t bit = 0x80; bit; bit >>= 1) {
            *buf++ = (channels[i] & bit) ? ES_PWM_WS2812_H_VALUE : ES_PWM_WS2812_L_VALUE;
        }
    }
}

void rgb_matrix_driver_flush_pwm_dma_start(void) {
    // DMA owns the encoded frame until the entire chain completes.
    if (rgb_matrix_dma_is_busy()) {
        if (timer_elapsed32(led_dma_started_at) >= ES_PWM_DMA_TIMEOUT_MS) {
            md_timer_disable_dma_upd(GP16C2T1);
            md_dma_disable_channel(DMA1, MD_DMA_CHANNEL2);
            md_timer_set_capture_compare1_value_ccrv1(GP16C2T1, 0);
        }
        return;
    }
    md_timer_disable_dma_upd(GP16C2T1);
    __DMB();

#ifdef KK_BACKLIGHT
    if (!Led_Power_Up || (backlight_fade.sleeping && kk_backlight_set_sleeping(true))) {
#else
    if (Keyboard_Status.System_Sleep_Mode || ((Keyboard_Info.Key_Mode != QMK_USB_MODE) && Usb_Change_Mode_Wakeup && Keyboard_Status.System_Work_Status) || (!Led_Power_Up)) {
#endif
#ifdef KK_INDICATOR_FADE
        kk_indicators_begin_frame();
#endif
        Led_Off_Start = true;
        gpio_write_pin_low(ES_LED_POWER_IO);
        return;
    }

    // Keep LED power on if either RGB matrix OR logo LEDs are enabled, or if
    // anything (e.g. an indicator drawn while the matrix is off) is lit.
    bool leds_active = rgb_matrix_is_enabled();
#ifdef KK_INDICATOR_FADE
    bool indicators_lit;
    led_frame_dirty |= kk_indicators_finish_frame(&indicators_lit);
    leds_active |= indicators_lit;
#endif
#if LOGO_LED_ENABLE
    leds_active = leds_active || Keyboard_Info.Logo_On_Off;
#endif
    for (uint8_t i = 0; !leds_active && i < RGB_MATRIX_LED_COUNT; i++) {
        leds_active = led_colors[i].r || led_colors[i].g || led_colors[i].b;
    }

    if (leds_active) {
        gpio_write_pin_high(ES_LED_POWER_IO);
        if (Led_Off_Start) {
            Led_Off_Start = false;
            wait_ms(3);
        }
    } else {
        Led_Off_Start = true;
        gpio_write_pin_low(ES_LED_POWER_IO);
    }

#ifdef KK_BACKLIGHT
    uint16_t gain = kk_backlight_gain();
    led_frame_dirty |= gain != backlight_frame_gain;
    backlight_frame_gain = gain;
#endif
#ifdef RGB_MATRIX_DRIVER_BRIGHTNESS_TRANSITION_MS
    uint32_t brightness_gain = rgb_matrix_brightness_gain();
    led_frame_dirty |= brightness_gain != brightness_frame_gain;
    brightness_frame_gain = brightness_gain;
#endif
    // The unencoded colors remain available for the next render while DMA runs.
    if (led_frame_dirty) {
        for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; i++) {
            rgb_led_t led = led_colors[i];
#ifdef RGB_MATRIX_DRIVER_BRIGHTNESS_TRANSITION_MS
            if (brightness_gain != BRIGHTNESS_GAIN_ONE) {
                led.r = (led.r * brightness_gain + BRIGHTNESS_GAIN_ONE / 2U) >> 16;
                led.g = (led.g * brightness_gain + BRIGHTNESS_GAIN_ONE / 2U) >> 16;
                led.b = (led.b * brightness_gain + BRIGHTNESS_GAIN_ONE / 2U) >> 16;
            }
#endif
#ifdef KK_INDICATOR_FADE
            led = kk_indicators_blend(i, led);
#endif
#ifdef KK_BACKLIGHT
            if (gain != 256U) {
                led.r = (led.r * gain + 128U) >> 8;
                led.g = (led.g * gain + 128U) >> 8;
                led.b = (led.b * gain + 128U) >> 8;
            }
#endif
            rgb_matrix_encode_led(i, led);
        }
        led_frame_dirty = false;
    }

    uint32_t task_words = ES_PWM_DMA_TASK_COUNT * (sizeof(led_dma_tasks[0]) / sizeof(uint32_t));
    uint32_t source_end = (uint32_t)(uintptr_t)&led_dma_tasks[ES_PWM_DMA_TASK_COUNT - 1U].reserved;
    uint32_t dest_end   = (uint32_t)(uintptr_t)&DMA1->ALT_CH02_SRC_DATA_END_PTR + offsetof(md_dma_channel_config_typedef, reserved);

    md_dma_enable_primary_mode(DMA1, MD_DMA_CHANNEL2);
    if (!rgb_matrix_write_dma_register(&DMA1->PRI_CH02_SRC_DATA_END_PTR, source_end) ||
        !rgb_matrix_write_dma_register(&DMA1->PRI_CH02_DST_DATA_END_PTR, dest_end) ||
        !rgb_matrix_write_dma_register(&DMA1->PRI_CH02_CHANNEL_CFG, ((task_words - 1U) << 4) | g_es_dma_ch2pri_cfg)) {
        return;
    }

    led_dma_started_at = timer_read32();
    __DMB();
    md_dma_enable_channel(DMA1, MD_DMA_CHANNEL2);
    md_timer_enable_dma_upd(GP16C2T1);
}

void rgb_matrix_driver_flush(void) {
    Led_Flash_Busy = false;
    if ((GP16C2T1->AR) != 60) {
        rgb_matrix_driver_init();
    }

    rgb_matrix_driver_flush_pwm_dma_start();
}

void rgb_matrix_driver_set_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index >= RGB_MATRIX_LED_COUNT) {
        return;
    }

    if (index == (RGB_MATRIX_LED_COUNT - 1)) {
        Led_Flash_Busy = false;
    } else if (index == 0) {
        Led_Flash_Busy = true;
    }

    if (led_colors[index].r == r && led_colors[index].g == g && led_colors[index].b == b) {
        return;
    }

    led_colors[index] = (rgb_led_t){r, g, b};
    led_frame_dirty = true;
}

void rgb_matrix_driver_set_color_all(uint8_t r, uint8_t g, uint8_t b) {
    for (uint32_t i = 0; i < RGB_MATRIX_LED_COUNT; i++) {
        rgb_matrix_driver_set_color(i, r, g, b);
    }
}

// clang-format off
const rgb_matrix_driver_t rgb_matrix_driver = {
    .init          = rgb_matrix_driver_init,
    .flush         = rgb_matrix_driver_flush,
    .set_color     = rgb_matrix_driver_set_color,
    .set_color_all = rgb_matrix_driver_set_color_all,
};

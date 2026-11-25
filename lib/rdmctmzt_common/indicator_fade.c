// SPDX-License-Identifier: GPL-2.0-or-later
#include "keyboard_common.h"

#ifdef KK_INDICATOR_FADE
#    ifndef KK_INDICATOR_FADE_MS
#        define KK_INDICATOR_FADE_MS 100U
#    endif
#    ifdef KK_VIA_SETTINGS
#        include "kk_via_settings.h"
#    endif

// Battery row, three locks, five mode keys, the connection LED, and spare
// slots for reset indicators fading out after being moved to another key.
#    define INDICATOR_SLOT_COUNT (BATTERY_LED_COUNT + 12U)

typedef enum {
    INDICATOR_HIDDEN,
    INDICATOR_SOLID,
    INDICATOR_ANIMATED,
} indicator_kind_t;

typedef struct {
    rgb_t   color;
    uint8_t alpha;
} indicator_color_t;

typedef struct {
    indicator_color_t from;
    indicator_color_t shown;
    rgb_t             target_color;
    rgb_t             requested_color;
    uint16_t          phase_ms;
    uint16_t          started_at;
    uint16_t          duration;
    uint8_t           requested_kind;
    uint8_t           target_kind;
    bool              live_color;
    bool              moving;
} indicator_fade_t;

typedef struct {
    indicator_fade_t fade;
    uint8_t          led;
} indicator_slot_t;

static indicator_fade_t matrix_indicator;
static indicator_slot_t indicator_slots[INDICATOR_SLOT_COUNT];
static uint8_t          indicator_count;

static uint8_t interpolate(uint8_t from, uint8_t to, uint16_t progress) {
    return (from * (256U - progress) + to * progress + 128U) >> 8;
}

static void request_color(indicator_fade_t *fade, rgb_t color, uint16_t phase_ms, uint8_t flags) {
    fade->requested_color    = color;
    fade->phase_ms           = phase_ms;
    fade->requested_kind     = (flags & KK_INDICATOR_ANIMATE) ? INDICATOR_ANIMATED : INDICATOR_SOLID;
    fade->live_color         = (flags & KK_INDICATOR_LIVE_COLOR) != 0;
}

static bool update_indicator(indicator_fade_t *fade, uint16_t now, uint16_t duration, bool *lit) {
    indicator_color_t previous = fade->shown;
    indicator_color_t target   = {0};
    if (fade->requested_kind != INDICATOR_HIDDEN) {
        target.color = fade->requested_color;
        target.alpha = UINT8_MAX;
    }

    // The charging wave supplies a new color every frame. Ease the edges of
    // the wave's visibility without restarting its fade on every color update.
    bool follow_animation = fade->requested_kind == INDICATOR_ANIMATED && fade->target_kind == INDICATOR_ANIMATED;
    bool visibility_changed = (fade->requested_kind == INDICATOR_HIDDEN) != (fade->target_kind == INDICATOR_HIDDEN);
    bool color_changed = memcmp(&target.color, &fade->target_color, sizeof(target.color)) != 0;
    bool live_color = fade->live_color && target.alpha;
    if (visibility_changed || (color_changed && !follow_animation && !live_color)) {
        fade->from       = fade->shown;
        fade->started_at = now;
        fade->moving     = memcmp(&fade->from, &target, sizeof(target)) != 0;
        fade->duration   = fade->phase_ms ? MIN(fade->phase_ms, duration) : duration;
    }
    if (live_color && color_changed) {
        // Apply the new hue at the current opacity without restarting the visibility fade.
        uint8_t alpha = fade->from.alpha;
        fade->from.color = (rgb_t){
            (target.color.r * alpha + 127U) / 255U,
            (target.color.g * alpha + 127U) / 255U,
            (target.color.b * alpha + 127U) / 255U,
        };
    }
    fade->target_color   = target.color;
    fade->target_kind    = fade->requested_kind;
    fade->requested_kind = INDICATOR_HIDDEN;

    uint16_t elapsed = TIMER_DIFF_16(now, fade->started_at);
    if (fade->moving && elapsed < fade->duration) {
        uint16_t progress   = kk_smoothstep(elapsed, fade->duration);
        fade->shown.color.r = interpolate(fade->from.color.r, target.color.r, progress);
        fade->shown.color.g = interpolate(fade->from.color.g, target.color.g, progress);
        fade->shown.color.b = interpolate(fade->from.color.b, target.color.b, progress);
        fade->shown.alpha   = interpolate(fade->from.alpha, target.alpha, progress);
    } else {
        fade->moving = false;
        fade->shown  = target;
    }

    *lit |= (fade->shown.color.r | fade->shown.color.g | fade->shown.color.b | target.color.r | target.color.g | target.color.b) != 0;
    return memcmp(&previous, &fade->shown, sizeof(previous)) != 0;
}

static indicator_fade_t *find_indicator(uint8_t index) {
    for (uint8_t i = 0; i < indicator_count; i++) {
        if (indicator_slots[i].led == index) {
            return &indicator_slots[i].fade;
        }
    }
    return NULL;
}

void kk_indicators_reset(void) {
    memset(&matrix_indicator, 0, sizeof(matrix_indicator));
    indicator_count = 0;
}

void kk_indicators_begin_frame(void) {
#    ifdef KK_VIA_SETTINGS
    kk_indicator_begin_color_frame();
#    endif
    matrix_indicator.requested_kind = INDICATOR_HIDDEN;
    for (uint8_t i = 0; i < indicator_count; i++) {
        indicator_slots[i].fade.requested_kind = INDICATOR_HIDDEN;
    }
}

void kk_indicator_set_color(uint8_t index, rgb_t color, uint16_t phase_ms, uint8_t flags) {
    if (index >= RGB_MATRIX_LED_COUNT) {
        return;
    }

    indicator_fade_t *fade = find_indicator(index);
    if (!fade && indicator_count < INDICATOR_SLOT_COUNT) {
        indicator_slot_t *slot = &indicator_slots[indicator_count++];
        *slot                  = (indicator_slot_t){.led = index};
        fade                   = &slot->fade;
    }
    if (fade) {
        request_color(fade, color, phase_ms, flags);
    }
}

void kk_indicator_clear_request(uint8_t index) {
    indicator_fade_t *fade = find_indicator(index);
    if (fade) {
        fade->requested_kind = INDICATOR_HIDDEN;
    }
}

void kk_indicators_fill(rgb_t color, uint16_t phase_ms) {
    // A full-matrix indication replaces lower-priority requests in this frame.
    kk_indicators_begin_frame();
    request_color(&matrix_indicator, color, phase_ms, 0);
}

bool kk_indicators_finish_frame(bool *lit) {
#    ifdef KK_VIA_SETTINGS
    uint16_t duration = kk_behavior_duration(KK_BEHAVIOR_INDICATOR_FADE);
#    else
    uint16_t duration = KK_INDICATOR_FADE_MS;
#    endif
    uint16_t now     = timer_read();
    *lit             = false;
    bool changed     = update_indicator(&matrix_indicator, now, duration, lit);
    for (uint8_t i = 0; i < indicator_count;) {
        indicator_fade_t *fade = &indicator_slots[i].fade;
        changed |= update_indicator(fade, now, duration, lit);
        if (fade->target_kind == INDICATOR_HIDDEN && !fade->moving) {
            indicator_slots[i] = indicator_slots[--indicator_count];
        } else {
            i++;
        }
    }
#    ifdef KK_VIA_SETTINGS
    kk_indicator_end_color_frame();
#    endif
    return changed;
}

static rgb_t over(rgb_t background, indicator_color_t layer) {
    if (layer.alpha == UINT8_MAX) {
        return layer.color;
    }
    if (!layer.alpha) {
        return background;
    }

    // During a transition the layer's RGB values are already multiplied by
    // their opacity. Only the current background needs to be scaled here.
    uint8_t remaining = UINT8_MAX - layer.alpha;
    return (rgb_t){
        .r = layer.color.r + (background.r * remaining + 127U) / 255U,
        .g = layer.color.g + (background.g * remaining + 127U) / 255U,
        .b = layer.color.b + (background.b * remaining + 127U) / 255U,
    };
}

rgb_t kk_indicators_blend(uint8_t index, rgb_t background) {
    rgb_t                   color = over(background, matrix_indicator.shown);
    const indicator_fade_t *fade  = find_indicator(index);
    return fade ? over(color, fade->shown) : color;
}
#endif

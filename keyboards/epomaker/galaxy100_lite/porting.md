# Galaxy100 Lite implementation and porting guide

The Galaxy100 Lite build selects its own PWM DMA driver in [`rules.mk`](rules.mk).
[`config.h`](config.h) enables the features for this model. Keyboards without
these options continue to use the existing QMK and `rdmctmzt_common` paths.

## Backlight, indicators and DMA

[`galaxy100_lite_rgb.c`](galaxy100_lite_rgb.c) holds the colors being drawn in
an RGB array. At the start of a render, QMK captures the effect settings in
[`rgb_matrix.c`](../../../quantum/rgb_matrix/rgb_matrix.c). The driver applies
the interpolated brightness, blends the indicators, and applies the overall
backlight fade before encoding the colors as PWM bytes. The encoded frame stays
stable while DMA sends the full chain. On this board the chain has two groups
of 42 LEDs and a final group of 17 LEDs plus two trailing bytes. The final DMA
descriptor uses Basic mode to finish the FS026 scatter-gather transfer.

[`indicator_fade.c`](../../../lib/rdmctmzt_common/indicator_fade.c) uses one
easing curve for visibility changes across locks, connection states and status
bars. VIA color edits update hue at the current opacity. The charging wave
supplies a new color each frame while visibility continues to ease. Timed
blinks cap their fade duration at the length of the blink phase.

To port these pieces, start with the target board's existing RGB driver.

1. For DMA, check the LED order, pin, timer, DMA request and channel, transfer
   limits, and reset interval. Keep the buffer owned by DMA stable until the
   entire frame completes. Preserve any additional LED zones the target drives.
2. Enable `RGB_MATRIX_DRIVER_BRIGHTNESS_TRANSITION_MS` once the target driver
   applies brightness to the finished RGB frame. QMK renders at a fixed
   brightness under this option, so the driver must apply the user's setting.
3. For the indicator module, add `indicator_fade.c` to the target build, use the
   target's LED indices, and integrate begin, draw, finish and blend into its
   render cycle. `KK_INDICATOR_FADE` enables that path in the shared library.
4. Add `RGB_DIGITAL_RAIN_USE_QMK_RANDOM` to the target config to select QMK's
   `random16()` for Digital Rain where that effect is enabled.

## Sleep and the encoder

[`user_system.c`](../../../lib/rdmctmzt_common/user_system.c) uses the existing
USB SOF check and wireless sleep handshake. In USB mode it waits about 100 ms
for SOF to resume before starting a fade. The local timer keeps the fade moving
when SOF stops. In wireless modes the radio requests sleep, and the firmware
finishes the fade before completing the sleep handshake. The default duration
under `KK_BACKLIGHT` is 1,000 ms. It can be changed in VIA.

On the Galaxy100 Lite, the encoder button is at matrix position `(5, 3)` and
quadrature uses PB2 and PB10. During wireless sleep the GPIO interrupts feed the
existing QMK quadrature decoder and its event queue. Ordinary polling resumes
after wake. [`encoder_gestures.c`](encoder_gestures.c) processes the button and
turn events and dispatches assigned keycodes through QMK. Empty VIA assignments
fall back to the standard button or turn mapping.

For another board, keep its existing USB suspend and radio sleep entry points.
Apply the fade before deep sleep and resume it after wake. For rotation wake,
check the encoder's pin bias, edge selection and interrupt groups against the
matrix and radio. Carry pending rotation events into the normal decoder or
event queue across wake. Adapt the gesture module's matrix position and encoder
index, then verify both directions and pick a resolution that matches one
physical detent. `KEYCODE_OVERRIDE_ENABLE` gives the gesture dispatcher access
to QMK's normal keycode pipeline where this board lacks Combo and Repeat Key.

## VIA, EEPROM and battery

[`kk_via_settings.c`](kk_via_settings.c) handles the custom settings declared
in the [VIA definition](VIA_Mapping_Galaxy100_Lite.json). It uses the 64 bytes
above `DYNAMIC_KEYMAP_EEPROM_MAX_ADDR` (`1984`–`2047` on this board). The record
has a checksum and a version marker written last. The loader migrates the
supported older records and uses defaults if the saved record is invalid.

`RGB_MATRIX_DEFER_EEPROM_WRITE` moves RGB saves to the keyboard's housekeeping
task. With `RAW_HID_ORDERED_RESPONSES`, the USB task services custom commands in
order and housekeeping waits for a quiet interval and idle HID and DMA queues
before writing to flash. The FS026 EEPROM implementation batches changed bytes
that share a flash record.

The shared battery code uses integer arithmetic for software ADC calibration on
this board and keeps the factory calibration path when those values are present.
The charge controller's standby signal selects charging or charged. While charging,
the displayed estimate stays below 100%. The latest wireless battery update
remains pending until SPI can send it.

For another board, match its own USB IDs, keep the JSON channel and value IDs in
sync with the custom value handler, and reserve space outside its dynamic keymap
and macros. Preserve the board's existing storage layout or migrate it explicitly.
Use the target's ADC reference, voltage divider, thresholds and charger status pin
when integrating battery reporting.

## Configuration scope

| Option | Scope |
| --- | --- |
| `KK_GALAXY100_LITE` | Board hardware integration, including encoder wake, mode switching, and local ADC, EEPROM and SPI changes. |
| `KK_BACKLIGHT` | Overall fade at power-on, sleep and wake, paired with a driver that applies the fade. |
| `KK_INDICATOR_FADE` | Shared visibility easing, paired with the indicator module and render hooks. |
| `KK_VIA_SETTINGS` | Board-specific settings and their EEPROM record. |
| `RGB_MATRIX_DRIVER_BRIGHTNESS_TRANSITION_MS` | Frame settings snapshot and duration of the driver's brightness interpolation. |
| `RGB_DIGITAL_RAIN_USE_QMK_RANDOM` | Optional generator for the QMK Digital Rain effect. |
| `RGB_MATRIX_DEFER_EEPROM_WRITE`, `RAW_HID_ORDERED_RESPONSES` | Optional QMK RGB persistence and ChibiOS Raw HID changes used by the board's VIA integration. |

Use the target keyboard's own config for opt-in. For a related FS026 board such
as Galaxy68, extract the feature you need into a shared option or hook and keep
the Galaxy100 Lite hardware selector scoped to this model.

## Verification

The Galaxy100 Lite, Galaxy68 and Keychron V1 ANSI default targets compile. The
Galaxy68 and Keychron builds leave the new options disabled. The Keychron build
retains the previous Digital Rain generator. A host-side comparison of the
shared indicator code with the new options disabled covered 36,864 combinations
of indicator state and matched the original LED writes and state transitions.

For hardware validation on a target board, check cold boot, idle sleep, wake by
key and encoder, and short USB interruptions. Repeat in USB, Bluetooth and
2.4 GHz modes where supported. Check RGB while quickly changing colors and
brightness in VIA. Verify gesture fallbacks, persistence after reconnect and
reboot, and the charging-to-charged transition.

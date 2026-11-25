# EPOMAKER Galaxy100 Lite

A customizable 96% (1800) keyboard with 100 keys and a rotary knob, built around
the same ES32 FS026 microcontroller as the other Epomaker wireless boards in this
tree (TH40, Luma40, Galaxy68, TH85). It uses the open-source
[`lib/rdmctmzt_common`](../../../lib/rdmctmzt_common/README.md) library instead of
the closed-source `rdr_lib` shipped by the vendor.

* Keyboard Maintainer: [CarlosEDP](https://github.com/carlosedp)
* Hardware Supported: EPOMAKER Galaxy100 Lite PCB with es32fs026 microcontroller

From the repository root, initialize the QMK submodules and build the default keymap.

```sh
git submodule update --init --recursive
make epomaker/galaxy100_lite:default
```

The build produces `epomaker_galaxy100_lite_default.bin` in the repository root.

1. Hold Esc while plugging in the keyboard in wired mode to enter the bootloader.
2. Copy the binary to the drive mounted by the bootloader.
3. Wait for the drive to disappear and the keyboard to restart.

See the [QMK build environment setup](../../../docs/newbs_getting_started.md)
and [make instructions](../../../docs/getting_started_make_guide.md).

## Firmware changes

- **Backlight.** A single fade duration covers power-on, sleep and wake. Brightness
  adjustments interpolate over 100 ms and can be repeated by holding the key.
  Changing brightness previews a white bar on `1`–`0` in the default configuration.
- **Indicators.** Locks, connection modes and status bars share an easing function.
  VIA controls visibility, color and brightness for each indicator, with one shared
  indicator fade duration.
- **Encoder.** The quadrature resolution is two transitions per step. The firmware
  handles double click, triple click, hold, hold and turn, and click followed by hold
  and turn. Rotation can wake the keyboard in Bluetooth and 2.4 GHz modes.
- **RGB and battery.** The driver keeps the DMA frame stable during transfer. VIA
  defers flash writes during edits. Digital Rain uses QMK's random generator, and
  software ADC calibration uses integer arithmetic. The brightness limit is `180`.

See the [implementation and porting guide](porting.md) for the code paths,
configuration switches and checks for adapting a feature to another board.

## VIA

VIA is enabled in the default keymap. Load
[`VIA_Mapping_Galaxy100_Lite.json`](VIA_Mapping_Galaxy100_Lite.json) as a design
file in VIA (Settings -> Show Design tab -> Design -> Load). The vendor's original
file is kept alongside it as `Epomaker Galaxy100 Lite.JSON` for reference; the
maintained one carries the custom keycode list and the Galaxy100 Lite settings.

**Lighting → Backlight → Fade** sets the backlight transition time at power-on,
sleep, and wake in milliseconds. Set it to zero for an instant transition.
**Lighting → Lock Indicators → Indicator Fade** sets the shared indicator fade
duration in milliseconds. It defaults to 100 ms. Set it to zero for instant transitions.
**Lock Indicators**, **Mode Indicators**, and **Status Bars** group the individual indicator switches. Turn on
an indicator to reveal its color and brightness controls. The battery bar uses
**Low Color** through 20%, **Medium Color** through 50%, and **High / Charged
Color** above 50% and while charging. The low-power warning has its own red
signal.

**Lighting → Status Bars → Battery Level** displays the keyboard's estimated
percentage and the charger's state in compatible VIA clients. At startup on
USB, the estimate starts from the saved value and converges gradually. A startup
on battery takes a fresh ADC estimate.

**Gestures** accepts QMK keycodes for the knob. All seven fields default to
`KC_NO`. Clearing a click or hold gesture uses the knob's ordinary button
assignment once. Clearing a held-turn direction uses the normal encoder map on
the active layer. The timing fields in **Gestures → Encoder** set the click window
(220 ms by default) and the hold threshold (500 ms by default). The hold action
runs on release so holding and turning can still trigger a turn gesture.
Settings are saved to the last 64 bytes of emulated EEPROM, above the keymap and
macro regions.

The effect dropdown is generated from the effect order the firmware actually
compiles (the enabled entries of `rgb_matrix.animations` in `keyboard.json`, in
`quantum/rgb_matrix/animations/rgb_matrix_effects.inc` order). If you change that
animation list, the dropdown has to be regenerated or VIA will select the wrong
effect.

## Layout notes

`LAYOUT_1800_ansi` has 101 positions: 100 keys plus the knob press on `(5, 3)`.
To the right of the F-row there are only three keys — Home, End and PrtSc — and
then the knob.

The vendor's `info.json` also lists `(5, 8)`, stacked at the same coordinate as
the knob. There is no switch there; the slot only exists because LED 17 is wired
to it. It is left out of the layout, as it is in the vendor's own VIA file.

## Macro storage

The stock firmware left 194 bytes for VIA macros. This build gets that to **570
bytes** by keeping the shared 2048-byte EEPROM budget and spending the extra room
on macros rather than layers: the 7x16 matrix costs 224 bytes per VIA layer, so
this board is configured for 6 dynamic keymap layers instead of the 8 the other
FS026 boards use.

Enlarging `EEPROM_SIZE` instead would buy more macro space, but it lengthens the
wear-levelling page transfer in `user_eeprom.c`, which runs with the CPU tied up
rewriting flash — that stalls USB reports and matrix scanning, so it is not a
free trade.

## Bootloader

Enter the bootloader in 2 ways:

* **Bootmagic reset**: Hold down the key at (0,0) in the matrix (Esc key) and plug in the keyboard
* **Physical reset button**: Briefly press the button on the back of the PCB

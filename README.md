# NTAG5/BeUno temp reader

![Screenshot, Where is the Patch? Arm, Chest, Abdomen](https://screenshot.tbspace.de/sfbjekzhilu.png) ![Patch: Chest, Sensor: 37.03C](https://screenshot.tbspace.de/rbvylfezwat.png)

Flipper Zero app for body temperature patches built from an NXP NTAG 5 link (ISO 15693)
with a TI TMP117 temperature sensor on its I2C master, as used by the BeUno patches.
The original phone app no longer works, this app reads the patches directly.

## Usage

1. Apps → NFC → NTAG5/BeUno temp reader
2. Pick where the patch is worn: Arm, Chest, Abdomen or Custom
3. Hold the patch to the back of the Flipper, near the top (the NFC antenna)

The app measures about once per second and shows the estimated body temperature in large
digits, with the raw skin temperature from the sensor below. The green LED blinks on every
reading. When the patch is removed, the last value stays on screen with a note.

## How it works

The command sequence is the one of the original app, sent as ISO 15693 custom commands
(manufacturer code 04, NXP):

| Step | Frame | Meaning |
|---|---|---|
| Select | `22 25 <UID>` | Select the tag, the following commands use the select flag |
| Check | `12 C0 04 3D 00` | READ CONFIG: energy harvesting must be on, it powers the sensor |
| Start | `12 D4 04 48 02 01 0C 00` | WRITE I2C to 0x48: TMP117 config = `0x0C00`, one-shot conversion |
| | wait 50 ms | |
| Pointer | `12 D4 04 48 00 00` | WRITE I2C: register pointer to the temperature register |
| Read | `12 D5 04 C8 01` | READ I2C: 2 bytes into the tag SRAM |
| Fetch | `12 D2 04 00 00` | READ SRAM block 0: flags, T_hi, T_lo, ... |

The skin temperature is the signed big-endian register value / 128. `0x8000` means no
conversion happened.

The body temperature estimate is the piecewise linear model the original app used when its
cloud service gave no estimate (`body_temp_estimator.c`). Chest has its own curve, the other
locations use the arm curve. Outside about 31 to 37 °C the skin temperature is shown unchanged.

Nothing is written to the tag's memory or configuration. The TMP117 configuration register
and the tag SRAM are volatile.

## Build

```sh
ufbt
ufbt launch
```

CI builds the app with uFBT for official firmware (dev and release), Unleashed and Momentum.

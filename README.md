# ESP32-S3-Touch-LCD-2 fault reports: ghost touches and a dead IMU INT1

Two hardware problems on the [Waveshare ESP32-S3-Touch-LCD-2](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2),
each with a small test sketch built on Waveshare's own setup:

1. [Ghost touches](#ghost-touch-test) (`touch_paint/`)
2. [IMU INT1 never reaches GPIO3](#imu-int1-gpio3-is-held-low) (`imu_int1_test/`)

## Ghost touch test

A small test sketch for the [Waveshare ESP32-S3-Touch-LCD-2](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2)
(2" ST7789 240×320, CST816D touch). On my two boards the touch controller reports touches that
never happened: short contacts, and sometimes long holds, along the vertical centre line of the
screen (around x = 120–130 in portrait), while nothing is touching the glass.

It happens on both of my boards, on USB power and on battery alone, and with Waveshare's own
demo code, so I'd like to know whether other boards do the same.

## What the sketch does

It uses only Waveshare's own display setup (from their `08_lvgl_example`) and their `bsp_cst816`
touch driver. No LVGL, no WiFi, no register writes to the touch chip.

- every touch sample draws a yellow dot, every new contact a red ring
- the header shows uptime and the number of contacts so far
- serial (115200 baud) prints one line per contact: start time, duration, first and last position
- sending `r` over serial prints the touch chip's id and config registers (read-only)

## How to test

1. Build and flash it (below).
2. Put the board down flat and don't touch it or the cable. Leave it for 15–30 minutes.
3. Look at the screen: any red ring or a contact count above 0 is a ghost touch.
   A photo of the screen plus the serial output (if you have it) is ideal.

On my boards the first ghost often shows up within a few minutes, but there can be quiet stretches of 10+ minutes.

## Build and flash

You need the libraries from Waveshare's
[demo zip](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2/ESP32-S3-Touch-LCD-2-Demo.zip),
folder `Arduino/libraries/`: `GFX_Library_for_Arduino` (1.5.0) and `bsp_cst816`.
Tested with the esp32 Arduino core 3.1.3.

**Arduino IDE:** copy those two library folders into your Arduino `libraries` folder, open
`touch_paint/touch_paint.ino`, select board *ESP32S3 Dev Module* with *USB CDC On Boot: Enabled*,
and upload.

**arduino-cli:**

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc \
  --libraries <demo>/Arduino/libraries --output-dir build touch_paint
esptool.py --port <port> write_flash 0x0 build/touch_paint.ino.merged.bin
```

If the board has firmware with a custom partition table on it, run `esptool.py --port <port> erase_flash` first.

## IMU INT1 (GPIO3) is held low

The QMI8658 interrupt can't be used for wake on motion, or for anything else.

**Schematic:** IO3 = net `IMU_INT1` = U3 (QMI8658C) pin 4, and the ESP32-S3 side is pin 8. Nothing else is on the net.

**Symptom:** GPIO3 reads 0 all the time. Wake on motion arms fine (registers read back as written, the WoM flag in
`STATUS1` bit 2 sets on motion), but the pin never produces an edge. Waveshare's demos poll the IMU over I2C, which is
why this goes unnoticed.

**What I measured** (ESP-IDF firmware, two modules with identical results; the sketch here repeats the main steps):

| Test | Result |
|---|---|
| Wake on motion, all four `CAL1_H[7:6]` encodings, INT1 and INT2 enables (`CTRL1` 0x48 / 0x58), 1 mg, shaken | WoM flag sets every time, **0 edges on IO3** |
| IO3 with the internal pull-up, 10 ms polls | **reads 0 on every poll** |
| Same on GPIO2 (a free pin, control) | pull-up 1, driven high 1, as expected |
| INT1 set high-Z (`CTRL1` 0x40), then after an IMU soft reset | IO3 still 0 with the pull-up |
| Wake on motion at 255 mg (no motion), INT1 initial level 0 then 1 | no GPIO follows the level, IO3 stays 0 |
| **IO3 before any I2C traffic** (first thing in `app_main`, after a USB power cycle, IMU at power-on defaults) | **0 with the pull-up**: not caused by my IMU configuration |
| Drive IO3 high for 20 us at 5 / 10 / 20 / 40 mA | reads **0 / 0 / 0 / 1** (GPIO2 reads 1 at every step) |

**Reading:** something on the net pulls IO3 low through roughly 75-150 ohms, which is not a dead short (a short would
stay 0 at 40 mA) and not a weak pull-down. An output stage sitting low from power-up fits (QMI8658 INT1 driven low
and ignoring `CTRL1`), as would a part or trace to GND that is not on the schematic. On the ESP32 side GPIO3 is not a
strapping or JTAG pin on this board (`STRAP_JTAG_SEL` eFuse is 0), not a flash, PSRAM or USB pin, and no function is
assigned to it. Driving it at 40 mA reaches the pad, so the load is external.

**Questions for Waveshare:**
- Does the production PCB match the published schematic around U3 pin 4 / U2 pin 8 (IO3)? Is there a pull-down or a
  component on `IMU_INT1` that isn't in the PDF?
- Does INT1 work on any board you have? If so, what is different about it?
- Is U3 the QMI8658C or the A on shipped boards? (`CTRL1` bit 3 enables INT1 on the A and is reserved on the C.)
- Is the ghost-touch fault above related (both are on the shared 3V3 / I2C parts of the board)?

**To test it:** `imu_int1_test/imu_int1_test.ino` needs no libraries (Arduino core only, `Wire` on SDA 48 / SCL 47) and
prints the steps above over serial at 115200. **Unplug USB and replug before running**, so the IMU is at power-on defaults
for step 1. Board *ESP32S3 Dev Module*, *USB CDC On Boot: Enabled*. Shake the board during step 5. Healthy boards should
read 1 on IO3 with the pull-up (before the IMU is configured) and show edges in steps 4 and 5. (The sketch compiles with
arduino-esp32; the measurements above are from my ESP-IDF firmware.)

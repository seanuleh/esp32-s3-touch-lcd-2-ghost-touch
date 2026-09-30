# ESP32-S3-Touch-LCD-2 ghost touch test

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

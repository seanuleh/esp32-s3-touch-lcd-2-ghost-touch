// Ghost-touch test for the Waveshare ESP32-S3-Touch-LCD-2.
// Uses Waveshare's own demo display setup (08_lvgl_example) and touch driver (bsp_cst816),
// no LVGL, no WiFi. Every touch sample leaves a yellow dot, every new contact a red ring, and
// the header counts contacts, so if you leave the board untouched the screen is the log.
// Serial (115200) prints one line per contact.
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "bsp_cst816.h"

// bsp_cst816.cpp's register read helper (not in its header).
bool bsp_touch_i2c_reg8_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint8_t len);

#define BL_PCT 60

#define EXAMPLE_PIN_NUM_LCD_SCLK 39
#define EXAMPLE_PIN_NUM_LCD_MOSI 38
#define EXAMPLE_PIN_NUM_LCD_MISO 40
#define EXAMPLE_PIN_NUM_LCD_DC 42
#define EXAMPLE_PIN_NUM_LCD_RST -1
#define EXAMPLE_PIN_NUM_LCD_CS 45
#define EXAMPLE_PIN_NUM_LCD_BL 1
#define EXAMPLE_PIN_NUM_TP_SDA 48
#define EXAMPLE_PIN_NUM_TP_SCL 47
#define EXAMPLE_LCD_ROTATION 0  // portrait, USB port at the bottom
#define EXAMPLE_LCD_H_RES 240
#define EXAMPLE_LCD_V_RES 320

Arduino_DataBus *bus = new Arduino_ESP32SPI(EXAMPLE_PIN_NUM_LCD_DC, EXAMPLE_PIN_NUM_LCD_CS, EXAMPLE_PIN_NUM_LCD_SCLK,
                                            EXAMPLE_PIN_NUM_LCD_MOSI, EXAMPLE_PIN_NUM_LCD_MISO);
Arduino_GFX *gfx = new Arduino_ST7789(bus, EXAMPLE_PIN_NUM_LCD_RST, EXAMPLE_LCD_ROTATION, true,
                                      EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);

static uint32_t contacts;
static bool down;
static uint32_t down_ms, last_hdr_ms;
static uint16_t first_x, first_y, last_x, last_y;

static void header(void)
{
  uint32_t s = millis() / 1000;
  gfx->fillRect(0, 0, EXAMPLE_LCD_H_RES, 20, RGB565_BLACK);
  gfx->setCursor(4, 4);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(1);
  gfx->printf("up %02lu:%02lu:%02lu  contacts %lu", s / 3600, s / 60 % 60, s % 60, contacts);
}

// Read-only dump of the CST816's id and config registers (send 'r' over serial).
static void reg_dump(void)
{
  static const uint8_t regs[] = { 0xA7, 0xA8, 0xA9, 0xB0, 0xB1, 0xB2, 0xB3, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1,
                                  0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE };
  Serial.printf("%lu ms: regs", millis());
  for (uint8_t r : regs) {
    uint8_t v;
    if (bsp_touch_i2c_reg8_read(CST816_ADDR, r, &v, 1)) Serial.printf(" %02X=%02X", r, v);
    else Serial.printf(" %02X=--", r);
  }
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  gfx->begin();
  gfx->fillScreen(RGB565_BLACK);
  ledcAttach(EXAMPLE_PIN_NUM_LCD_BL, 5000, 10);
  ledcWrite(EXAMPLE_PIN_NUM_LCD_BL, (1 << 10) / 100 * BL_PCT);
  Wire.begin(EXAMPLE_PIN_NUM_TP_SDA, EXAMPLE_PIN_NUM_TP_SCL);
  bsp_touch_init(&Wire, gfx->getRotation(), gfx->width(), gfx->height());
  gfx->drawRect(0, 20, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES - 20, RGB565_DARKGREY);
  header();
  Serial.println("touch_paint: ready, one line per contact ('r' dumps the touch chip's registers)");
  reg_dump();
}

void loop()
{
  uint16_t x, y;
  uint32_t now = millis();
  bsp_touch_read();
  if (bsp_touch_get_coordinates(&x, &y)) {
    if (!down) {
      down = true;
      down_ms = now;
      first_x = x;
      first_y = y;
      contacts++;
      gfx->drawCircle(x, y, 6, RGB565_RED);
      header();
    }
    gfx->fillCircle(x, y, 2, RGB565_YELLOW);
    last_x = x;
    last_y = y;
  } else if (down) {
    down = false;
    Serial.printf("%lu ms: contact %lu, %lu ms, (%u,%u) -> (%u,%u)\n", down_ms, contacts, now - down_ms, first_x,
                  first_y, last_x, last_y);
  }
  if (now - last_hdr_ms >= 1000) {
    last_hdr_ms = now;
    header();
  }
  while (Serial.available())
    if (Serial.read() == 'r') reg_dump();
  delay(10);
}

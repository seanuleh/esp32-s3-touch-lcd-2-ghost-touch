// ESP32-S3-Touch-LCD-2: does the QMI8658 INT1 line reach GPIO3?
//
// Schematic: IO3 = net IMU_INT1 = U3 (QMI8658C) pin 4. On my two boards IO3 is held low from
// power-up, whatever the IMU is told to do, so wake on motion never produces an edge.
// Needs nothing but the Arduino core (Wire). No display, no touch, no WiFi. Serial 115200.
// Power-cycle the board (unplug USB) before running: step 1 needs the IMU at its power-on state.
//
// Step 1  IO3 BEFORE any I2C: pull-up / pull-down / drive-high ladder. GPIO2 (a free pin) is the
//         control: it must read 1 with the pull-up and 1 when driven high.
// Step 2  find the QMI8658 (0x6B, then 0x6A), read WHO_AM_I (0x05) and REVISION.
// Step 3  INT1 disabled (CTRL1 0x40), then after a soft reset: IO3 with pull-up, drive ladder again.
// Step 4  wake on motion armed at 255 mg (nothing in the room moves that much) with each CAL1_H[7:6]
//         encoding (INT select + initial level): IO3 should follow the initial level. It never moves on my boards.
// Step 5  wake on motion at 1 mg, all four CAL1_H[7:6] encodings: shake the board. The WoM flag
//         (STATUS1 bit 2) sets every time; IO3 edges stay 0.
#include <Arduino.h>
#include <Wire.h>

#define PIN_SDA 48
#define PIN_SCL 47
#define PIN_INT1 3
#define PIN_CONTROL 2

#define REG_WHO 0x00
#define REG_REV 0x01
#define REG_CTRL1 0x02
#define REG_CTRL2 0x03
#define REG_CTRL7 0x08
#define REG_CTRL9 0x0A
#define REG_CAL1_L 0x0B
#define REG_CAL1_H 0x0C
#define REG_STATUSINT 0x2D
#define REG_STATUS1 0x2F
#define REG_RESET 0x60

static uint8_t addr;
static volatile uint32_t edges;
static void IRAM_ATTR on_edge() { edges++; }

static bool wr(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}
static uint8_t rd(uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((int)addr, 1);
  return Wire.available() ? Wire.read() : 0xFF;
}

// CTRL9 command handshake (QMI8658 datasheet 9.4): write the command, wait for CmdDone, ACK.
static bool ctrl9(uint8_t cmd) {
  wr(REG_CTRL9, cmd);
  for (int i = 0; i < 100; i++) {
    delay(2);
    if (rd(REG_STATUSINT) & 0x80 || rd(REG_STATUS1) & 0x01) {  // QMI8658A / C CmdDone
      wr(REG_CTRL9, 0x00);
      return true;
    }
  }
  return false;
}

static void reads(int pin, const char *what) {
  int up, down;
  pinMode(pin, INPUT_PULLUP);
  delay(2);
  up = digitalRead(pin);
  pinMode(pin, INPUT_PULLDOWN);
  delay(2);
  down = digitalRead(pin);
  pinMode(pin, INPUT);
  Serial.printf("  GPIO%d %-34s pull-up reads %d, pull-down reads %d\n", pin, what, up, down);
}

// Drive the pin high for 20 us at each drive strength (~5/10/20/40 mA). A dead short stays 0 at
// 40 mA; a push-pull output sitting low loses to the stronger drives.
static void ladder(int pin, const char *what) {
  int r[4];
  for (int cap = 0; cap < 4; cap++) {
    pinMode(pin, OUTPUT);
    gpio_set_drive_capability((gpio_num_t)pin, (gpio_drive_cap_t)cap);
    digitalWrite(pin, HIGH);
    delayMicroseconds(20);
    pinMode(pin, INPUT);  // read back with the driver off, 5 us later
    r[cap] = digitalRead(pin);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    pinMode(pin, INPUT);
    delayMicroseconds(200);
  }
  Serial.printf("  GPIO%d %-34s driven high reads: 5 mA %d, 10 mA %d, 20 mA %d, 40 mA %d\n", pin, what, r[0], r[1], r[2], r[3]);
}

static void wom(uint8_t mg, uint8_t cal1h, uint8_t ctrl1, uint32_t ms, const char *what) {
  wr(REG_CTRL7, 0x00);
  wr(REG_CTRL2, 0x1E);  // +-4 g, 11 Hz low-power accel
  wr(REG_CAL1_L, mg);
  wr(REG_CAL1_H, cal1h);
  bool ok = ctrl9(0x08);  // CTRL_CMD_WRITE_WOM_SETTING
  wr(REG_CTRL1, ctrl1);
  wr(REG_CTRL7, 0x01);
  rd(REG_STATUS1);  // clear
  edges = 0;
  attachInterrupt(PIN_INT1, on_edge, CHANGE);
  uint32_t t0 = millis(), flag = 0, high = 0, n = 0;
  while (millis() - t0 < ms) {
    if (rd(REG_STATUS1) & 0x04) flag++;
    high += digitalRead(PIN_INT1);
    n++;
    delay(10);
  }
  detachInterrupt(PIN_INT1);
  Serial.printf("  %-30s cmd %s | IO3 edges %lu, high %lu/%lu polls, WoM flag %lu polls\n", what, ok ? "ok" : "FAILED", (unsigned long)edges, (unsigned long)high, (unsigned long)n, (unsigned long)flag);
  wr(REG_CTRL7, 0x00);
  wr(REG_CAL1_L, 0x00);
  ctrl9(0x08);
  wr(REG_CTRL1, 0x40);
}

void setup() {
  Serial.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 4000) delay(10);
  delay(500);
  Serial.println("\n=== QMI8658 INT1 -> GPIO3 test ===");

  Serial.println("Step 1: before any I2C traffic (IMU at power-on defaults)");
  reads(PIN_CONTROL, "CONTROL (free pin)");
  reads(PIN_INT1, "IMU_INT1");
  ladder(PIN_CONTROL, "CONTROL (free pin)");
  ladder(PIN_INT1, "IMU_INT1");

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  const uint8_t addrs[] = {0x6B, 0x6A};
  for (uint8_t a : addrs) {
    addr = a;
    if (rd(REG_WHO) == 0x05) break;
    addr = 0;
  }
  if (!addr) {
    Serial.println("Step 2: no QMI8658 found at 0x6B/0x6A, stopping");
    return;
  }
  Serial.printf("Step 2: QMI8658 at 0x%02X, WHO_AM_I 0x%02X, REVISION 0x%02X\n", addr, rd(REG_WHO), rd(REG_REV));

  Serial.println("Step 3: INT1 disabled (CTRL1 0x40), then after a soft reset");
  wr(REG_CTRL1, 0x40);
  reads(PIN_INT1, "IMU_INT1, INT1 disabled");
  ladder(PIN_INT1, "IMU_INT1, INT1 disabled");
  wr(REG_RESET, 0xB0);
  delay(50);
  wr(REG_CTRL1, 0x40);
  reads(PIN_INT1, "IMU_INT1, after soft reset");
  ladder(PIN_INT1, "IMU_INT1, after soft reset");

  Serial.println("Step 4: 255 mg threshold (no motion), each CAL1_H[7:6]: IO3 should follow the initial level");
  wom(255, 0x02, 0x58, 3000, "CAL1_H[7:6]=00 (bit 6 = level)");
  wom(255, 0x42, 0x58, 3000, "CAL1_H[7:6]=01");
  wom(255, 0x82, 0x58, 3000, "CAL1_H[7:6]=10");
  wom(255, 0xC2, 0x58, 3000, "CAL1_H[7:6]=11");

  Serial.println("Step 5: SHAKE THE BOARD, 4 x 8 s, 1 mg threshold");
  wom(1, 0x02, 0x58, 8000, "CAL1_H[7:6]=00");
  wom(1, 0x42, 0x58, 8000, "CAL1_H[7:6]=01");
  wom(1, 0x82, 0x58, 8000, "CAL1_H[7:6]=10");
  wom(1, 0xC2, 0x58, 8000, "CAL1_H[7:6]=11");
  Serial.println("=== done ===");
}

void loop() { delay(1000); }

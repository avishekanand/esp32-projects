// =============================================================================
// display_test.ino  —  LCDWIKI ES3C35P  (ESP32-S3) bring-up
// 3.5" IPS QSPI panel + integrated touch, Sitronix ST77922, 320x480 RGB565
//
// Proves: backlight ON, ST77922 QSPI init, solid color fills (R,G,B,W),
//         rectangles + text, and a Protocol-A touch read over I2C (addr 0x55).
//
// LIBRARIES (install exactly these):
//   arduino-cli lib install "GFX Library for Arduino"    # moononournation, >= 1.5.5
//   (Wire is bundled with the esp32:esp32@3.3.12 core — no install needed)
//
// BUILD / FLASH (your confirmed toolchain):
//   FQBN: esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,\
//         PartitionScheme=app3M_fat9M_16MB,PSRAM=opi
//   arduino-cli compile --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,\
//         FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi" display_test
//   arduino-cli upload  -p /dev/cu.usbmodemXXXX --fqbn "...same..." display_test
//
// PSRAM (opi) is required: a full 320x480x2 = 300 KB framebuffer (Arduino_Canvas)
// is allocated there so every panel write is a single 4-pixel-aligned full frame
// (sidesteps the ST77922 draw-window alignment constraint — see notes at bottom).
// =============================================================================

#include <Arduino_GFX_Library.h>
#include <Wire.h>

// Color name aliases (this Arduino_GFX version prefixes constants with RGB565_).
#ifndef BLACK
#define BLACK  RGB565_BLACK
#endif
#ifndef WHITE
#define WHITE  RGB565_WHITE
#endif
#ifndef YELLOW
#define YELLOW RGB565_YELLOW
#endif
#ifndef RED
#define RED    RGB565_RED
#endif
#ifndef GREEN
#define GREEN  RGB565_GREEN
#endif
#ifndef BLUE
#define BLUE   RGB565_BLUE
#endif

// ---------------- Pins (confirmed board facts) --------------------------------
// LCD QSPI
#define LCD_CS   10
#define LCD_SCK  12
#define LCD_D0   11   // IO0 / MOSI
#define LCD_D1   13   // IO1 / MISO
#define LCD_D2   14   // IO2 / WP
#define LCD_D3    9   // IO3 / HD
#define LCD_BL   41   // backlight, active HIGH, PWM-capable
// Touch (integrated in ST77922, shared I2C bus with ES8311 @0x18)
#define TP_SDA   38
#define TP_SCL   39
#define TP_INT   47
#define TP_RST   48
#define TP_ADDR  0x55 // 7-bit (vendor PDF prints a wrong value; 0x55 is correct)

#define LCD_W 320
#define LCD_H 480
#define QSPI_HZ 40000000UL

// MADCTL bits
#define MADCTL_MY 0x80
#define MADCTL_MX 0x40
#define MADCTL_MV 0x20

// =============================================================================
// ST77922 vendor QSPI init table.
// Copied VERBATIM from ydedox/st77922 ->
//   Example_01_Simple_test/Simple_test/Simple_test.ino  (array: st77922_lcd_init)
// Cross-checked against andrewscherpbier/esp32-s3-display
//   components/es3c35p_bsp/bsp_display.c (same Espressif esp_lcd_st77922 table:
//   0x21 INVON, 0x11 SLPOUT+120ms, 0x29 DISPON, 0x3A=0x01, 0x36=0x00, 0x35 TEON).
// Do not edit the byte values — they are the factory-decoded power/gamma/gate table.
// =============================================================================
typedef struct { uint8_t cmd; const uint8_t *data; uint8_t len; uint8_t delay_ms; } st_cmd_t;

static const st_cmd_t ST77922_INIT[] = {
  {0xF1, (const uint8_t[]){0x00}, 1, 0},
  {0x60, (const uint8_t[]){0x00,0x00,0x00}, 3, 0},
  {0x65, (const uint8_t[]){0x80}, 1, 0},
  {0x79, (const uint8_t[]){0x06}, 1, 0},
  {0x7B, (const uint8_t[]){0x00,0x08,0x08}, 3, 0},
  {0x80, (const uint8_t[]){0x55,0x62,0x2F,0x17,0xF0,0x52,0x70,0xD2,0x52,0x62,0xEA}, 11, 0},
  {0x81, (const uint8_t[]){0x26,0x52,0x72,0x27}, 4, 0},
  {0x84, (const uint8_t[]){0x92,0x25}, 2, 0},
  {0x87, (const uint8_t[]){0x10,0x10,0x58,0x00,0x02,0x3A}, 6, 0},
  {0x88, (const uint8_t[]){0x00,0x00,0x2C,0x10,0x04,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x00,0x06}, 15, 0},
  {0x89, (const uint8_t[]){0x00,0x00,0x00}, 3, 0},
  {0x8A, (const uint8_t[]){0x13,0x00,0x2C,0x00,0x00,0x2C,0x10,0x10,0x00,0x3E,0x19}, 11, 0},
  {0x8B, (const uint8_t[]){0x15,0xB1,0xB1,0x44,0x96,0x2C,0x10,0x97,0x8E}, 9, 0},
  {0x8C, (const uint8_t[]){0x1D,0xB1,0xB1,0x44,0x96,0x2C,0x10,0x50,0x0F,0x01,0xC5,0x12,0x09}, 13, 0},
  {0x8D, (const uint8_t[]){0x0C}, 1, 0},
  {0x8E, (const uint8_t[]){0x33,0x01,0x0C,0x13,0x01,0x01}, 6, 0},
  {0xB3, (const uint8_t[]){0x00,0x30}, 2, 0},
  {0xF1, (const uint8_t[]){0x00}, 1, 0},
  {0x71, (const uint8_t[]){0xD0}, 1, 0},
  {0x66, (const uint8_t[]){0x02,0x3F}, 2, 0},
  {0xBE, (const uint8_t[]){0x26,0x00,0x9D}, 3, 0},
  {0x70, (const uint8_t[]){0x01,0xA0,0x11,0x40,0xE0,0x00,0x11,0x69,0x11,0x00,0x00,0x1A}, 12, 0},
  {0x90, (const uint8_t[]){0x04,0x04,0x55,0x74,0x00,0x40,0x43,0x27,0x27}, 9, 0},
  {0x91, (const uint8_t[]){0x04,0x04,0x55,0x75,0x00,0x40,0x42,0x27,0x27}, 9, 0},
  {0x92, (const uint8_t[]){0x04,0x44,0x55,0xC0,0x06,0x00,0x07,0x05,0x90,0x27}, 10, 0},
  {0x93, (const uint8_t[]){0x04,0x43,0x11,0x00,0x00,0x00,0x00,0x05,0x90,0x27}, 10, 0},
  {0x94, (const uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00}, 6, 0},
  {0x95, (const uint8_t[]){0x96,0x16,0x00,0x00,0xFF}, 5, 0},
  {0x96, (const uint8_t[]){0x44,0x53,0x03,0x12,0x23,0x24,0x06,0x05,0x94,0x27,0x00,0x44}, 12, 0},
  {0x97, (const uint8_t[]){0x44,0x53,0x47,0x56,0x20,0x20,0x02,0x01,0x94,0x27,0x00,0x44}, 12, 0},
  {0xBA, (const uint8_t[]){0x55,0x94,0x2D,0x94,0x27}, 5, 0},
  {0x9A, (const uint8_t[]){0x40,0x00,0x06,0x00,0x00,0x00,0x00}, 7, 0},
  {0x9B, (const uint8_t[]){0x00,0x00,0x06,0x00,0x00,0x00,0x00}, 7, 0},
  {0x9C, (const uint8_t[]){0x5C,0x12,0x00,0x00,0x10,0x12,0x00,0x00,0x10,0x02,0x00,0x00,0x00}, 13, 0},
  {0x9D, (const uint8_t[]){0x8A,0x51,0x00,0x00,0x00,0x80,0x1E,0x01}, 8, 0},
  {0x9E, (const uint8_t[]){0x51,0x00,0x00,0x00,0x80,0x1E,0x01}, 7, 0},
  {0xB4, (const uint8_t[]){0x1D,0x1C,0x1E,0x0B,0x14,0x02,0x13,0x09,0x1E,0x00,0x1E,0x10}, 12, 0},
  {0xB5, (const uint8_t[]){0x1D,0x1C,0x1E,0x0A,0x15,0x03,0x11,0x08,0x1E,0x01,0x1E,0x12}, 12, 0},
  {0xB6, (const uint8_t[]){0x77,0x77,0x00,0x0A,0xFF,0x0A,0xFF}, 7, 0},
  {0x86, (const uint8_t[]){0xCD,0x04,0xB1,0x02,0x58,0x12,0x58,0x0C,0x13,0x01,0xA5,0x00,0xA5,0xA5}, 14, 0},
  {0xB7, (const uint8_t[]){0x07,0x0A,0x0E,0x06,0x05,0x03,0x2B,0x03,0x03,0x42,0x07,0x10,0x10,0x2E,0x3F,0x0D}, 16, 0},
  {0xB8, (const uint8_t[]){0x07,0x0A,0x0D,0x05,0x05,0x02,0x2B,0x02,0x03,0x42,0x06,0x10,0x0F,0x2E,0x3F,0x0D}, 16, 0},
  {0xB9, (const uint8_t[]){0x23,0x23}, 2, 0},
  {0xBF, (const uint8_t[]){0x10,0x14,0x14,0x0B,0x0B,0x0B}, 6, 0},
  {0xF2, (const uint8_t[]){0x00}, 1, 0},
  {0x73, (const uint8_t[]){0x04,0xDA,0x12,0x54,0x47}, 5, 0},
  {0x77, (const uint8_t[]){0x6B,0x5B,0xFD,0xC3,0xC5}, 5, 0},
  {0x7A, (const uint8_t[]){0x15,0x27}, 2, 0},
  {0x7B, (const uint8_t[]){0x04,0x57}, 2, 0},
  {0x7E, (const uint8_t[]){0x01,0x0E}, 2, 0},
  {0xBF, (const uint8_t[]){0x36}, 1, 0},
  {0xE3, (const uint8_t[]){0x40,0x40}, 2, 0},
  {0xF0, (const uint8_t[]){0x00}, 1, 0},
  {0xD0, (const uint8_t[]){0x00}, 1, 0},
  {0x2A, (const uint8_t[]){0x00,0x00,0x01,0x3F}, 4, 0},  // CASET 0..319
  {0x2B, (const uint8_t[]){0x00,0x00,0x01,0xDF}, 4, 0},  // RASET 0..479
  {0x21, NULL, 0, 0},                                    // INVON  (inversion ON)
  {0x11, NULL, 0, 120},                                  // SLPOUT + 120 ms
  {0x29, NULL, 0, 0},                                    // DISPON
  {0x2C, NULL, 0, 0},                                    // RAMWR
  {0x3A, (const uint8_t[]){0x01}, 1, 0},                 // COLMOD RGB565 (QSPI code)
  {0x36, (const uint8_t[]){0x00}, 1, 0},                 // MADCTL = 0x00 -> RGB, top-left
  {0x35, (const uint8_t[]){0x01}, 1, 20},                // TEON + 20 ms
};
static const size_t ST77922_INIT_LEN = sizeof(ST77922_INIT) / sizeof(ST77922_INIT[0]);

// =============================================================================
// Custom Arduino_GFX panel class for the ST77922.
// Arduino_GFX has no ST77922 class, so this subclasses Arduino_TFT and reuses
// the Arduino_ESP32QSPI databus (identical QSPI framing to the ST77916 sibling:
// command phase = 0x02 + (cmd<<8) on single line; pixel data = 0x32 + 0x3C00 on
// quad lines). The pixel path (writeAddrWindow/RAMWR) is cloned from the known-
// good Arduino_ST77916; only the init table and 320x480 geometry differ.
// =============================================================================
class Arduino_ST77922 : public Arduino_TFT {
public:
  Arduino_ST77922(Arduino_ESP32QSPI *bus, uint8_t r = 0)
    : Arduino_TFT((Arduino_DataBus *)bus, GFX_NOT_DEFINED /*rst: tied to EN, use SW reset*/,
                  r, true /*ips*/, LCD_W, LCD_H, 0, 0, 0, 0),
      _qbus(bus) {}

  bool begin(int32_t speed = GFX_NOT_DEFINED) override { return Arduino_TFT::begin(speed); }

  void tftInit() override {
    // Panel RESET is tied to chip EN (no GPIO) -> issue a software reset.
    _qbus->writeCommand(0x01);  // SWRESET
    delay(120);
    for (size_t i = 0; i < ST77922_INIT_LEN; i++) {
      const st_cmd_t *c = &ST77922_INIT[i];
      if (c->len) _qbus->writeC8Bytes(c->cmd, (uint8_t *)c->data, c->len);
      else        _qbus->writeCommand(c->cmd);
      if (c->delay_ms) delay(c->delay_ms);
    }
  }

  void writeAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) override {
    if ((x != _currentX) || (w != _currentW)) {
      _currentX = x; _currentW = w; x += _xStart;
      _qbus->writeC8D16D16(0x2A, x, x + w - 1);   // CASET
    }
    if ((y != _currentY) || (h != _currentH)) {
      _currentY = y; _currentH = h; y += _yStart;
      _qbus->writeC8D16D16(0x2B, y, y + h - 1);   // RASET
    }
    _qbus->writeCommand(0x2C);                    // RAMWR
  }

  void setRotation(uint8_t r) override {
    Arduino_TFT::setRotation(r);
    uint8_t m;
    switch (_rotation) {
      case 1:  m = MADCTL_MX | MADCTL_MV; break;  // landscape
      case 2:  m = MADCTL_MX | MADCTL_MY; break;  // portrait flipped
      case 3:  m = MADCTL_MY | MADCTL_MV; break;  // landscape flipped
      default: m = 0x00;                 break;   // 0: native portrait, RGB order
    }
    _qbus->writeC8D8(0x36, m);
  }

  void invertDisplay(bool i) override {
    _qbus->writeCommand((_ips ^ i) ? 0x21 /*INVON*/ : 0x20 /*INVOFF*/);
  }
  void displayOn()  override { _qbus->writeCommand(0x11); delay(120); _qbus->writeCommand(0x29); }
  void displayOff() override { _qbus->writeCommand(0x28); _qbus->writeCommand(0x10); delay(120); }

protected:
  Arduino_ESP32QSPI *_qbus;
};

// ---------------- Objects -----------------------------------------------------
// Arduino_ESP32QSPI(cs, sck, d0, d1, d2, d3)
Arduino_ESP32QSPI *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_ST77922 *panel = new Arduino_ST77922(bus, 0 /*rotation 0 = 320x480 portrait*/);
// Full-frame framebuffer in PSRAM -> every flush() is a single 4px-aligned write.
Arduino_Canvas *gfx = new Arduino_Canvas(LCD_W, LCD_H, panel);

// =============================================================================
// Touch — Sitronix "Protocol A" over I2C (decoded from clackups/draftling PR #83
// ST77922 poll routine, which follows Freenove's ST77922_Touch.cpp):
//   - 16-bit register address, sent MSB first, repeated-start before read.
//   - Reg 0x0010 = touch info/status (bit3 = new data ready).
//   - Reg 0x0014 = point 0; 7 bytes per point, up to 5 points.
//       byte0: bit7 = touch-down flag, bits[5:0] = X high bits
//       byte1: X low 8 bits        -> X = ((b0 & 0x3F) << 8) | b1
//       byte2: bits[5:0] = Y high bits
//       byte3: Y low 8 bits        -> Y = ((b2 & 0x3F) << 8) | b3
//   NOTE: always read the full 7-byte block or the controller sticks on its
//         first report.
// =============================================================================
static bool tp_read_reg(uint16_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(TP_ADDR);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;   // repeated start
  uint8_t n = Wire.requestFrom((uint8_t)TP_ADDR, len);
  if (n < len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

// Returns true if a touch is present; fills x,y with the first point (native coords).
// Protocol (verified against Freenove ST77922_Touch + Espressif esp_lcd_touch_st7123/
// st77926): read ONE contiguous block starting at TOUCH_INFO(0x0010) — 4-byte header
// then 7 bytes/point — in a single I2C transaction. Reading 0x0010 and 0x0014 as two
// separate transactions makes the controller stick on its first report.
//   block[0] = TOUCH_INFO, bit 0x08 = new coordinate data ready.
//   block[4] = TOUCH_POINT0: byte0 bit7 = finger down; x/y = 6 high + 8 low bits.
bool touchRead(uint16_t *x, uint16_t *y) {
  uint8_t blk[4 + 7 * 5];                                // header + up to 5 points
  if (!tp_read_reg(0x0010, blk, sizeof(blk))) return false;
  if (!(blk[0] & 0x08)) return false;                    // new coordinate data ready
  const uint8_t *p0 = &blk[4];                           // point 0 == reg 0x0014
  if (!(p0[0] & 0x80)) return false;                     // byte0 bit7: finger down
  *x = ((uint16_t)(p0[0] & 0x3F) << 8) | p0[1];
  *y = ((uint16_t)(p0[2] & 0x3F) << 8) | p0[3];
  return true;
}

void touchInit() {
  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);  delay(10);
  digitalWrite(TP_RST, HIGH); delay(80);                  // release touch reset
  pinMode(TP_INT, INPUT_PULLUP);
  Wire.begin(TP_SDA, TP_SCL, 400000);                     // 400 kHz, shared w/ ES8311
  // Wait for controller ready: STATUS(0x0001) low nibble == 0 (Freenove handshake).
  for (int i = 0; i < 16; i++) {
    uint8_t st;
    if (tp_read_reg(0x0001, &st, 1) && (st & 0x0F) == 0) break;
    delay(10);
  }
}

// ---------------- Demo ---------------------------------------------------------
void showColor(uint16_t c, const char *name) {
  gfx->fillScreen(c);
  gfx->setTextColor((c == WHITE) ? BLACK : WHITE);
  gfx->setTextSize(3);
  gfx->setCursor(20, 20);
  gfx->println(name);
  gfx->flush();           // single full-frame, 4px-aligned push over QSPI
}

void setup() {
  Serial.begin(115200);

  // Backlight ON (active HIGH). Use LEDC PWM so brightness is adjustable.
  ledcAttach(LCD_BL, 5000 /*Hz*/, 8 /*bit*/);
  ledcWrite(LCD_BL, 255);   // full brightness
  // (simple alternative: pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, HIGH);)

  if (!gfx->begin(QSPI_HZ)) {
    Serial.println("gfx->begin() failed (PSRAM canvas alloc? check PSRAM=opi)");
    while (1) delay(100);
  }

  touchInit();

  // Color sweep
  showColor(RED,   "RED");   delay(1000);
  showColor(GREEN, "GREEN"); delay(1000);
  showColor(BLUE,  "BLUE");  delay(1000);
  showColor(WHITE, "WHITE"); delay(1000);

  // Rectangles + text (dimensions kept multiples of 4 for the panel)
  gfx->fillScreen(BLACK);
  gfx->fillRect(20, 60, 280, 80, RED);
  gfx->fillRect(20, 160, 280, 80, GREEN);
  gfx->fillRect(20, 260, 280, 80, BLUE);
  gfx->drawRect(8, 8, 304, 464, WHITE);
  gfx->setTextColor(WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(24, 380);
  gfx->println("ST77922 QSPI OK");
  gfx->setCursor(24, 410);
  gfx->println("Touch the screen");
  gfx->flush();
}

void loop() {
  uint16_t x, y;
  if (touchRead(&x, &y)) {
    Serial.printf("touch: x=%u y=%u\n", x, y);
    gfx->fillCircle(x, y, 6, YELLOW);   // draw into canvas
    gfx->flush();                       // push full frame
  }
  delay(15);
}

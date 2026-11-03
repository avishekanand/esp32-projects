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
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP_I2S.h>
#include "AudioBoard.h"
#include "wifi_secrets.h"

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

// =============================================================================
// Chatbot STT app: tap-to-start/stop recording -> POST PCM to Mac -> show text.
// =============================================================================

// ---- Config ----
#define SERVER_BASE  "http://192.168.86.134:8080"       // Mac dev server (LAN)
#define SERVER_STT      SERVER_BASE "/stt"
#define SERVER_CHAT     SERVER_BASE "/chat"
#define SERVER_TTS      SERVER_BASE "/tts"
#define SERVER_CONVERSE SERVER_BASE "/converse"   // streaming LLM+TTS (framed)
#define AMP_ENABLE   1          // ES8311 amp enable (active LOW); keep OFF for mic
// Mic I2S pins (verified in 05_Microphone_Record)
#define I2S_MCLK 17
#define I2S_BCLK 18
#define I2S_WS   21
#define I2S_DOUT 15
#define I2S_DIN  16
static const int SR = 24000;            // 24 kHz (full Kokoro fidelity; mic + speaker)
static const int MAX_SEC = 10;          // cap recording length
static const size_t MAX_PCM = (size_t)SR * 2 * MAX_SEC;  // mono int16 bytes (~480KB)

// ---- UI geometry ----
static const int BTN_X = 40, BTN_Y = 340, BTN_W = 240, BTN_H = 100;  // mults of 4

AudioBoard codec(AudioDriverES8311, NoPins);
I2SClass   i2s;

enum State { ST_IDLE, ST_RECORDING, ST_UPLOADING, ST_CONVERSE, ST_RESULT };
State    state = ST_IDLE;
uint8_t *pcm   = nullptr;        // PSRAM record buffer
size_t   pcmLen = 0;
int      level  = 0;             // current mic level 0..100 (for animation)
String   userText  = "";         // last transcription
String   replyText = "";         // last LLM reply
String   status    = "connecting wifi...";
int      replyScroll  = 0;       // Pip box scroll offset (in lines)
int      g_replyLines = 0;       // total wrapped lines of current reply
bool     g_touchDown  = false;   // shared per-loop touch sample
uint16_t g_tx = 0, g_ty = 0;

// Pip (reply) box geometry — shared by the drawer and the scroll handler
#define  TXT_SIZE 2
static const int LINE_H = 8 * TXT_SIZE + 3;      // 19 px per line
#define  PIP_X 24
#define  PIP_Y 232
#define  PIP_W 272
#define  PIP_H 94
static const int PIP_VISIBLE = PIP_H / LINE_H;   // visible reply lines
#define  MAX_WRAP_LINES 120
String   g_lines[MAX_WRAP_LINES];

// ---------- helpers ----------
static bool inButton(uint16_t x, uint16_t y) {
  return x >= BTN_X && x < BTN_X + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H;
}

// Word-wrap `txt` into g_lines[] at `maxChars` per line; returns line count.
// Long words are hard-split. Capped at MAX_WRAP_LINES.
int wrapIntoLines(const String &txt, int maxChars) {
  int count = 0;
  String line = "";
  auto push = [&](const String &l) { if (count < MAX_WRAP_LINES) g_lines[count++] = l; };
  int i = 0, n = txt.length();
  while (i < n && count < MAX_WRAP_LINES) {
    int sp = txt.indexOf(' ', i);
    String word = (sp < 0) ? txt.substring(i) : txt.substring(i, sp);
    i = (sp < 0) ? n : sp + 1;
    while ((int)word.length() > maxChars) {
      if (line.length()) { push(line); line = ""; }
      push(word.substring(0, maxChars));
      word = word.substring(maxChars);
    }
    if (line.length() == 0) line = word;
    else if ((int)(line.length() + 1 + word.length()) <= maxChars) line += " " + word;
    else { push(line); line = word; }
  }
  if (line.length()) push(line);
  return count;
}

// Draw a scrollable window of wrapped text into (x,y,maxW,maxH). `scroll` is the
// first visible line. Draws a scrollbar on the right when content overflows.
// Returns the TOTAL number of wrapped lines (so the caller can clamp scroll).
int drawWrapped(const String &txt, int x, int y, int maxW, int maxH, uint8_t size, int scroll) {
  gfx->setTextSize(size);
  gfx->setTextColor(WHITE);
  const int charW = 6 * size, lineH = 8 * size + 3;
  int maxChars = maxW / charW; if (maxChars < 1) maxChars = 1;
  int visible  = maxH / lineH; if (visible < 1) visible = 1;
  int total = wrapIntoLines(txt, maxChars);
  int maxScroll = total > visible ? total - visible : 0;
  if (scroll < 0) scroll = 0;
  if (scroll > maxScroll) scroll = maxScroll;
  int cy = y;
  for (int k = scroll; k < scroll + visible && k < total; k++) {
    gfx->setCursor(x, cy); gfx->print(g_lines[k].c_str()); cy += lineH;
  }
  if (total > visible) {                      // scrollbar
    int barH = max(10, maxH * visible / total);
    int barY = y + (maxH - barH) * scroll / maxScroll;
    gfx->fillRect(x + maxW - 2, y, 3, maxH, BLACK);
    gfx->fillRect(x + maxW - 2, barY, 3, barH, RGB565_CYAN);
  }
  return total;
}

void drawUI() {
  gfx->fillScreen(BLACK);
  gfx->drawRect(6, 6, 308, 468, RGB565_DARKGREY);

  // Title
  gfx->setTextColor(RGB565_CYAN);
  gfx->setTextSize(3);
  gfx->setCursor(20, 24);
  gfx->println("Chatbot Dog");

  // Status line
  gfx->setTextColor(RGB565_YELLOW);
  gfx->setTextSize(2);
  gfx->setCursor(20, 70);
  gfx->println(status.c_str());

  gfx->setTextWrap(false);                       // we wrap manually

  // "You" box (transcription) — not scrollable (your own words, kept short)
  gfx->setTextColor(RGB565_GREEN); gfx->setTextSize(2);
  gfx->setCursor(20, 96);  gfx->println("You:");
  gfx->drawRect(16, 116, 288, 80, RGB565_DARKGREY);
  drawWrapped(userText, 24, 124, 272, 66, 2, 0);

  // "Pip" box (reply) — scrollable (drag anywhere above the button)
  gfx->setTextColor(RGB565_CYAN);  gfx->setTextSize(2);
  gfx->setCursor(20, 204); gfx->print("Pip:");
  if (g_replyLines > PIP_VISIBLE) {            // scroll indicator + hint
    int maxScroll = g_replyLines - PIP_VISIBLE;
    gfx->setTextColor(RGB565_DARKGREY);
    gfx->setCursor(104, 204);
    gfx->print(replyScroll > 0 ? "^" : " ");
    gfx->print(replyScroll < maxScroll ? " v  drag to scroll" : "    (end)");
  }
  gfx->drawRect(16, 224, 288, 108, RGB565_DARKGREY);
  g_replyLines = drawWrapped(replyText, PIP_X, PIP_Y, PIP_W, PIP_H, TXT_SIZE, replyScroll);

  // Mic-reactive animation while recording (pulsing circle over the empty boxes)
  if (state == ST_RECORDING) {
    int r = 18 + (level * 50) / 100;           // radius grows with level
    gfx->fillCircle(160, 250, r, RGB565_RED);
    gfx->fillCircle(160, 250, 12, RGB565_WHITE);
  }

  // Button
  bool idle = (state == ST_IDLE || state == ST_RESULT);
  uint16_t bcol = (state == ST_RECORDING) ? RGB565_RED
                : idle ? RGB565_GREEN : RGB565_DARKGREY;
  gfx->fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 16, bcol);
  gfx->setTextColor(BLACK);
  gfx->setTextSize(3);
  const char *label = (state == ST_RECORDING) ? "TAP TO STOP"
                    : idle ? "TAP TO TALK" : "...";
  int16_t tx = BTN_X + 20, ty = BTN_Y + 36;
  gfx->setCursor(tx, ty);
  gfx->println(label);

  gfx->flush();
}

// Sample the touch panel once per loop into shared globals.
void pollTouch() { g_touchDown = touchRead(&g_tx, &g_ty); }

// Edge-detected tap inside the button. Returns true once per press.
bool buttonTapped() {
  static bool wasDown = false;
  bool tap = (g_touchDown && !wasDown && inButton(g_tx, g_ty));
  wasDown = g_touchDown;
  return tap;
}

// Drag anywhere above the button scrolls Pip's reply. Call every loop in ST_RESULT.
void handleReplyScroll() {
  static int prevY = -1, accum = 0;
  bool inPip = g_touchDown && g_ty < BTN_Y - 4;   // whole upper area = scroll surface
  if (inPip) {
    if (prevY >= 0) {
      accum += (prevY - g_ty);          // finger up (g_ty decreases) -> scroll down
      const int STEP = 12;              // px of drag per line (smaller = more sensitive)
      bool changed = false;
      while (accum >= STEP)  { replyScroll++; accum -= STEP; changed = true; }
      while (accum <= -STEP) { replyScroll--; accum += STEP; changed = true; }
      int maxScroll = g_replyLines > PIP_VISIBLE ? g_replyLines - PIP_VISIBLE : 0;
      if (replyScroll < 0) replyScroll = 0;
      if (replyScroll > maxScroll) replyScroll = maxScroll;
      if (changed) drawUI();
    }
    prevY = g_ty;
  } else {
    prevY = -1; accum = 0;
  }
}

// Read one I2S chunk while recording: append mono samples, update `level`.
void recordChunk() {
  static int16_t buf[512 * 2];                 // 512 stereo frames
  size_t got = i2s.readBytes((char *)buf, sizeof(buf));
  int n = got / 2;                             // int16 count
  long peak = 0;
  for (int i = 0; i + 1 < n; i += 2) {
    int16_t s = buf[i];                        // left channel = mono mic
    if (abs(s) > peak) peak = abs(s);
    if (pcm && pcmLen + 2 <= MAX_PCM) {
      pcm[pcmLen++] = (uint8_t)(s & 0xFF);
      pcm[pcmLen++] = (uint8_t)((s >> 8) & 0xFF);
    }
  }
  level = (int)min(100L, peak * 100L / 20000L);
}

// Extract a top-level JSON string field value: {"key":"value"} -> value.
String jsonField(const String &body, const char *key) {
  String k = String("\"") + key + "\"";
  int i = body.indexOf(k);
  if (i < 0) return "";
  int colon = body.indexOf(':', i + k.length());
  if (colon < 0) return "";
  int q1 = body.indexOf('"', colon);
  if (q1 < 0) return "";
  // find closing quote, skipping escaped \"
  int q2 = q1 + 1;
  while (q2 < (int)body.length()) {
    if (body[q2] == '\\') { q2 += 2; continue; }
    if (body[q2] == '"') break;
    q2++;
  }
  String t = body.substring(q1 + 1, q2);
  t.replace("\\\"", "\""); t.replace("\\n", " "); t.replace("\\", "");
  return t;
}

String postAudio() {
  if (WiFi.status() != WL_CONNECTED) return "(wifi down)";
  HTTPClient http;
  http.begin(SERVER_STT);
  http.addHeader("Content-Type", "application/octet-stream");
  http.addHeader("X-Rate", String(SR));        // tell server the mic sample rate
  http.setTimeout(30000);
  int code = http.POST(pcm, pcmLen);
  String body = (code > 0) ? http.getString() : String();
  http.end();
  Serial.printf("STT %u bytes -> HTTP %d : %s\n", (unsigned)pcmLen, code, body.c_str());
  if (code != 200) return String("(server ") + code + ")";
  String t = jsonField(body, "text");
  return t.length() ? t : "(no speech)";
}

String postChat(const String &text) {
  if (WiFi.status() != WL_CONNECTED) return "(wifi down)";
  HTTPClient http;
  http.begin(SERVER_CHAT);
  http.addHeader("Content-Type", "text/plain");
  http.setTimeout(60000);
  int code = http.POST((uint8_t *)text.c_str(), text.length());
  String body = (code > 0) ? http.getString() : String();
  http.end();
  Serial.printf("CHAT -> HTTP %d : %s\n", code, body.c_str());
  if (code != 200) return String("(server ") + code + ")";
  String r = jsonField(body, "reply");
  return r.length() ? r : "(no reply)";
}

// Fetch TTS audio for `text` from /tts and stream it to the speaker (ES8311 DAC).
// Server returns raw PCM16 mono @16k; we duplicate to stereo for the I2S slot mode.
void speakReply(const String &text) {
  if (WiFi.status() != WL_CONNECTED || text.length() == 0) return;
  HTTPClient http;
  http.begin(SERVER_TTS);
  http.addHeader("Content-Type", "text/plain");
  http.setTimeout(60000);
  int code = http.POST((uint8_t *)text.c_str(), text.length());
  if (code != 200) { Serial.printf("TTS HTTP %d\n", code); http.end(); return; }

  int total = http.getSize();                 // content length (-1 if unknown)
  WiFiClient *stream = http.getStreamPtr();
  static uint8_t  rbuf[1024];                  // incoming mono PCM
  static int16_t  sbuf[512 * 2];               // stereo out

  digitalWrite(AMP_ENABLE, LOW);               // amp ON
  delay(5);
  int remaining = total;
  unsigned long lastData = millis();
  while (http.connected() && (total < 0 || remaining > 0)) {
    size_t avail = stream->available();
    if (avail) {
      int toRead = avail < sizeof(rbuf) ? avail : sizeof(rbuf);
      if (total > 0 && toRead > remaining) toRead = remaining;
      toRead &= ~1;                            // keep 16-bit aligned
      if (toRead <= 0) { delay(1); continue; }
      int n = stream->readBytes(rbuf, toRead);
      if (n <= 0) break;
      int samples = n / 2;
      for (int i = 0; i < samples; i++) {
        int16_t s = (int16_t)(rbuf[2 * i] | (rbuf[2 * i + 1] << 8));
        sbuf[2 * i] = s; sbuf[2 * i + 1] = s;  // mono -> L+R
      }
      i2s.write((uint8_t *)sbuf, samples * 4);
      if (total > 0) remaining -= n;
      lastData = millis();
    } else {
      if (millis() - lastData > 4000) break;   // stall timeout
      delay(2);
    }
  }
  delay(40);
  digitalWrite(AMP_ENABLE, HIGH);              // amp OFF (silence hiss)
  http.end();
}

// --- Streaming /converse: frame = [1B type 'T'/'A'][4B BE length][payload] ---
// Read exactly n bytes from the stream (blocks with timeouts).
static bool readExact(WiFiClient *st, uint8_t *buf, uint32_t n) {
  uint32_t got = 0; unsigned long last = millis();
  while (got < n) {
    int a = st->available();
    if (a > 0) {
      int r = st->read(buf + got, (a < (int)(n - got)) ? a : (n - got));
      if (r > 0) { got += r; last = millis(); }
    } else {
      if (!st->connected() && st->available() == 0) return false;
      if (millis() - last > 8000) return false;
      delay(1);
    }
  }
  return true;
}

// Read `len` bytes of PCM16 mono, duplicate to stereo, stream to I2S.
static bool playPcm(WiFiClient *st, uint32_t len) {
  static uint8_t rb[1024]; static int16_t sb[512 * 2];
  uint32_t got = 0;
  while (got < len) {
    uint32_t want = len - got; if (want > sizeof(rb)) want = sizeof(rb);
    want &= ~1u;
    if (want == 0) { uint8_t one; return readExact(st, &one, 1); }
    if (!readExact(st, rb, want)) return false;
    int samples = want / 2;
    for (int i = 0; i < samples; i++) {
      int16_t v = (int16_t)(rb[2 * i] | (rb[2 * i + 1] << 8));
      sb[2 * i] = v; sb[2 * i + 1] = v;
    }
    i2s.write((uint8_t *)sb, samples * 4);
    got += want;
  }
  return true;
}

// Full streaming turn: send user text, receive framed sentences (show + speak).
void converseStream(const String &text) {
  if (WiFi.status() != WL_CONNECTED) { replyText = "(wifi down)"; drawUI(); return; }
  HTTPClient http;
  http.begin(SERVER_CONVERSE);
  http.addHeader("Content-Type", "text/plain");
  http.setTimeout(60000);
  int code = http.POST((uint8_t *)text.c_str(), text.length());
  if (code != 200) { replyText = String("(server ") + code + ")"; http.end(); drawUI(); return; }

  WiFiClient *st = http.getStreamPtr();
  replyText = ""; replyScroll = 9999;
  digitalWrite(AMP_ENABLE, LOW);               // amp ON for the whole turn
  while (true) {
    uint8_t hb[5];
    if (!readExact(st, hb, 5)) break;          // type + 4-byte big-endian length
    uint32_t len = ((uint32_t)hb[1] << 24) | ((uint32_t)hb[2] << 16) |
                   ((uint32_t)hb[3] << 8) | hb[4];
    if (hb[0] == 'T') {                         // sentence text -> show it
      String s; s.reserve(len + 1);
      uint8_t tmp[128]; uint32_t got = 0; bool ok = true;
      while (got < len) {
        uint32_t w = len - got; if (w > sizeof(tmp)) w = sizeof(tmp);
        if (!(ok = readExact(st, tmp, w))) break;
        for (uint32_t i = 0; i < w; i++) s += (char)tmp[i];
        got += w;
      }
      if (!ok) break;
      if (replyText.length()) replyText += " ";
      replyText += s; replyScroll = 9999; drawUI();
    } else if (hb[0] == 'A') {                  // sentence audio -> play it
      if (!playPcm(st, len)) break;
    } else {                                    // unknown frame -> skip
      uint8_t junk[256]; uint32_t got = 0;
      while (got < len) { uint32_t w = len - got; if (w > sizeof(junk)) w = sizeof(junk);
        if (!readExact(st, junk, w)) break; got += w; }
    }
  }
  delay(40);
  digitalWrite(AMP_ENABLE, HIGH);              // amp OFF
  http.end();
  int maxScroll = g_replyLines > PIP_VISIBLE ? g_replyLines - PIP_VISIBLE : 0;
  replyScroll = maxScroll;                      // leave scrolled to the end
}

void setup() {
  Serial.begin(115200);
  delay(300);

  // Backlight
  ledcAttach(LCD_BL, 5000, 8);
  ledcWrite(LCD_BL, 255);

  if (!gfx->begin(QSPI_HZ)) {
    Serial.println("gfx->begin() failed (PSRAM? FQBN PSRAM=opi)");
    while (1) delay(100);
  }
  touchInit();                               // also begins Wire @400k (shared bus)
  drawUI();

  // Amp off (mic-only); codec init for ADC input
  pinMode(AMP_ENABLE, OUTPUT);
  digitalWrite(AMP_ENABLE, HIGH);
  CodecConfig cfg;
  cfg.input_device  = ADC_INPUT_ALL;
  cfg.output_device = DAC_OUTPUT_ALL;
  cfg.i2s.bits = BIT_LENGTH_16BITS;
  cfg.i2s.rate = RATE_24K;               // 24 kHz codec clocking
  if (!codec.begin(cfg)) Serial.println("WARN: ES8311 init failed");
  codec.setInputVolume(75);     // mic gain
  codec.setVolume(50);          // speaker/DAC volume (calibrated level)
  i2s.setPins(I2S_BCLK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO))
    Serial.println("WARN: I2S init failed");

  // PSRAM record buffer
  pcm = (uint8_t *)ps_malloc(MAX_PCM);
  if (!pcm) Serial.println("WARN: ps_malloc failed");

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
  status = (WiFi.status() == WL_CONNECTED)
             ? String("ready  ") + WiFi.localIP().toString()
             : String("wifi FAILED");
  replyText = "Tap the button and talk to me!";
  drawUI();
}

static String readyStatus() {
  return (WiFi.status() == WL_CONNECTED)
           ? String("ready  ") + WiFi.localIP().toString()
           : String("wifi FAILED");
}

void loop() {
  pollTouch();
  switch (state) {
    case ST_IDLE:
      if (buttonTapped()) {
        pcmLen = 0; level = 0; userText = ""; replyText = ""; replyScroll = 0;
        state = ST_RECORDING;
        status = "listening...";
        drawUI();
      }
      break;

    case ST_RECORDING: {
      recordChunk();
      static unsigned long lastDraw = 0;
      if (millis() - lastDraw > 100) { drawUI(); lastDraw = millis(); }
      if (buttonTapped() || pcmLen >= MAX_PCM) {
        state = ST_UPLOADING;
        status = (pcmLen >= MAX_PCM) ? "max 10s, transcribing..." : "transcribing...";
        level = 0;
        drawUI();
      }
      break;
    }

    case ST_UPLOADING:
      userText = postAudio();
      if (userText.length() && userText[0] != '(') {   // valid transcription
        status = "Pip is thinking...";
        replyText = "";
        drawUI();                                       // show "You:" now
        state = ST_CONVERSE;
      } else {
        replyText = "(couldn't catch that - try again)";
        status = readyStatus();
        state = ST_RESULT;
        drawUI();
      }
      break;

    case ST_CONVERSE:
      converseStream(userText);                  // streams sentences: show + speak
      status = readyStatus();
      state = ST_RESULT;
      drawUI();
      break;

    case ST_RESULT:
      handleReplyScroll();                      // drag inside Pip box to scroll
      if (buttonTapped()) {                     // tap button to record next
        pcmLen = 0; level = 0; userText = ""; replyText = ""; replyScroll = 0;
        state = ST_RECORDING;
        status = "listening...";
        drawUI();
      }
      break;
  }
  delay(5);
}

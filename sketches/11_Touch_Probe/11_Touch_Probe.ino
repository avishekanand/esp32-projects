// 11_Touch_Probe v3 — dump the contiguous 0x0010 touch report block (header+points)
// so we can read the real byte layout. Safe: only reads the valid 0x0010 register.
// Screen stays dark (no display driver) — expected.
#include <Wire.h>

#define TP_SDA  38
#define TP_SCL  39
#define TP_INT  47
#define TP_RST  48
#define TP_ADDR 0x55
#define BLK     24    // header(4) + ~2 points(14) + margin

// read `len` bytes starting at 16-bit reg; returns actual count received
static uint8_t rd16(uint16_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(TP_ADDR);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return 0;
  uint8_t n = Wire.requestFrom((uint8_t)TP_ADDR, len);
  for (uint8_t i = 0; i < n && i < len; i++) buf[i] = Wire.read();
  return n;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);  delay(10);
  digitalWrite(TP_RST, HIGH); delay(80);
  pinMode(TP_INT, INPUT_PULLUP);
  Wire.begin(TP_SDA, TP_SCL, 400000);
  delay(50);
  Serial.println("Dump 0x0010 block. Touch & move; only non-idle frames print.");
}

void loop() {
  uint8_t b[BLK];
  uint8_t n = rd16(0x0010, b, BLK);
  if (n == 0) { Serial.println("read fail"); delay(200); return; }
  // Only print frames where something looks active (header byte0 nonzero OR
  // first point down-bit set) to cut idle noise, plus INT state.
  bool active = (b[0] & 0x08) || (b[4] & 0x80) || b[0] || b[1];
  if (active) {
    Serial.printf("INT=%d n=%u |", digitalRead(TP_INT), n);
    for (uint8_t i = 0; i < n && i < BLK; i++) Serial.printf(" %02X", b[i]);
    Serial.println();
  }
  delay(60);
}

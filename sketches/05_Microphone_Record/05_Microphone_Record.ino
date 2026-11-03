// 05_Microphone_Record — verify the ES8311 built-in microphone (input path).
// Prints a per-channel peak/RMS meter so we can see amplitude move when you
// speak/tap, and tell which I2S slot (L/R) the mono mic lands on.
#include <Arduino.h>
#include <Wire.h>
#include <ESP_I2S.h>
#include "AudioBoard.h"

#define AMP_ENABLE 1
#define I2S_MCLK   17
#define I2S_BCLK   18
#define I2S_WS     21
#define I2S_DOUT   15
#define I2S_DIN    16

const int SAMPLE_RATE = 16000;
const int FRAMES      = 1024;                 // stereo frames per read
int16_t buf[FRAMES * 2];                      // L,R interleaved

AudioBoard board(AudioDriverES8311, NoPins);
I2SClass i2s;

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(AMP_ENABLE, OUTPUT);
  digitalWrite(AMP_ENABLE, HIGH);             // speaker amp OFF (mic test)

  Wire.begin(38, 39);
  AudioDriverLogger.begin(Serial, AudioDriverLogLevel::Info);

  CodecConfig cfg;
  cfg.input_device  = ADC_INPUT_ALL;          // enable mic ADC
  cfg.output_device = DAC_OUTPUT_ALL;          // keep duplex clocking as in tone test
  cfg.i2s.bits      = BIT_LENGTH_16BITS;
  cfg.i2s.rate      = RATE_16K;

  Serial.println("Initializing ES8311 (mic)...");
  if (!board.begin(cfg)) {
    Serial.println("Codec initialization failed!");
    return;
  }
  board.setInputVolume(75);                    // ADC/PGA gain (0-100)

  i2s.setPins(I2S_BCLK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE,
                 I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("I2S initialization failed!");
    return;
  }

  Serial.println("Mic ready. Speak / tap the mic — watch the numbers.");
  Serial.println("Format: Lpeak Lrms | Rpeak Rrms  [bar = louder channel]");
}

static void meter(const char* tag, int peak) {
  Serial.print(tag);
  int bars = peak / 400;                       // ~0..80 chars for full-scale
  if (bars > 80) bars = 80;
  for (int i = 0; i < bars; i++) Serial.print('#');
}

void loop() {
  size_t got = i2s.readBytes((char*)buf, sizeof(buf));
  int n = got / 2;                             // int16 samples read
  if (n < 2) { Serial.println("(no I2S data)"); delay(200); return; }

  long lpeak = 0, rpeak = 0;
  double lsq = 0, rsq = 0; int lc = 0, rc = 0;
  for (int i = 0; i + 1 < n; i += 2) {
    int l = buf[i], r = buf[i + 1];
    if (abs(l) > lpeak) lpeak = abs(l);
    if (abs(r) > rpeak) rpeak = abs(r);
    lsq += (double)l * l; rsq += (double)r * r; lc++; rc++;
  }
  int lrms = lc ? (int)sqrt(lsq / lc) : 0;
  int rrms = rc ? (int)sqrt(rsq / rc) : 0;

  Serial.printf("L:%5ld/%5d  R:%5ld/%5d  ", lpeak, lrms, rpeak, rrms);
  meter("", (int)max(lpeak, rpeak));
  Serial.println();
  delay(100);                                  // ~10 updates/sec
}

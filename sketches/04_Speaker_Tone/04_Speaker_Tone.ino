// 04_Speaker_Tone — known-good baseline for LCDWIKI ES3C35P (ESP32-S3 + ES8311)
// Verified audible on the original dev machine. Used here to prove the toolchain.
#include <Arduino.h>
#include <Wire.h>
#include <ESP_I2S.h>
#include "AudioBoard.h"
#include <math.h>

#define AMP_ENABLE 1
#define I2S_MCLK   17
#define I2S_BCLK   18
#define I2S_WS     21
#define I2S_DOUT   15

const int SAMPLE_RATE = 44100;
const int TONE_HZ = 440;

AudioBoard board(AudioDriverES8311, NoPins);
I2SClass i2s;

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(AMP_ENABLE, OUTPUT);
  digitalWrite(AMP_ENABLE, HIGH);   // amp OFF during init

  Wire.begin(38, 39);

  AudioDriverLogger.begin(Serial, AudioDriverLogLevel::Info);

  CodecConfig cfg;
  cfg.input_device = ADC_INPUT_NONE;
  cfg.output_device = DAC_OUTPUT_ALL;
  cfg.i2s.bits = BIT_LENGTH_16BITS;
  cfg.i2s.rate = RATE_44K;

  Serial.println("Initializing ES8311...");
  if (!board.begin(cfg)) {
    Serial.println("Codec initialization failed!");
    return;
  }

  board.setVolume(50);

  i2s.setPins(I2S_BCLK, I2S_WS, I2S_DOUT, -1, I2S_MCLK);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE,
                 I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("I2S initialization failed!");
    return;
  }

  digitalWrite(AMP_ENABLE, LOW);    // amp ON after audio subsystem ready

  Serial.println("Speaker ready!");
}

void loop() {
  Serial.println("Playing tone...");
  for (int n = 0; n < SAMPLE_RATE; n++) {
    float phase = 2.0f * PI * TONE_HZ * n / SAMPLE_RATE;
    int16_t sample = (int16_t)(9000 * sinf(phase));
    int16_t stereo[2] = { sample, sample };
    i2s.write((uint8_t*)stereo, sizeof(stereo));
  }
  Serial.println("Silence...");
  delay(1000);
}

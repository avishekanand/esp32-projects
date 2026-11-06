// 12_WiFi_Scan — list the 2.4 GHz networks the ESP32-S3 can actually see.
// Confirms whether the phone hotspot is visible and its EXACT name (case-sensitive).
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(1500);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(200);
}

void loop() {
  Serial.println("=== scanning (2.4 GHz only on ESP32-S3) ===");
  int n = WiFi.scanNetworks();
  Serial.printf("found %d networks:\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("  [%2d] '%s'  rssi=%d dBm  ch=%d  enc=%d\n",
                  i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                  (int)WiFi.encryptionType(i));
  }
  Serial.println("(enc: 0=open 2=WPA 3=WPA2 4=WPA/WPA2 5=WPA2-ENT ...)");
  delay(5000);
}

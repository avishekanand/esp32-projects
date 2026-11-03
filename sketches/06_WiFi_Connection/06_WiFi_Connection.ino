// 06_WiFi_Connection — verify 2.4 GHz Wi-Fi association on the ESP32-S3.
// Credentials come from wifi_secrets.h (gitignored; never printed).
// NOTE: this sketch prints SSID / IP / RSSI only — never the password.
#include <WiFi.h>
#include "wifi_secrets.h"

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.printf("Connecting to SSID: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long timeoutMs = 20000;
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("SUCCESS: Wi-Fi connected!");
    Serial.print("IP address   : "); Serial.println(WiFi.localIP());
    Serial.print("Gateway      : "); Serial.println(WiFi.gatewayIP());
    Serial.print("Subnet       : "); Serial.println(WiFi.subnetMask());
    Serial.print("RSSI (dBm)   : "); Serial.println(WiFi.RSSI());
    Serial.print("MAC          : "); Serial.println(WiFi.macAddress());
    Serial.print("Channel      : "); Serial.println(WiFi.channel());
  } else {
    Serial.println("FAILED: Wi-Fi connection");
    Serial.printf("WiFi.status() = %d\n", WiFi.status());
    Serial.println("Hints: 2.4 GHz only (ESP32-S3 has no 5 GHz); check SSID/password; move closer to AP.");
  }
}

void loop() {
  // Reprint status every 10 s so the serial monitor always shows current state.
  delay(10000);
  Serial.printf("status=%d  RSSI=%ld dBm  IP=%s\n",
                WiFi.status(), (long)WiFi.RSSI(), WiFi.localIP().toString().c_str());
}

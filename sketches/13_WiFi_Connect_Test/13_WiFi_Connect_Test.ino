// 13_WiFi_Connect_Test — try to join the configured network and print the exact
// disconnect reason code (tells wrong-password from not-found, etc.).
#include <WiFi.h>
#include "wifi_secrets.h"

void onEvent(arduino_event_id_t e, arduino_event_info_t info) {
  if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.printf("DISCONNECTED reason=%d\n", info.wifi_sta_disconnected.reason);
    // 15/205 = 4way handshake timeout (usually WRONG PASSWORD)
    // 202/2  = auth fail/expire (WRONG PASSWORD)
    // 201    = NO AP FOUND (ssid/band)
  } else if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    Serial.printf("CONNECTED! IP=%s rssi=%ld\n",
                  WiFi.localIP().toString().c_str(), (long)WiFi.RSSI());
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.printf("Trying to join SSID='%s' (pw len=%d)...\n", WIFI_SSID, (int)strlen(WIFI_PASSWORD));
  WiFi.onEvent(onEvent);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
  static unsigned long t = 0;
  if (millis() - t > 2000) { t = millis(); Serial.printf("status=%d\n", WiFi.status()); }
}

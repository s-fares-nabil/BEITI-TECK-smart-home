#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  WiFi.mode(WIFI_STA);
  delay(500);
  Serial.print("ESP32 MAC: ");
  Serial.println(WiFi.macAddress());
}

void loop() {}

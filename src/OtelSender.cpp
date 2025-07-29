#include "OtelSender.h"
// ——————————————————————————————————————————————————————————
// Platform-specific networking includes
// ——————————————————————————————————————————————————————————
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
#elif defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_RP2040)
  #include <WiFi.h>
  #include <HTTPClient.h>
#else
  #error "Unsupported platform: must be ESP8266, ESP32 or RP2040"
#endif

namespace OTel {

void OTelSender::sendJson(const char* path, JsonDocument& doc) {
  if (doc.overflowed()){
     Serial.println("Document Overflowed");
     return;
  }

  String payload;
  serializeJson(doc, payload);
  Serial.print("Sending to ");
  Serial.println(OTEL_COLLECTOR_HOST);
  Serial.println(payload);

  HTTPClient http;
  // on ESP8266 the legacy begin(url) is removed, must pass a WiFiClient
  #if defined(ESP8266)
    WiFiClient client;
    http.begin(client, String(OTEL_COLLECTOR_HOST) + path);
  #else
    http.begin(String(OTEL_COLLECTOR_HOST) + path);
  #endif

   http.addHeader("Content-Type", "application/json");
   http.POST(payload);
   http.end();

  http.addHeader("Content-Type", "application/json");
  http.POST(payload);
  http.end();
}

} // namespace OTel


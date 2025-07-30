#include "OtelDebug.h"

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
     DBG_PRINTLN("Document Overflowed");
     return;
  }

  String payload;
  serializeJson(doc, payload);
//  DBG_PRINT("Sending to ");
//  DBG_PRINT(OTEL_COLLECTOR_HOST);
//  DBG_PRINT(":");
//  DBG_PRINT(OTEL_COLLECTOR_PORT);
//  DBG_PRINTLN(path);
//  DBG_PRINTLN(payload);

  HTTPClient http;
  // on ESP8266 the legacy begin(url) is removed, must pass a WiFiClient
  #if defined(ESP8266)
    WiFiClient client;
    http.begin(client, String(OTEL_COLLECTOR_HOST) + path);
  #else
    http.begin(String(OTEL_COLLECTOR_HOST) + path);
  #endif

   DBG_PRINTLN(payload);

   http.addHeader("Content-Type", "application/json");
   http.POST(payload);
   http.end();
}

} // namespace OTel


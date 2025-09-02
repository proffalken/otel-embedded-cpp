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

static String baseUrl() {
  String base = String(OTEL_COLLECTOR_HOST); // may be "http://192.168.8.10" or "192.168.8.10:4318" etc.

  // Ensure there is a scheme
  if (!(base.startsWith("http://") || base.startsWith("https://"))) {
    base = String("http://") + base;
  }

  // Ensure there is a port (check for ":" after the scheme)
  int scheme_end = base.indexOf("://") + 3;
  int colon_after_scheme = base.indexOf(':', scheme_end);
  int slash_after_scheme = base.indexOf('/', scheme_end);
  if (colon_after_scheme == -1 || (slash_after_scheme != -1 && colon_after_scheme > slash_after_scheme)) {
    // No explicit port present; append compile-time port
    base += ":" + String(OTEL_COLLECTOR_PORT);
  }

  // Strip any trailing slash before we append path
  if (base.endsWith("/")) base.remove(base.length() - 1);
  return base;
}

static String fullUrl(const char* path) {
  String p = path ? String(path) : String();
  if (!p.startsWith("/")) p = "/" + p;
  return baseUrl() + p;
}

void OTelSender::sendJson(const char* path, JsonDocument& doc) {
  if (doc.overflowed()){
     DBG_PRINTLN("Document Overflowed");
     return;
  }



  String payload;
  serializeJson(doc, payload);

  String url = fullUrl(path);
DBG_PRINT("HTTP begin URL: >"); DBG_PRINT(url); DBG_PRINTLN("<");

#ifdef ESP8266
  WiFiClient *clientPtr = nullptr;
  WiFiClient client;            // or WiFiClientSecure if using https
  clientPtr = &client;
  HTTPClient http;
  http.begin(*clientPtr, url);
#else
  HTTPClient http;
  http.begin(url);
#endif

http.addHeader("Content-Type", "application/json");
DBG_PRINT("Sending Payload: ");
DBG_PRINTLN(payload);
int code = http.POST(payload);
DBG_PRINT("HTTP POST returned: "); DBG_PRINTLN(code);
if (code < 0) { DBG_PRINTLN(http.errorToString(code)); }
http.end();

  /*
  HTTPClient http;
  // on ESP8266 the legacy begin(url) is removed, must pass a WiFiClient
  String url = String(OTEL_COLLECTOR_HOST) + path;
  DBG_PRINT("HTTP begin URL: >"); DBG_PRINT(url); DBG_PRINTLN("<");  // angle brackets expose stray quotes/spaces
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

   int code = http.POST(payload);
DBG_PRINT("HTTP POST returned: "); DBG_PRINTLN(code);
if (code < 0) { DBG_PRINTLN(http.errorToString(code)); }
*/
}

} // namespace OTel


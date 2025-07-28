#include "OtelSender.h"
#include <HTTPClient.h>

void OTelSender::sendJson(const char* path, JsonDocument& doc) {
  if (doc.overflowed()) {
    return;
  }

  String payload;
  serializeJson(doc, payload);

  String url = String(OTEL_COLLECTOR_HOST) + path;

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.POST(payload);
  http.end();
}


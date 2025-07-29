#include "OtelSender.h"
#include <HTTPClient.h>  // <-- was missing

namespace OTel {

void OTelSender::sendJson(const char* path, JsonDocument& doc) {
  if (doc.overflowed()) return;

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.begin(String(OTEL_COLLECTOR_HOST) + path);
  http.addHeader("Content-Type", "application/json");
  http.POST(payload);
  http.end();
}

} // namespace OTel


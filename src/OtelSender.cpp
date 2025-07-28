#include "OtelSender.h"
#include "OtelDefaults.h"

namespace OTel {

void OTelSender::sendJson(const char* path, JsonDocument& doc) {
    HTTPClient http;

    String url = String(OTEL_COLLECTOR_HOST) + path;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String body;
    serializeJson(doc, body);
    http.POST(body);

    http.end();
}

} // namespace OTel


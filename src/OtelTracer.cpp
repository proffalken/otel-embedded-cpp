#include "OtelTracer.h"

namespace OTel {

void Span::end() {
  JsonDocument doc;

  JsonObject resourceSpan = doc["resourceSpans"].add<JsonObject>();
  JsonObject resource = resourceSpan["resource"].to<JsonObject>();
  getDefaultResource().addResourceAttributes(resource);

  JsonObject scopeSpan = resourceSpan["scopeSpans"].add<JsonObject>();
  JsonObject scope = scopeSpan["scope"].to<JsonObject>();
  scope["name"] = "otel-embedded";
  scope["version"] = "0.1.0";

  JsonObject spanObj = scopeSpan["spans"].add<JsonObject>();
  spanObj["traceId"] = traceId;
  spanObj["spanId"] = spanId;
  spanObj["name"] = name;
  spanObj["startTimeUnixNano"] = nowUnixNano();
  spanObj["endTimeUnixNano"] = nowUnixNano();

  OTelSender::sendJson("/v1/traces", doc);
}

} // namespace OTel


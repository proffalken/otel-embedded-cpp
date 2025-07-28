#include <OtelTracer.h>

using namespace OTel;

Span::Span(String spanName) : name(spanName), startTime(millis()) {}

void Span::end() {
  JsonDocument doc;

  JsonObject resource = doc["resource"].to<JsonObject>();
  OTel::getDefaultResource().addResourceAttributes(resource);

  JsonArray scopeSpans = doc["scopeSpans"].to<JsonArray>();
  JsonObject scopeSpan = scopeSpans.add<JsonObject>();

  JsonObject scope = scopeSpan["scope"].to<JsonObject>();
  scope["name"] = "OtelTracer";

  JsonArray spans = scopeSpan["spans"].to<JsonArray>();
  JsonObject span = spans.add<JsonObject>();

  span["name"] = name;
  span["startTimeUnixNano"] = (uint64_t)(startTime * 1000000ULL);
  span["endTimeUnixNano"] = (uint64_t)(millis() * 1000000ULL);

  Tracer().sendJson("/v1/traces", doc);
}

void Tracer::begin(const String &serviceName, const String &host, const String &version) {
  auto config = OTel::getDefaultResource();
  config.setAttribute("service.name", serviceName);
  config.setAttribute("host.name", host);
  if (version.length() > 0) {
    config.setAttribute("service.version", version);
  }
}

Span Tracer::startSpan(const char *name) {
  return Span(String(name));
}

void Tracer::endSpan(Span &span) {
  span.end();
}


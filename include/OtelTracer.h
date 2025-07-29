#ifndef OTEL_TRACER_H
#define OTEL_TRACER_H

#include <ArduinoJson.h>
#include "OtelDefaults.h"   // provides OTelResourceConfig
#include "OtelSender.h"     // provides OTelSender::sendJson()

namespace OTel {

//––– a single, shared ResourceConfig for the whole process –––
static inline OTelResourceConfig& defaultTraceResource() {
  static OTelResourceConfig rc;
  return rc;
}

/// A single span instance.
struct Span {
  String name;
  String traceId;
  String spanId;
  unsigned long startMs;

  Span(const String& n, const String& tId, const String& sId)
    : name(n), traceId(tId), spanId(sId), startMs(millis()) {}

  void end() {
    JsonDocument doc;

    // resourceSpans → [ { … } ]
    JsonArray rsArr = doc["resourceSpans"].to<JsonArray>();
    JsonObject rs   = rsArr.add<JsonObject>();

    // attach the shared resource attributes
    JsonObject resource = rs["resource"].to<JsonObject>();
    defaultTraceResource().addResourceAttributes(resource);

    // scopeSpans → [ { … } ]
    JsonArray ssArr = rs["scopeSpans"].to<JsonArray>();
    JsonObject ss   = ssArr.add<JsonObject>();

    // scope metadata
    JsonObject scope = ss["scope"].to<JsonObject>();
    scope["name"]    = "otel-embedded";
    scope["version"] = "0.1.0";

    // spans → [ { … } ]
    JsonArray spans = ss["spans"].to<JsonArray>();
    JsonObject sp   = spans.add<JsonObject>();
    sp["traceId"]           = traceId;
    sp["spanId"]            = spanId;
    sp["name"]              = name;
    sp["startTimeUnixNano"] = nowUnixNano();
    sp["endTimeUnixNano"]   = nowUnixNano();

    // send it off
    OTelSender::sendJson("/v1/traces", doc);
  }
};

/// Simple tracer: stamp service‐level attributes once, then start/end spans
class Tracer {
public:
  /// Call once in setup() to set service.name, host.name, version
  static void begin(const String& serviceName,
                    const String& serviceNamespace,
                    const String& host,
                    const String& version = "") {
    auto& rc = defaultTraceResource();
    rc.setAttribute("service.name",  serviceName);
    rc.setAttribute("service.namespace",  serviceNamespace);
    rc.setAttribute("host.name",     host);
    if (!version.isEmpty()) {
      rc.setAttribute("service.version", version);
    }
  }

  /// Start a new Span. You must .end() it.
  static Span startSpan(const char* spanName) {
    return Span(String(spanName), randomHex(32), randomHex(16));
  }

private:
  static String randomHex(size_t len) {
    static const char* hexDigits = "0123456789abcdef";
    String out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
      out += hexDigits[random(0, 16)];
    }
    return out;
  }
};

} // namespace OTel

#endif // OTEL_TRACER_H


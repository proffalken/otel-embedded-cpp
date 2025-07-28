#pragma once
#include <ArduinoJson.h>
#include "OtelSender.h"
#include "OtelDefaults.h"

namespace OTel {

struct SpanContext {
    uint64_t start;
    String traceId;
    String spanId;
};

class Tracer : public OTelSender {
private:
    static OTelResourceConfig config;

public:
    static SpanContext startSpan(const String& name, const SpanContext* parent = nullptr) {
        SpanContext ctx;
        ctx.start = millis() * 1000000ull;
        ctx.traceId = generateHex(16);
        ctx.spanId = generateHex(8);
        return ctx;
    }

    static void endSpan(const SpanContext& ctx) {
        uint64_t end = millis() * 1000000ull;

        JsonDocument doc;
        JsonObject resourceSpans = doc["resourceSpans"].add<JsonObject>();
        JsonObject resource = resourceSpans["resource"].to<JsonObject>();
        JsonArray attrs = resource["attributes"].to<JsonArray>();
        config.serializeAttributes(attrs);

        JsonObject scopeSpans = resourceSpans["scopeSpans"].add<JsonObject>();
        JsonObject scope = scopeSpans["scope"].to<JsonObject>();
        scope["name"] = "otel-embedded";
        scope["version"] = "0.0.1";

        JsonArray spans = scopeSpans["spans"].to<JsonArray>();
        JsonObject span = spans.add<JsonObject>();
        span["traceId"] = ctx.traceId;
        span["spanId"] = ctx.spanId;
        span["name"] = "unnamed";
        span["startTimeUnixNano"] = ctx.start;
        span["endTimeUnixNano"] = end;

        sendJson(doc, "/v1/traces");
    }

    static void begin(const String& serviceName, const String& host, const String& version = "") {
        config = getDefaultResource();
        config.setAttribute("service.name", serviceName);
        config.setAttribute("host.name", host);
        if (version != "") {
            config.setAttribute("service.version", version);
        }
    }

private:
    static String generateHex(size_t length) {
        String result;
        for (size_t i = 0; i < length; ++i) {
            result += String(random(16), HEX);
        }
        return result;
    }
};

}  // namespace OTel


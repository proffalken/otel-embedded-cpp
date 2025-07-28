#include "OtelTracer.h"
#include "OtelDefaults.h"
#include "OtelSender.h"

namespace OTel {

Span::Span(const String& spanName)
    : name(spanName), start(millis()) {}

void Span::end() {
    uint64_t endTime = millis() * 1000000ULL;

    DynamicJsonDocument doc(1024);
    JsonObject resourceSpan = doc.createNestedObject("resource_spans");
    getDefaultResource().addResourceAttributes(resourceSpan.createNestedObject("resource"));

    JsonArray scopeSpans = resourceSpan.createNestedArray("scope_spans");
    JsonObject scope = scopeSpans.createNestedObject();
    JsonArray spans = scope.createNestedArray("spans");

    JsonObject span = spans.createNestedObject();
    span["name"] = name;
    span["start_time_unix_nano"] = (uint64_t)(start * 1000000ULL);
    span["end_time_unix_nano"] = endTime;

    OTelSender::sendJson("/v1/traces", doc);
}

Span Tracer::startSpan(const String& name) {
    return Span(name);
}

void Tracer::endSpan(Span& span) {
    span.end();
}

void Tracer::begin(const String& serviceName, const String& host, const String& version) {
    OTel::getDefaultResource().setAttribute("service.name", serviceName);
    OTel::getDefaultResource().setAttribute("host.name", host);
    if (version.length()) {
        OTel::getDefaultResource().setAttribute("service.version", version);
    }
}

} // namespace OTel


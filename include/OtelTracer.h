#pragma once

#include <ArduinoJson.h>
#include <Arduino.h>
#include "OtelMetrics.h"       // For OTelSender and OTelResourceConfig
#include "OtelEmbeddedCpp.h"   // For OTel::getDefaultResource

class OTelTracer : public OTelSender {
public:
  static void begin(const String &serviceName,
                    const String &collector,
                    const String &version = "0.1.0",
                    const String &instanceId = "") {
    setCollectorHost(collector);
    _resource = OTelResourceConfig(serviceName, "", version, instanceId);
  }

  static void setResource(const OTelResourceConfig &cfg) {
    _resource = cfg;
  }

  static uint64_t startSpan(const String &name) {
    uint64_t t = ((uint64_t)millis()) * 1000000ULL;
    _spanStart = t;
    _spanName = name;
    return t;
  }

  static void endSpan(uint64_t startTime) {
    StaticJsonDocument<1024> doc;

    JsonObject resourceSpans = doc.createNestedArray("resourceSpans").createNestedObject();
    JsonObject resource = resourceSpans.createNestedObject("resource");
    JsonArray attrs = resource.createNestedArray("attributes");
    _resource.serializeAttributes(attrs);
    resource["droppedAttributesCount"] = 0;

    JsonObject scopeSpans = resourceSpans.createNestedArray("scopeSpans").createNestedObject();
    JsonObject scope = scopeSpans.createNestedObject("scope");
    scope["name"] = "otel-embedded-cpp";
    scope["version"] = "0.2.0";

    JsonArray spans = scopeSpans.createNestedArray("spans");
    JsonObject span = spans.createNestedObject();
    span["name"] = _spanName;
    span["startTimeUnixNano"] = startTime;
    span["endTimeUnixNano"] = ((uint64_t)millis()) * 1000000ULL;
    span["traceId"] = generateHexId(32); // 128-bit hex
    span["spanId"] = generateHexId(16);  // 64-bit hex
    span["kind"] = 1; // INTERNAL

    sendJson(doc, "/v1/traces");
  }

private:
  static OTelResourceConfig _resource;
  static String _spanName;
  static uint64_t _spanStart;

  static String generateHexId(int bits) {
    String out;
    for (int i = 0; i < bits / 8; i++) {
      byte b = random(0, 255);
      if (b < 16) out += "0";
      out += String(b, HEX);
    }
    return out;
  }
};


#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "OtelMetrics.h"  // for OTelSender + ResourceConfig
#include "OtelEmbeddedCpp.h" // for getDefaultResource()

class OTelLogger : public OTelSender {
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

  static void log(const String &severity, const String &message) {
    StaticJsonDocument<768> doc;

    JsonObject resourceLogs = doc.createNestedArray("resourceLogs").createNestedObject();
    JsonObject resource = resourceLogs.createNestedObject("resource");
    JsonArray attrs = resource.createNestedArray("attributes");
    _resource.serializeAttributes(attrs);
    resource["droppedAttributesCount"] = 0;

    JsonObject scopeLogs = resourceLogs.createNestedArray("scopeLogs").createNestedObject();
    JsonObject scope = scopeLogs.createNestedObject("scope");
    scope["name"] = "otel-embedded-cpp";
    scope["version"] = "0.2.0";

    JsonArray logs = scopeLogs.createNestedArray("logRecords");
    JsonObject entry = logs.createNestedObject();
    entry["timeUnixNano"] = ((uint64_t)millis()) * 1000000ULL;
    entry["severityText"] = severity;
    entry["severityNumber"] = mapSeverity(severity);
    entry["body"]["stringValue"] = message;

    sendJson(doc, "/v1/logs");
  }

  static void logInfo(const String &msg)  { log("INFO", msg); }
  static void logDebug(const String &msg) { log("DEBUG", msg); }
  static void logError(const String &msg) { log("ERROR", msg); }

private:
  static OTelResourceConfig _resource;

  static int mapSeverity(const String &s) {
    if (s == "TRACE") return 1;
    if (s == "DEBUG") return 5;
    if (s == "INFO")  return 9;
    if (s == "WARN")  return 13;
    if (s == "ERROR") return 17;
    if (s == "FATAL") return 21;
    return 0;
  }
};


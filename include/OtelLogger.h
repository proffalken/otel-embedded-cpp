#ifndef OTEL_LOGGER_H
#define OTEL_LOGGER_H

#include <ArduinoJson.h>
#include "OtelDefaults.h"
#include "OtelSender.h"

namespace OTel {

class Logger {
public:
  static void begin(const String &serviceName, const String &collector, const String &version) {
    getDefaultResource().setAttribute("service.name", serviceName);
    getDefaultResource().setAttribute("service.version", version);
    getDefaultResource().setAttribute("host.name", collector);
  }

  static void log(const char* severity, const String& message) {
    JsonDocument doc;

    JsonObject resourceLog = doc["resourceLogs"].add<JsonObject>();
    JsonObject resource = resourceLog["resource"].to<JsonObject>();
    getDefaultResource().addResourceAttributes(resource);

    JsonObject scopeLog = resourceLog["scopeLogs"].add<JsonObject>();
    JsonObject scope = scopeLog["scope"].to<JsonObject>();
    scope["name"] = "otel-embedded";
    scope["version"] = "0.1.0";

    JsonObject logEntry = scopeLog["logRecords"].add<JsonObject>();
    logEntry["timeUnixNano"] = nowUnixNano();
    JsonObject body = logEntry["body"].to<JsonObject>();
    body["stringValue"] = message;

    OTelSender::sendJson("/v1/logs", doc);
  }

  static void logInfo(const char* message) {
    log("info", String(message));
  }

  static void logInfo(const String& message) {
    log("info", message);
  }
};

} // namespace OTel

#endif


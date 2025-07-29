#ifndef OTEL_LOGGER_H
#define OTEL_LOGGER_H

#include <ArduinoJson.h>
#include "OtelDefaults.h"
#include "OtelSender.h"

namespace OTel {

// This is your one-and-only resource config singleton
static inline OTelResourceConfig& defaultResource() {
  static OTelResourceConfig rc;
  return rc;
}

class Logger {
public:
  // Initialize service.* and host resource attributes
  static void begin(const String &serviceName,
                    const String &serviceNamespace,
                    const String &collector,
                    const String &host,
                    const String &version) {
    auto& rc = defaultResource();
    rc.setAttribute("service.name",      serviceName);
    rc.setAttribute("service.namespace", serviceNamespace);
    rc.setAttribute("service.version",   version);
    rc.setAttribute("host.name",         host);
    // you can also add rc.setAttribute("collector.endpoint", collector);
  }

  // Core log emitter
  static void log(const char* severity, const String& message) {
    // adjust capacity as needed
    DynamicJsonDocument doc(1024);

    // Top-level resourceLogs array
    JsonObject resourceLog = doc["resourceLogs"].add<JsonObject>();

    // Populate resource attributes from the same singleton you set in begin()
    JsonObject resource = resourceLog["resource"].to<JsonObject>();
    defaultResource().addResourceAttributes(resource);

    // instrumentation scope
    JsonObject scopeLog = resourceLog["scopeLogs"].add<JsonObject>();
    JsonObject scope    = scopeLog["scope"].to<JsonObject>();
    scope["name"]       = "otel-embedded";
    scope["version"]    = "0.1.0";

    // the log record itself
    JsonObject logEntry            = scopeLog["logRecords"].add<JsonObject>();
    logEntry["timeUnixNano"]       = nowUnixNano();
    JsonObject body                = logEntry["body"].to<JsonObject>();
    body["stringValue"]            = message;
    logEntry["severityText"]       = severity;

    // send to OTLP HTTP exporter
    OTelSender::sendJson("/v1/logs", doc);
  }

  static void logInfo(const char* msg)    { log("INFO", String(msg)); }
  static void logInfo(const String& msg)  { log("INFO", msg); }
};

} // namespace OTel

#endif // OTEL_LOGGER_H


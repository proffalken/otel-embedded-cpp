#ifndef OTEL_LOGGER_H
#define OTEL_LOGGER_H

#include <ArduinoJson.h>
#include <OtelDefaults.h>
#include <OtelSender.h>

namespace OTel {

class Logger : public OtelSender {
public:
  static OTelResourceConfig config;

  static void begin(const String& serviceName,
                    const String& host,
                    const String& version = "") {
    config.setAttribute("service.name", serviceName);
    config.setAttribute("host.name", host);
    if (version.length()) {
      config.setAttribute("service.version", version);
    }
  }

  static void log(const char* severity, const String& body) {
    JsonDocument doc;
    JsonObject logRoot = doc.to<JsonObject>();

    JsonObject resource = logRoot["resource"].to<JsonObject>();
    config.addResourceAttributes(resource);

    JsonArray scopeLogs = logRoot["scopeLogs"].to<JsonArray>();
    JsonObject scopeLog = scopeLogs.add<JsonObject>();
    JsonObject scope = scopeLog["scope"].to<JsonObject>();
    scope["name"] = "OtelLogger";

    JsonArray logs = scopeLog["logRecords"].to<JsonArray>();
    JsonObject log = logs.add<JsonObject>();

    log["timeUnixNano"] = (uint64_t) (millis() * 1000000ULL);
    log["severityText"] = severity;
    log["body"]["stringValue"] = body;

    sendJson("/v1/logs", doc);
  }

  static void logInfo(const String& message) {
    log("INFO", message);
  }

  static void logWarn(const String& message) {
    log("WARN", message);
  }

  static void logError(const String& message) {
    log("ERROR", message);
  }
};

} // namespace OTel

#endif


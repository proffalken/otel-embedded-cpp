#pragma once
#include <ArduinoJson.h>
#include "OtelSender.h"
#include "OtelDefaults.h"

namespace OTel {

class Logger : public OTelSender {
private:
    static OTelResourceConfig config;

    static void log(const char* severity, const String& body) {
        JsonDocument doc;
        JsonObject resourceLogs = doc["resourceLogs"].add<JsonObject>();
        JsonObject resource = resourceLogs["resource"].to<JsonObject>();
        JsonArray attrs = resource["attributes"].to<JsonArray>();
        config.serializeAttributes(attrs);

        JsonObject scopeLogs = resourceLogs["scopeLogs"].add<JsonObject>();
        JsonObject scope = scopeLogs["scope"].to<JsonObject>();
        scope["name"] = "otel-embedded";
        scope["version"] = "0.0.1";

        JsonArray logs = scopeLogs["logRecords"].to<JsonArray>();
        JsonObject log = logs.add<JsonObject>();
        log["timeUnixNano"] = millis() * 1000000ull;
        log["severityText"] = severity;
        log["body"]["stringValue"] = body;

        sendJson(doc, "/v1/logs");
    }

public:
    static void begin(const String& serviceName, const String& host, const String& version = "") {
        config = getDefaultResource();
        config.setAttribute("service.name", serviceName);
        config.setAttribute("host.name", host);
        if (version != "") {
            config.setAttribute("service.version", version);
        }
    }

    static void logInfo(const String& message) { log("INFO", message); }
    static void logWarn(const String& message) { log("WARN", message); }
    static void logError(const String& message) { log("ERROR", message); }
};

}  // namespace OTel


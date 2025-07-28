#include "OtelLogger.h"
#include "OtelDefaults.h"
#include "OtelSender.h"

namespace OTel {

OTelResourceConfig Logger::config;

void Logger::begin(const String& serviceName, const String& host, const String& version) {
    config = getDefaultResource();
    config.setAttribute("service.name", serviceName);
    config.setAttribute("host.name", host);
    if (version.length() > 0) {
        config.setAttribute("service.version", version);
    }
}

void Logger::log(const char* severity, const String& body) {
    DynamicJsonDocument doc(1024);
    JsonArray logs = doc.createNestedArray("logs");
    JsonObject log = logs.add<JsonObject>();

    JsonObject attrs = log.createNestedObject("attributes");
    config.serializeAttributes(attrs);

    log["severity_text"] = severity;
    log["body"] = body;
    log["timestamp"] = millis() * 1000000ULL;

    OTelSender::sendJson("/v1/logs", doc);
}

void Logger::logInfo(const String& message) {
    log("INFO", message);
}

void Logger::logError(const String& message) {
    log("ERROR", message);
}

void Logger::logDebug(const String& message) {
    log("DEBUG", message);
}

} // namespace OTel


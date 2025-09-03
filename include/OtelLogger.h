// OtelLogger.h
#ifndef OTEL_LOGGER_H
#define OTEL_LOGGER_H

#include <Arduino.h>
#include <map>
#include <initializer_list>
#include <ArduinoJson.h>
#include "OtelDefaults.h"   // expects: nowUnixNano()
#include "OtelSender.h"     // expects: OTelSender::sendJson(path, doc)
#include "OtelTracer.h"     // provides: currentTraceContext(), u64ToStr(), defaults & addResAttr helpers

namespace OTel {

// ---- Severity mapping -------------------------------------------------------
static inline int severityNumberFromText(const String& s) {
  if (s == "TRACE") return 1;
  if (s == "DEBUG") return 5;
  if (s == "INFO")  return 9;
  if (s == "WARN")  return 13;
  if (s == "ERROR") return 17;
  if (s == "FATAL") return 21;
  return 0;
}

// ---- Instrumentation scope for logs -----------------------------------------
struct LogScopeConfig {
  String scopeName{"otel-embedded-cpp"};
  String scopeVersion{""}; // optional
};
static inline LogScopeConfig& logScopeConfig() {
  static LogScopeConfig cfg;
  return cfg;
}

// ---- Default labels (merged into each log record's attributes) --------------
static inline std::map<String, String>& defaultLabels() {
  static std::map<String, String> labels;
  return labels;
}

class Logger {
public:
  // Set/merge defaults
  static void setDefaultLabels(const std::map<String, String>& labels) {
    defaultLabels() = labels;
  }
  static void setDefaultLabel(const String& key, const String& value) {
    defaultLabels()[key] = value;
  }

  // Map-based API
  static void log(const String& severity, const String& message,
                  const std::map<String,String>& labels = {}) {
    buildAndSend(severity, message, labels);
  }

  // Convenience overload: initializer_list of key/value pairs
  static void log(const String& severity, const String& message,
                  std::initializer_list<std::pair<const char*, const char*>> kvs) {
    std::map<String, String> labels;
    for (auto &kv : kvs) labels[String(kv.first)] = String(kv.second);
    buildAndSend(severity, message, labels);
  }

  // Helpers by severity
  static void logTrace(const String &m, const std::map<String,String> &l = {}) { log("TRACE", m, l); }
  static void logDebug(const String &m, const std::map<String,String> &l = {}) { log("DEBUG", m, l); }
  static void logInfo (const String &m, const std::map<String,String> &l = {}) { log("INFO",  m, l); }
  static void logWarn (const String &m, const std::map<String,String> &l = {}) { log("WARN",  m, l); }
  static void logError(const String &m, const std::map<String,String> &l = {}) { log("ERROR", m, l); }
  static void logFatal(const String &m, const std::map<String,String> &l = {}) { log("FATAL", m, l); }

  static void logTrace(const String &m, std::initializer_list<std::pair<const char*,const char*>> kvs) { log("TRACE", m, kvs); }
  static void logDebug(const String &m, std::initializer_list<std::pair<const char*,const char*>> kvs) { log("DEBUG", m, kvs); }
  static void logInfo (const String &m, std::initializer_list<std::pair<const char*,const char*>> kvs) { log("INFO",  m, kvs); }
  static void logWarn (const String &m, std::initializer_list<std::pair<const char*,const char*>> kvs) { log("WARN",  m, kvs); }
  static void logError(const String &m, std::initializer_list<std::pair<const char*,const char*>> kvs) { log("ERROR", m, kvs); }
  static void logFatal(const String &m, std::initializer_list<std::pair<const char*,const char*>> kvs) { log("FATAL", m, kvs); }

private:
  static void buildAndSend(const String& severity, const String& message,
                           const std::map<String,String>& labels)
  {
    // Build OTLP/HTTP logs payload (ArduinoJson v7)
    JsonDocument doc;

    JsonArray resourceLogs = doc["resourceLogs"].to<JsonArray>();
    JsonObject rl = resourceLogs.add<JsonObject>();

    // Resource (with attributes to ensure service.name lands)
    JsonObject resource = rl["resource"].to<JsonObject>();
    JsonArray rattrs = resource["attributes"].to<JsonArray>();
    addResAttr(rattrs, "service.name",        defaultServiceName());
    addResAttr(rattrs, "service.instance.id", defaultServiceInstanceId());
    addResAttr(rattrs, "host.name",           defaultHostName());

    // Scope
    JsonObject sl = rl["scopeLogs"].to<JsonArray>().add<JsonObject>();
    JsonObject scope = sl["scope"].to<JsonObject>();
    scope["name"]    = logScopeConfig().scopeName;
    if (logScopeConfig().scopeVersion.length())
      scope["version"] = logScopeConfig().scopeVersion;

    // Log record
    JsonObject lr = sl["logRecords"].to<JsonArray>().add<JsonObject>();
    lr["timeUnixNano"]   = u64ToStr(nowUnixNano());
    lr["severityNumber"] = severityNumberFromText(severity);
    lr["severityText"]   = severity;

    // Body
    JsonObject body = lr["body"].to<JsonObject>();
    body["stringValue"] = message;

    // Correlate to active span if present
    auto &ctx = currentTraceContext();
    if (ctx.valid()) {
      lr["traceId"] = ctx.traceId;
      lr["spanId"]  = ctx.spanId;
      // (optional) lr["flags"] = 1;
    }

    // Attributes (merge defaults first, then per-call to allow override)
    JsonArray lattrs = lr["attributes"].to<JsonArray>();

    for (const auto& kv : defaultLabels()) {
      JsonObject a = lattrs.add<JsonObject>();
      a["key"] = kv.first;
      a["value"].to<JsonObject>()["stringValue"] = kv.second;
    }
    for (const auto& kv : labels) {
      JsonObject a = lattrs.add<JsonObject>();
      a["key"] = kv.first;
      a["value"].to<JsonObject>()["stringValue"] = kv.second;
    }

    // Send
    OTelSender::sendJson("/v1/logs", doc);
  }
};

} // namespace OTel

#endif // OTEL_LOGGER_H


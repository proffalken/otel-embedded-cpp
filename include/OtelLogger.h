// OtelLogger.h
#ifndef OTEL_LOGGER_H
#define OTEL_LOGGER_H

#include <map>
#include <ArduinoJson.h>
#include "OtelDefaults.h"
#include "OtelSender.h"

namespace OTel {

class Logger {
private:
  // NEW: process-wide default labels for log records (not resources)
  static inline std::map<String, String> &defaultLabels() {
    static std::map<String, String> labels;
    return labels;
  }

  // NEW: map common severity text to the model's "bucket" numbers
  // See Logs Data Model; we use the lowest value in each range.
  // TRACE: 1–4, DEBUG: 5–8, INFO: 9–12, WARN: 13–16, ERROR: 17–20, FATAL: 21–24
  static uint8_t severityNumberFromText(const char *s) {
    if (!s) return 0;
    // Uppercase comparison expected by our helpers; be tolerant
    if      (!strcasecmp(s, "TRACE")) return 1;
    else if (!strcasecmp(s, "DEBUG")) return 5;
    else if (!strcasecmp(s, "INFO"))  return 9;
    else if (!strcasecmp(s, "WARN") || !strcasecmp(s, "WARNING")) return 13;
    else if (!strcasecmp(s, "ERROR")) return 17;
    else if (!strcasecmp(s, "FATAL") || !strcasecmp(s, "CRITICAL")) return 21;
    return 0; // UNSPECIFIED
  }

  // Helper: merge defaults + per-call labels (per-call overwrites)
  static std::map<String, String> merged(const std::map<String, String> &add) {
    std::map<String, String> out = defaultLabels();
    for (auto &p : add) out[p.first] = p.second;
    return out;
  }

  // Core JSON builder: LogsData envelope with one record
  static void buildAndSend(const char *severity,
                           const String &message,
                           const std::map<String, String> &labels) {
    JsonDocument doc;

    // resourceLogs[0]
    JsonArray resourceLogs = doc["resourceLogs"].to<JsonArray>();
    JsonObject rl = resourceLogs.add<JsonObject>();

    // resourceLogs[0].resource.attributes[]
    JsonObject resource = rl["resource"].to<JsonObject>();
    OTel::defaultResource().toJson(resource); // <- NEW: resource attributes

    // resourceLogs[0].scopeLogs[0]
    JsonArray scopeLogs = rl["scopeLogs"].to<JsonArray>();
    JsonObject sl = scopeLogs.add<JsonObject>();

    // scopeLogs[0].scope – keep minimal; name is optional but nice to have
    JsonObject scope = sl["scope"].to<JsonObject>();
    scope["name"] = "otel-embedded-cpp";

    // scopeLogs[0].logRecords[0]
    JsonArray logRecords = sl["logRecords"].to<JsonArray>();
    JsonObject lr = logRecords.add<JsonObject>();

    // timestamps + severity
    lr["timeUnixNano"]   = OTel::u64ToString(OTel::nowUnixNano());
    lr["severityNumber"] = severityNumberFromText(severity);
    lr["severityText"]   = severity;

    // body
    JsonObject body = lr["body"].to<JsonObject>();
    body["stringValue"] = message;

    // attributes: defaults + per-call
    auto ml = merged(labels);
    if (!ml.empty()) {
      JsonArray attrs = lr["attributes"].to<JsonArray>();
      for (auto &p : ml) {
        OTel::serializeKeyValue(attrs, p.first, p.second);
      }
    }

    // fire
    OTelSender::sendJson("/v1/logs", doc);
  }

public:
  // ————————————————————————————————————————————————
  // Default label management (process-wide)
  // ————————————————————————————————————————————————
  static void setDefaultLabel(const String &key, const String &val) {
    defaultLabels()[key] = val;
  }
  static void setDefaultLabels(const std::map<String, String> &labels) {
    defaultLabels() = labels;
  }
  static void clearDefaultLabels() {
    defaultLabels().clear();
  }

  // ————————————————————————————————————————————————
  // Public logging API
  // ————————————————————————————————————————————————
  // Existing API: keep as-is
  static void log(const char *severity, const String &message) {
    static const std::map<String, String> empty;
    buildAndSend(severity, message, empty);
  }

  // NEW: map-style labels
  static void log(const char *severity,
                  const String &message,
                  const std::map<String, String> &labels) {
    buildAndSend(severity, message, labels);
  }

  // NEW: light-weight pair-list convenience (good for call-sites)
  static void log(const char *severity,
                  const String &message,
                  std::initializer_list<std::pair<const char *, const char *>> kvs) {
    std::map<String, String> m;
    for (auto &p : kvs) m[String(p.first)] = String(p.second);
    buildAndSend(severity, message, m);
  }

  // Convenience levels
  static void logTrace(const String &m)                  { log("TRACE", m); }
  static void logDebug(const String &m)                  { log("DEBUG", m); }
  static void logInfo (const String &m)                  { log("INFO",  m); }
  static void logWarn (const String &m)                  { log("WARN",  m); }
  static void logError(const String &m)                  { log("ERROR", m); }
  static void logFatal(const String &m)                  { log("FATAL", m); }

  // …and with labels
  static void logTrace(const String &m, const std::map<String,String> &l){ log("TRACE", m, l); }
  static void logDebug(const String &m, const std::map<String,String> &l){ log("DEBUG", m, l); }
  static void logInfo (const String &m, const std::map<String,String> &l){ log("INFO",  m, l); }
  static void logWarn (const String &m, const std::map<String,String> &l){ log("WARN",  m, l); }
  static void logError(const String &m, const std::map<String,String> &l){ log("ERROR", m, l); }
  static void logFatal(const String &m, const std::map<String,String> &l){ log("FATAL", m, l); }

  static void logTrace(const String &m, std::initializer_list<std::pair<const char*,const char*>> kvs){ log("TRACE", m, kvs); }
  static void logDebug(const String &m, std::initializer_list<std::pair<const char*,const char*>> kvs){ log("DEBUG", m, kvs); }
  static void logInfo (const String &m, std::initializer_list<std::pair<const char*,const char*>> kvs){ log("INFO",  m, kvs); }
  static void logWarn (const String &m, std::initializer_list<std::pair<const char*,const char*>> kvs){ log("WARN",  m, kvs); }
  static void logError(const String &m, std::initializer_list<std::pair<const char*,const char*>> kvs){ log("ERROR", m, kvs); }
  static void logFatal(const String &m, std::initializer_list<std::pair<const char*,const char*>> kvs){ log("FATAL", m, kvs); }
};

} // namespace OTel
#endif // OTEL_LOGGER_H


#ifndef OTEL_DEFAULTS_H
#define OTEL_DEFAULTS_H

#include <map>
#include <ArduinoJson.h>
#include <sys/time.h>  // gettimeofday()

// This header provides:
//  - Time helpers (nowUnixNano/Millis)
//  - OTLP JSON KeyValue serializers (string/double/int) using ArduinoJson v7 APIs
//  - OTelResourceConfig with legacy-compatible helpers used by Metrics/Tracer:
//      setAttribute(), addResourceAttributes(JsonObject)
//    and newer helpers used by Logger:
//      set(), clear(), toJson(resource)
//  - defaultResource() and defaultTraceResource() singletons

namespace OTel {

// -------------------------------------------------------------------------------------------------
// Time helpers
// -------------------------------------------------------------------------------------------------

/** UNIX timestamp in nanoseconds. Ensure clock is synced (configTime(), etc.) */
static inline uint64_t nowUnixNano() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<uint64_t>(tv.tv_sec) * 1000000000ULL
       + static_cast<uint64_t>(tv.tv_usec) * 1000ULL;
}

/** UNIX timestamp in milliseconds (spare helper) */
static inline uint64_t nowUnixMillis() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<uint64_t>(tv.tv_sec) * 1000ULL
       + static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
}

// Portable uint64 -> String (no printf/ULL reliance; RP2040-safe)
inline String u64ToString(uint64_t v) {
  if (v == 0) return String("0");
  char buf[21];               // max 20 digits + NUL
  char* p = &buf[20];
  *p = '\0';
  while (v > 0) {
    *--p = char('0' + (v % 10));
    v /= 10;
  }
  return String(p);
}

// -------------------------------------------------------------------------------------------------
// OTLP JSON KeyValue helpers (ArduinoJson v7 deprecation-safe)
// -------------------------------------------------------------------------------------------------

/**
 * Serialise a string KeyValue into an OTLP JSON attributes array:
 *   {"key":"<key>","value":{"stringValue":"<value>"}}.
 */
inline void serializeKeyValue(JsonArray &arr, const String &key, const String &value) {
  JsonObject kv = arr.add<JsonObject>();
  kv["key"] = key;
  JsonObject any = kv["value"].to<JsonObject>();
  any["stringValue"] = value;
}

/** Double-valued KeyValue */
inline void serializeKeyDouble(JsonArray &arr, const String &key, double value) {
  JsonObject kv = arr.add<JsonObject>();
  kv["key"] = key;
  JsonObject any = kv["value"].to<JsonObject>();
  any["doubleValue"] = value;
}

/** Int64-valued KeyValue */
inline void serializeKeyInt(JsonArray &arr, const String &key, int64_t value) {
  JsonObject kv = arr.add<JsonObject>();
  kv["key"] = key;
  JsonObject any = kv["value"].to<JsonObject>();
  any["intValue"] = value;
}

// -------------------------------------------------------------------------------------------------
// Resource attributes container (back-compat + new helpers)
// -------------------------------------------------------------------------------------------------

/**
 * Holds resource attributes (service/host/instance/etc.).
 * This struct supports:
 *  - Legacy calls used by your Metrics/Tracer code:
 *      setAttribute(k,v), addResourceAttributes(JsonObject target)
 *    (these write "attributes" directly under the passed object)
 *  - Newer usage for logs envelope:
 *      toJson(resource) -> writes into resource["attributes"]
 */
struct OTelResourceConfig {
  std::map<String, String> attrs;

  // Newer API
  void set(const String &k, const String &v) { attrs[k] = v; }
  void set(const char *k, const String &v)   { attrs[String(k)] = v; }
  void clear()                               { attrs.clear(); }
  bool empty() const                         { return attrs.empty(); }

  // Backwards-compatible API expected by existing Metrics/Tracer code
  void setAttribute(const String &k, const String &v) { attrs[k] = v; }
  void setAttribute(const char *k, const String &v)   { attrs[String(k)] = v; }

  /**
   * Legacy helper used by Metrics/Tracer paths:
   * Writes attributes directly under the given target as:
   *   target["attributes"] = [ {key, value:{stringValue}}, ... ]
   */
  void addResourceAttributes(JsonObject target) const {
    if (attrs.empty()) return;
    JsonArray attributes = target["attributes"].to<JsonArray>();
    for (auto &p : attrs) {
      serializeKeyValue(attributes, p.first, p.second);
    }
  }

  /**
   * Logs envelope helper:
   * Writes into "resource.attributes" of the given resource object:
   *   resource["attributes"] = [ {key, value:{stringValue}}, ... ]
   */
  void toJson(JsonObject resource) const {
    if (attrs.empty()) return;
    JsonArray attributes = resource["attributes"].to<JsonArray>();
    for (auto &p : attrs) {
      serializeKeyValue(attributes, p.first, p.second);
    }
  }
};

// -------------------------------------------------------------------------------------------------
// Singletons (headers-only, inline ok)
// -------------------------------------------------------------------------------------------------

/** Default resource for general use (metrics/logs/etc.) */
static inline OTelResourceConfig& defaultResource() {
  static OTelResourceConfig rc;
  return rc;
}


} // namespace OTel

#endif // OTEL_DEFAULTS_H


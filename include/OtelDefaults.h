#ifndef OTEL_DEFAULTS_H
#define OTEL_DEFAULTS_H

#include <map>
#include <ArduinoJson.h>
#include <sys/time.h>    // for gettimeofday()

namespace OTel {

/**
 * @brief Get the current UNIX time in nanoseconds.
 * Uses the POSIX gettimeofday() clock, which you should have
 * synchronized via configTime()/time().
 */
static inline uint64_t nowUnixNano() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  // seconds→ns, usec→ns
  return uint64_t(tv.tv_sec) * 1000000000ULL
       + uint64_t(tv.tv_usec) *    1000ULL;
}

struct OTelResourceConfig {
  // internal map of attributes
  std::map<String, String> attrs;

  void setAttribute(const String& key, const String& value) {
    attrs[key] = value;
  }

  void serializeKeyValue(JsonArray &arr, const String &k, const String &v) const {
    JsonObject kv = arr.add<JsonObject>();
    kv["key"] = k;
    JsonObject val = kv["value"].to<JsonObject>();
    val["stringValue"] = v;
  }

  void addResourceAttributes(JsonObject &resource) const {
    if (attrs.empty()) return;
    JsonArray attributes = resource["attributes"].to<JsonArray>();
    for (auto &p : attrs) {
      serializeKeyValue(attributes, p.first, p.second);
    }
  }
};

inline OTelResourceConfig getDefaultResource() {
  return OTelResourceConfig();
}

} // namespace OTel

#endif


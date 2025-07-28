#ifndef OTEL_DEFAULTS_H
#define OTEL_DEFAULTS_H

#include <map>
#include <ArduinoJson.h>

namespace OTel {
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


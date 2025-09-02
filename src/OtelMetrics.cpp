#include "OtelMetrics.h"

namespace OTel {

// Helper: merge default + per-call labels into a datapoint attributes array
static void addPointAttributes(JsonArray& attrArray,
                               const std::map<String, String>& callLabels) {
  // Defaults first
  for (const auto& kv : defaultMetricLabels()) {
    JsonObject a = attrArray.add<JsonObject>();
    a["key"] = kv.first;
    a["value"].to<JsonObject>()["stringValue"] = kv.second;
  }
  // Then per-call (override by reusing key downstream in the stack)
  for (const auto& kv : callLabels) {
    JsonObject a = attrArray.add<JsonObject>();
    a["key"] = kv.first;
    a["value"].to<JsonObject>()["stringValue"] = kv.second;
  }
}

static void addCommonResource(JsonObject& resource) {
  JsonArray rattrs = resource["attributes"].to<JsonArray>();
  addResAttr(rattrs, "service.name",        defaultServiceName());
  addResAttr(rattrs, "service.instance.id", defaultServiceInstanceId());
  addResAttr(rattrs, "host.name",           defaultHostName());
}

static void addCommonScope(JsonObject& scope) {
  scope["name"]    = metricsScopeConfig().scopeName;
  scope["version"] = metricsScopeConfig().scopeVersion;
}

// ----------------- GAUGE -----------------
void Metrics::buildAndSendGauge(const String& name, double value,
                                const String& unit,
                                const std::map<String,String>& labels)
{
  JsonDocument doc;

  JsonArray resourceMetrics = doc["resourceMetrics"].to<JsonArray>();
  JsonObject rm = resourceMetrics.add<JsonObject>();

  // resource with attributes (service.name, etc.)
  JsonObject resource = rm["resource"].to<JsonObject>();
  addCommonResource(resource);

  // scope
  JsonObject sm = rm["scopeMetrics"].to<JsonArray>().add<JsonObject>();
  JsonObject scope = sm["scope"].to<JsonObject>();
  addCommonScope(scope);

  // metric
  JsonArray metrics = sm["metrics"].to<JsonArray>();
  JsonObject metric = metrics.add<JsonObject>();
  metric["name"] = name;
  metric["unit"] = unit;
  metric["type"] = "gauge";

  JsonObject gauge = metric["gauge"].to<JsonObject>();
  JsonArray dps = gauge["dataPoints"].to<JsonArray>();
  JsonObject dp = dps.add<JsonObject>();

  dp["timeUnixNano"] = u64ToStr(nowUnixNano());
  dp["asDouble"]     = value;

  JsonArray attrs = dp["attributes"].to<JsonArray>();
  addPointAttributes(attrs, labels);

  OTelSender::sendJson("/v1/metrics", doc);
}

// ----------------- SUM -------------------
void Metrics::buildAndSendSum(const String& name, double value,
                              bool isMonotonic,
                              const String& temporality,
                              const String& unit,
                              const std::map<String,String>& labels)
{
  JsonDocument doc;

  JsonArray resourceMetrics = doc["resourceMetrics"].to<JsonArray>();
  JsonObject rm = resourceMetrics.add<JsonObject>();

  // resource with attributes
  JsonObject resource = rm["resource"].to<JsonObject>();
  addCommonResource(resource);

  // scope
  JsonObject sm = rm["scopeMetrics"].to<JsonArray>().add<JsonObject>();
  JsonObject scope = sm["scope"].to<JsonObject>();
  addCommonScope(scope);

  // metric
  JsonArray metrics = sm["metrics"].to<JsonArray>();
  JsonObject metric = metrics.add<JsonObject>();
  metric["name"] = name;
  metric["unit"] = unit;
  metric["type"] = "sum";

  JsonObject sum = metric["sum"].to<JsonObject>();
  sum["isMonotonic"]           = isMonotonic;
  sum["aggregationTemporality"] = temporality; // "DELTA" or "CUMULATIVE"

  JsonArray dps = sum["dataPoints"].to<JsonArray>();
  JsonObject dp = dps.add<JsonObject>();

  dp["timeUnixNano"] = u64ToStr(nowUnixNano());
  dp["asDouble"]     = value;

  JsonArray attrs = dp["attributes"].to<JsonArray>();
  addPointAttributes(attrs, labels);

  OTelSender::sendJson("/v1/metrics", doc);
}

} // namespace OTel


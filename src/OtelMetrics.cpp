#include "OtelMetrics.h"

namespace OTel {

#if OTEL_EXPORTER_OTLP_PROTOCOL != OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF

/** Append default metric labels then per-call labels to @p attrArray as OTLP KeyValue objects. */
static void addPointAttributes(JsonArray& attrArray,
                               const std::map<String, String>& callLabels) {
  for (const auto& kv : defaultMetricLabels()) {
    JsonObject a = attrArray.add<JsonObject>();
    a["key"] = kv.first;
    a["value"].to<JsonObject>()["stringValue"] = kv.second;
  }
  for (const auto& kv : callLabels) {
    JsonObject a = attrArray.add<JsonObject>();
    a["key"] = kv.first;
    a["value"].to<JsonObject>()["stringValue"] = kv.second;
  }
}

/** Populate the OTLP resource object from defaultResource() or compile-time defaults. */
static void addCommonResource(JsonObject& resource) {
  auto &res = OTel::defaultResource();
  if (!res.empty()) {
    res.addResourceAttributes(resource);
    return;
  }
  JsonArray rattrs = resource["attributes"].to<JsonArray>();
  addResAttr(rattrs, "service.name",        defaultServiceName());
  addResAttr(rattrs, "service.instance.id", defaultServiceInstanceId());
  addResAttr(rattrs, "host.name",           defaultHostName());
}

/** Write the instrumentation scope name and version into @p scope. */
static void addCommonScope(JsonObject& scope) {
  scope["name"]    = metricsScopeConfig().scopeName;
  scope["version"] = metricsScopeConfig().scopeVersion;
}

#endif // OTEL_EXPORTER_OTLP_PROTOCOL != OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF

// ----------------- GAUGE -----------------
void Metrics::buildAndSendGauge(const String& name, double value,
                                const String& unit,
                                const std::map<String,String>& labels)
{
#if OTEL_EXPORTER_OTLP_PROTOCOL == OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF
  {
    auto merged = defaultMetricLabels();
    for (const auto& kv : labels) merged[kv.first] = kv.second;
    OTel::Proto::sendGauge(name, value, unit, merged);
  }
#else
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
#endif  // OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF
}

// ----------------- SUM -------------------
void Metrics::buildAndSendSum(const String& name, double value,
                              bool isMonotonic,
                              const String& temporality,
                              const String& unit,
                              const std::map<String,String>& labels)
{
#if OTEL_EXPORTER_OTLP_PROTOCOL == OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF
  {
    auto merged = defaultMetricLabels();
    for (const auto& kv : labels) merged[kv.first] = kv.second;
    int temporalityInt = (temporality == "CUMULATIVE") ? 2 : 1;
    OTel::Proto::sendSum(name, value, isMonotonic, temporalityInt, unit, merged);
  }
#else
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

  // OTLP JSON encodes AggregationTemporality as an integer enum, not a string.
  // From opentelemetry/proto/metrics/v1/metrics.proto:
  //   AGGREGATION_TEMPORALITY_DELTA       = 1
  //   AGGREGATION_TEMPORALITY_CUMULATIVE  = 2
  // Previously this sent the raw string (e.g. "DELTA"), which is not spec-compliant.
  // Spec-compliant collectors (including Datadog direct OTLP) require the integer.
  // Collectors that were previously accepting the string may silently ignore or
  // default the field — switching to the integer is the correct behaviour.
  // The public API (passing "DELTA" / "CUMULATIVE" to sum()) is unchanged.
  int temporalityInt = (temporality == "CUMULATIVE") ? 2 : 1; // default to DELTA
  JsonObject sum = metric["sum"].to<JsonObject>();
  sum["isMonotonic"]            = isMonotonic;
  sum["aggregationTemporality"] = temporalityInt;

  JsonArray dps = sum["dataPoints"].to<JsonArray>();
  JsonObject dp = dps.add<JsonObject>();

  dp["timeUnixNano"] = u64ToStr(nowUnixNano());
  dp["asDouble"]     = value;

  JsonArray attrs = dp["attributes"].to<JsonArray>();
  addPointAttributes(attrs, labels);

  OTelSender::sendJson("/v1/metrics", doc);
#endif  // OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF
}

} // namespace OTel


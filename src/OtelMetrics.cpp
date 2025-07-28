#include <OtelMetrics.h>

void OTelMetricBase::addCommonResource(JsonDocument &doc) {
  JsonObject resource = doc["resource"].to<JsonObject>();
  OTel::getDefaultResource().addResourceAttributes(resource);
}

void OTelGauge::set(float value) {
  JsonDocument doc;

  addCommonResource(doc);

  JsonArray scopeMetrics = doc["scopeMetrics"].to<JsonArray>();
  JsonObject scopeMetric = scopeMetrics.add<JsonObject>();

  JsonObject scope = scopeMetric["scope"].to<JsonObject>();
  scope["name"] = "OtelGauge";

  JsonArray metrics = scopeMetric["metrics"].to<JsonArray>();
  JsonObject gauge = metrics.add<JsonObject>();

  gauge["name"] = name;
  gauge["unit"] = unit;
  gauge["type"] = "gauge";

  JsonObject gaugeData = gauge["gauge"].to<JsonObject>();
  JsonArray dataPoints = gaugeData["dataPoints"].to<JsonArray>();
  JsonObject dp = dataPoints.add<JsonObject>();

  dp["asDouble"] = value;
  dp["timeUnixNano"] = (uint64_t)(millis() * 1000000ULL);

  OtelSender::sendJson("/v1/metrics", doc);
}

void OTelCounter::inc(float value) {
  JsonDocument doc;

  addCommonResource(doc);

  JsonArray scopeMetrics = doc["scopeMetrics"].to<JsonArray>();
  JsonObject scopeMetric = scopeMetrics.add<JsonObject>();

  JsonObject scope = scopeMetric["scope"].to<JsonObject>();
  scope["name"] = "OtelCounter";

  JsonArray metrics = scopeMetric["metrics"].to<JsonArray>();
  JsonObject counter = metrics.add<JsonObject>();

  counter["name"] = name;
  counter["unit"] = unit;
  counter["type"] = "sum";

  JsonObject sum = counter["sum"].to<JsonObject>();
  sum["isMonotonic"] = true;
  sum["aggregationTemporality"] = 2;

  JsonArray dataPoints = sum["dataPoints"].to<JsonArray>();
  JsonObject dp = dataPoints.add<JsonObject>();

  dp["asDouble"] = value;
  dp["timeUnixNano"] = (uint64_t)(millis() * 1000000ULL);

  OtelSender::sendJson("/v1/metrics", doc);
}


#ifndef OTEL_METRICS_H
#define OTEL_METRICS_H

#include <ArduinoJson.h>
#include "OtelDefaults.h"
#include "OtelSender.h"

namespace OTel {

class OTelMetricBase {
protected:
  String name;
  String unit;
  OTelResourceConfig config;

public:
  OTelMetricBase(String metricName, String metricUnit = "")
      : name(metricName), unit(metricUnit), config(getDefaultResource()) {}

  void setAttribute(const String &key, const String &value) {
    config.setAttribute(key, value);
  }
};

class OTelGauge : public OTelMetricBase {
public:
  using OTelMetricBase::OTelMetricBase;

  void set(float value) {
    JsonDocument doc;

    JsonObject resourceMetric = doc["resourceMetrics"].add<JsonObject>();
    JsonObject resource = resourceMetric["resource"].to<JsonObject>();
    config.addResourceAttributes(resource);

    JsonObject scopeMetric = resourceMetric["scopeMetrics"].add<JsonObject>();
    JsonObject scope = scopeMetric["scope"].to<JsonObject>();
    scope["name"] = "otel-embedded";
    scope["version"] = "0.1.0";

    JsonObject metric = scopeMetric["metrics"].add<JsonObject>();
    metric["name"] = name;
    metric["unit"] = unit;
    metric["type"] = "gauge";

    JsonObject gauge = metric["gauge"].to<JsonObject>();
    JsonObject dp = gauge["dataPoints"].add<JsonObject>();
    config.addResourceAttributes(dp);
    dp["timeUnixNano"] = (unsigned long long)(millis()) * 1000000ULL;
    dp["asDouble"] = value;

    OTelSender::sendJson("/v1/metrics", doc);
  }
};

class OTelCounter : public OTelMetricBase {
private:
  float count = 0;

public:
  using OTelMetricBase::OTelMetricBase;

  void inc(float value = 1) {
    count += value;

    JsonDocument doc;

    JsonObject resourceMetric = doc["resourceMetrics"].add<JsonObject>();
    JsonObject resource = resourceMetric["resource"].to<JsonObject>();
    config.addResourceAttributes(resource);

    JsonObject scopeMetric = resourceMetric["scopeMetrics"].add<JsonObject>();
    JsonObject scope = scopeMetric["scope"].to<JsonObject>();
    scope["name"] = "otel-embedded";
    scope["version"] = "0.1.0";

    JsonObject metric = scopeMetric["metrics"].add<JsonObject>();
    metric["name"] = name;
    metric["unit"] = unit;
    metric["type"] = "sum";

    JsonObject sum = metric["sum"].to<JsonObject>();
    sum["isMonotonic"] = true;
    sum["aggregationTemporality"] = 2;

    JsonObject dp = sum["dataPoints"].add<JsonObject>();
    config.addResourceAttributes(dp);
    dp["timeUnixNano"] = (unsigned long long)(millis()) * 1000000ULL;
    dp["asDouble"] = count;

    OTelSender::sendJson("/v1/metrics", doc);
  }
};

class OTelHistogram : public OTelMetricBase {
public:
  using OTelMetricBase::OTelMetricBase;

  void record(double value) {
    // no size parameters, use the new JsonDocument
    JsonDocument doc;

    // resourceMetrics → array
    JsonArray resourceMetrics = doc["resourceMetrics"].to<JsonArray>();
    JsonObject resourceMetric = resourceMetrics.add<JsonObject>();

    // resource + attributes
    JsonObject resource = resourceMetric["resource"].to<JsonObject>();
    config.addResourceAttributes(resource);

    // scopeMetrics → array
    JsonArray scopeMetrics = resourceMetric["scopeMetrics"].to<JsonArray>();
    JsonObject scopeMetric = scopeMetrics.add<JsonObject>();

    // scope
    JsonObject scope = scopeMetric["scope"].to<JsonObject>();
    scope["name"]    = "otel-embedded";
    scope["version"] = "0.1.0";

    // metrics → array
    JsonArray metrics = scopeMetric["metrics"].to<JsonArray>();
    JsonObject metric = metrics.add<JsonObject>();
    metric["name"] = name;
    metric["unit"] = unit;

    // histogram object
    JsonObject histogram = metric["histogram"].to<JsonObject>();

    // dataPoints → array
    JsonArray dataPoints = histogram["dataPoints"].to<JsonArray>();
    JsonObject dp = dataPoints.add<JsonObject>();

    // attach resource attrs, timestamp, and value
    config.addResourceAttributes(dp);
    dp["timeUnixNano"] = (unsigned long long)millis() * 1000000ULL;
    dp["asDouble"]     = value;

    // ship it off
    OTelSender::sendJson("/v1/metrics", doc);
  }
};

} // namespace OTel

#endif


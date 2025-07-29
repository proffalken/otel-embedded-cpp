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

} // namespace OTel

#endif


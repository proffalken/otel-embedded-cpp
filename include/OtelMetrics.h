#ifndef OTEL_METRICS_H
#define OTEL_METRICS_H

#include <ArduinoJson.h>
#include "OtelDefaults.h"
#include "OtelSender.h"

namespace OTel {

  // — a single, shared ResourceConfig for all metrics —
  static inline OTelResourceConfig& defaultMetricResource() {
    static OTelResourceConfig rc;
    return rc;    // <-- returns a reference, not a temporary
  }

 struct Metrics {
    /// Call once in setup() to stamp service attributes on all metrics
    static void begin(const String& serviceName,
                      const String& serviceNamespace,
                      const String& instanceId,
                      const String& version = "") {
      auto& rc = defaultMetricResource();
      rc.setAttribute("service.name",       serviceName);
      rc.setAttribute("service.namespace",  serviceNamespace);
      rc.setAttribute("service.instance.id", instanceId);
      if (!version.isEmpty()) {
        rc.setAttribute("service.version", version);
      }
    }
  };

class OTelMetricBase {
protected:
  String name;
  String unit;
  OTelResourceConfig& config = OTel::defaultMetricResource();
  //OTelResourceConfig config;

public:
    // ctor must initialize config from the ref‑returning function
  OTelMetricBase(const String& metricName,
                 const String& metricUnit)
    : name(metricName),
      unit(metricUnit),
      config(OTel::defaultMetricResource())  // <-- now an lvalue reference
  {}
};

#include <ArduinoJson.h>
#include "OtelDefaults.h"   // provides OTelResourceConfig
#include "OtelSender.h"     // provides OTelSender::sendJson()

class OTelGauge : public OTelMetricBase {
public:
  using OTelMetricBase::OTelMetricBase;

  void set(float value) {
    // elastic, heap‑backed document—no need to pick static vs dynamic.
    JsonDocument doc;

    // ── resourceMetrics → [ { … } ]
    JsonArray resourceMetrics = doc["resourceMetrics"].to<JsonArray>();
    JsonObject resourceMetric = resourceMetrics.add<JsonObject>();

    //    └─ resource
    JsonObject resource = resourceMetric["resource"].to<JsonObject>();
    config.addResourceAttributes(resource);

    // ── scopeMetrics → [ { … } ]
    JsonArray scopeMetrics = resourceMetric["scopeMetrics"].to<JsonArray>();
    JsonObject scopeMetric = scopeMetrics.add<JsonObject>();

    //    └─ scope
    JsonObject scope = scopeMetric["scope"].to<JsonObject>();
    scope["name"]    = "otel-embedded";
    scope["version"] = "0.1.0";

    //    └─ metrics → [ { … } ]
    JsonArray metrics = scopeMetric["metrics"].to<JsonArray>();
    JsonObject metric = metrics.add<JsonObject>();
    metric["name"] = name;
    metric["unit"] = unit;
    metric["type"] = "gauge";

    //      └─ gauge.dataPoints → [ { … } ]
    JsonObject gauge       = metric["gauge"].to<JsonObject>();
    JsonArray  dataPoints  = gauge["dataPoints"].to<JsonArray>();
    JsonObject dp          = dataPoints.add<JsonObject>();

    //         ├─ attach the same resource labels on each point
    config.addResourceAttributes(dp);

    //         ├─ timestamp & value
    dp["timeUnixNano"] = nowUnixNano();
    dp["asDouble"]     = value;

    // send it off
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
    dp["timeUnixNano"] = nowUnixNano();
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
    metric["type"] = "histogram";

    // histogram object
    JsonObject histogram = metric["histogram"].to<JsonObject>();
    histogram["aggregationTemporality"] = 2;

    // dataPoints → array
    JsonArray dataPoints = histogram["dataPoints"].to<JsonArray>();
    JsonObject dp = dataPoints.add<JsonObject>();

    // attach resource attrs, timestamp, and value
    config.addResourceAttributes(dp);
    dp["timeUnixNano"] = nowUnixNano();
    dp["asDouble"]     = value;

    // ship it off
    OTelSender::sendJson("/v1/metrics", doc);
  }
};

} // namespace OTel

#endif


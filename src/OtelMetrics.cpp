#include "OtelMetrics.h"
#include "OtelDefaults.h"
#include "OtelSender.h"

namespace OTel {

void OTelMetricBase::addCommonResource(JsonDocument& doc) {
    JsonObject resource = doc.createNestedObject("resource");
    getDefaultResource().addResourceAttributes(resource);
}

void OTelGauge::set(float value) {
    DynamicJsonDocument doc(1024);
    JsonObject resourceMetric = doc.createNestedObject("resource_metrics");
    getDefaultResource().addResourceAttributes(resourceMetric.createNestedObject("resource"));

    JsonArray scopeMetrics = resourceMetric.createNestedArray("scope_metrics");
    JsonObject scopeMetric = scopeMetrics.createNestedObject();
    JsonArray metrics = scopeMetric.createNestedArray("metrics");

    JsonObject metric = metrics.createNestedObject();
    metric["name"] = name;
    metric["unit"] = unit;
    JsonObject gauge = metric.createNestedObject("gauge");
    JsonArray dataPoints = gauge.createNestedArray("data_points");

    JsonObject point = dataPoints.createNestedObject();
    point["as_double"] = value;
    point["time_unix_nano"] = millis() * 1000000ULL;

    OTelSender::sendJson("/v1/metrics", doc);
}

void OTelCounter::inc(float value) {
    DynamicJsonDocument doc(1024);
    JsonObject resourceMetric = doc.createNestedObject("resource_metrics");
    getDefaultResource().addResourceAttributes(resourceMetric.createNestedObject("resource"));

    JsonArray scopeMetrics = resourceMetric.createNestedArray("scope_metrics");
    JsonObject scopeMetric = scopeMetrics.createNestedObject();
    JsonArray metrics = scopeMetric.createNestedArray("metrics");

    JsonObject metric = metrics.createNestedObject();
    metric["name"] = name;
    metric["unit"] = unit;
    JsonObject counter = metric.createNestedObject("sum");
    counter["is_monotonic"] = true;
    counter["aggregation_temporality"] = 2;

    JsonArray dataPoints = counter.createNestedArray("data_points");

    JsonObject point = dataPoints.createNestedObject();
    point["as_double"] = value;
    point["time_unix_nano"] = millis() * 1000000ULL;

    OTelSender::sendJson("/v1/metrics", doc);
}

} // namespace OTel


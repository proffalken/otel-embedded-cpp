// src/OtelMetrics.cpp

#include "OtelMetrics.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>

// Allow build-time override of collector host
#ifndef OTEL_COLLECTOR_HOST
#define OTEL_COLLECTOR_HOST "http://localhost:4318"
#endif

// Initialise static collector host from macro
String OTelSender::collectorHost = OTEL_COLLECTOR_HOST;

// Helper: get current UNIX time in nanoseconds (placeholder using millis)
uint64_t OTelMetricBase::getUnixTime() {
  return ((uint64_t)millis()) * 1000000ULL;
}

// Adds OTLP-compliant resource info
void OTelMetricBase::addCommonResource(JsonDocument &doc) {
  JsonObject resourceMetrics = doc.createNestedArray("resourceMetrics").createNestedObject();
  JsonObject resource = resourceMetrics.createNestedObject("resource");
  JsonArray attrs = resource.createNestedArray("attributes");
  config.serializeAttributes(attrs);
  resource["droppedAttributesCount"] = 0;

  JsonObject scopeMetrics = resourceMetrics.createNestedArray("scopeMetrics").createNestedObject();
  JsonObject scope = scopeMetrics.createNestedObject("scope");
  scope["name"] = "otel-embedded-cpp";
  scope["version"] = "0.2.0";
  JsonArray metrics = scopeMetrics.createNestedArray("metrics");

  // Store for later use
  doc["__metrics"] = metrics;
}

// Gauge Metric: sends value as OTLP Gauge
void OTelGauge::set(float value) {
  StaticJsonDocument<1024> doc;
  addCommonResource(doc);
  JsonArray metrics = doc["__metrics"];

  JsonObject metric = metrics.createNestedObject();
  metric["name"] = name;
  metric["unit"] = unit;

  JsonObject gauge = metric.createNestedObject("gauge");
  JsonArray points = gauge.createNestedArray("dataPoints");

  JsonObject point = points.createNestedObject();
  point["timeUnixNano"] = getUnixTime();
  point["asDouble"] = value;

  doc.remove("__metrics");

  sendJson(doc, "/v1/metrics");
}

// Counter Metric: adds to total and sends OTLP Sum
void OTelCounter::inc(float by) {
  if (startTime == 0) startTime = getUnixTime();
  total += by;

  StaticJsonDocument<1024> doc;
  addCommonResource(doc);
  JsonArray metrics = doc["__metrics"];

  JsonObject metric = metrics.createNestedObject();
  metric["name"] = name;
  metric["unit"] = unit;

  JsonObject sum = metric.createNestedObject("sum");
  sum["aggregationTemporality"] = 2; // CUMULATIVE
  sum["isMonotonic"] = true;

  JsonArray points = sum.createNestedArray("dataPoints");
  JsonObject point = points.createNestedObject();
  point["startTimeUnixNano"] = startTime;
  point["timeUnixNano"] = getUnixTime();
  point["asDouble"] = total;

  doc.remove("__metrics");

  sendJson(doc, "/v1/metrics");
}

// Histogram constructor
OTelHistogram::OTelHistogram(String n, std::vector<double> explicitBounds)
  : OTelMetricBase(n), bounds(explicitBounds) {
  bucketCounts.resize(bounds.size() + 1, 0); // +1 for overflow bucket
  startTime = getUnixTime();
}

// Histogram: records value and sends OTLP Histogram
void OTelHistogram::record(double value) {
  count++;
  sum += value;

  size_t bucket = bounds.size(); // default to overflow
  for (size_t i = 0; i < bounds.size(); ++i) {
    if (value <= bounds[i]) {
      bucket = i;
      break;
    }
  }
  bucketCounts[bucket]++;

  StaticJsonDocument<2048> doc;
  addCommonResource(doc);
  JsonArray metrics = doc["__metrics"];

  JsonObject metric = metrics.createNestedObject();
  metric["name"] = name;
  metric["unit"] = unit;

  JsonObject hist = metric.createNestedObject("histogram");
  hist["aggregationTemporality"] = 2; // CUMULATIVE

  JsonArray points = hist.createNestedArray("dataPoints");
  JsonObject point = points.createNestedObject();
  point["startTimeUnixNano"] = startTime;
  point["timeUnixNano"] = getUnixTime();
  point["count"] = count;
  point["sum"] = sum;

  JsonArray bCounts = point.createNestedArray("bucketCounts");
  for (auto c : bucketCounts) bCounts.add(c);

  JsonArray boundsArray = point.createNestedArray("explicitBounds");
  for (auto b : bounds) boundsArray.add(b);

  doc.remove("__metrics");

  sendJson(doc, "/v1/metrics");
}


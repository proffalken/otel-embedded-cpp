// OtelMetrics.h
#ifndef OTEL_METRICS_H
#define OTEL_METRICS_H

#include <Arduino.h>
#include <map>
#include <initializer_list>
#include <ArduinoJson.h>
#include "OtelDefaults.h"   // expects: nowUnixNano()
#include "OtelSender.h"     // expects: OTelSender::sendJson(path, doc)
#include "OtelTracer.h"     // reuses: u64ToStr(), defaultServiceName(), defaultServiceInstanceId(), defaultHostName(), addResAttr()

namespace OTel {

// ---- Instrumentation scope for metrics --------------------------------------
struct MetricsScopeConfig {
  String scopeName{"otel-embedded"};
  String scopeVersion{"0.1.0"};
};

static inline MetricsScopeConfig& metricsScopeConfig() {
  static MetricsScopeConfig cfg;
  return cfg;
}

// ---- Default metric labels (merged into each datapoint's attributes) --------
static inline std::map<String, String>& defaultMetricLabels() {
  static std::map<String, String> labels;
  return labels;
}

class Metrics {
public:
  // Configure the instrumentation scope name/version for metrics
  static void begin(const String& scopeName, const String& scopeVersion) {
    metricsScopeConfig().scopeName    = scopeName;
    metricsScopeConfig().scopeVersion = scopeVersion;
  }

  // Set/merge defaults applied to *every* datapoint
  static void setDefaultMetricLabels(const std::map<String, String>& labels) {
    defaultMetricLabels() = labels;
  }
  static void setDefaultMetricLabel(const String& key, const String& value) {
    defaultMetricLabels()[key] = value;
  }

  // --------- GAUGE (double) ----------
  // Convenience with std::map
  static void gauge(const String& name, double value,
                    const String& unit = "1",
                    const std::map<String,String>& labels = {}) {
    buildAndSendGauge(name, value, unit, labels);
  }

  // Convenience with initializer_list
  static void gauge(const String& name, double value,
                    const String& unit,
                    std::initializer_list<std::pair<const char*, const char*>> kvs) {
    std::map<String, String> labels;
    for (auto &kv : kvs) labels[String(kv.first)] = String(kv.second);
    buildAndSendGauge(name, value, unit, labels);
  }

  // --------- SUM (double) -------------
  // OTLP requires temporality + monotonic flags for Sum
  static void sum(const String& name, double value,
                  bool isMonotonic = false,
                  const String& temporality = "DELTA", // or "CUMULATIVE"
                  const String& unit = "1",
                  const std::map<String,String>& labels = {}) {
    buildAndSendSum(name, value, isMonotonic, temporality, unit, labels);
  }

  static void sum(const String& name, double value,
                  bool isMonotonic,
                  const String& temporality,
                  const String& unit,
                  std::initializer_list<std::pair<const char*, const char*>> kvs) {
    std::map<String, String> labels;
    for (auto &kv : kvs) labels[String(kv.first)] = String(kv.second);
    buildAndSendSum(name, value, isMonotonic, temporality, unit, labels);
  }

private:
  static void buildAndSendGauge(const String& name, double value,
                                const String& unit,
                                const std::map<String,String>& labels);

  static void buildAndSendSum(const String& name, double value,
                              bool isMonotonic,
                              const String& temporality,
                              const String& unit,
                              const std::map<String,String>& labels);
};

} // namespace OTel

#endif // OTEL_METRICS_H


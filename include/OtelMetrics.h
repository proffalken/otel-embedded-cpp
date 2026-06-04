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

/** Instrumentation scope name and version emitted on every metrics payload. */
struct MetricsScopeConfig {
  String scopeName{"otel-embedded"};
  String scopeVersion{"0.1.0"};
};

/** Returns the process-wide MetricsScopeConfig singleton. */
inline MetricsScopeConfig& metricsScopeConfig() {
  static MetricsScopeConfig cfg;
  return cfg;
}

/** Returns the process-wide default metric labels map (merged into every datapoint). */
inline std::map<String, String>& defaultMetricLabels() {
  static std::map<String, String> labels;
  return labels;
}

/**
 * Static façade for emitting OTLP metrics (gauges and sums).
 *
 * Call @c begin() once after connecting to Wi-Fi to set the instrumentation
 * scope name/version.  Then call @c gauge() or @c sum() freely from loop().
 */
class Metrics {
public:
  /**
   * Configure the instrumentation scope name and version for all metrics.
   * @param scopeName    Library/component name, e.g. "my-firmware".
   * @param scopeVersion Semver string, e.g. "1.0.0".
   */
  static void begin(const String& scopeName, const String& scopeVersion) {
    metricsScopeConfig().scopeName    = scopeName;
    metricsScopeConfig().scopeVersion = scopeVersion;
  }

  /**
   * Replace the full set of default labels applied to every datapoint.
   * @param labels Map of attribute key/value pairs.
   */
  static void setDefaultMetricLabels(const std::map<String, String>& labels) {
    defaultMetricLabels() = labels;
  }

  /**
   * Set or update a single default label applied to every datapoint.
   * @param key   Attribute key.
   * @param value Attribute value.
   */
  static void setDefaultMetricLabel(const String& key, const String& value) {
    defaultMetricLabels()[key] = value;
  }

  /**
   * Emit a gauge datapoint.
   * @param name   Metric name (e.g. "cpu.usage").
   * @param value  Measured value.
   * @param unit   UCUM unit string, e.g. "1", "By", "ms".
   * @param labels Per-call attributes merged on top of default labels.
   */
  static void gauge(const String& name, double value,
                    const String& unit = "1",
                    const std::map<String,String>& labels = {}) {
    buildAndSendGauge(name, value, unit, labels);
  }

  /**
   * Emit a gauge datapoint using an initializer-list of attributes.
   * @param name   Metric name.
   * @param value  Measured value.
   * @param unit   UCUM unit string.
   * @param kvs    Brace-enclosed attribute pairs, e.g. {{"core","0"},{"host","esp1"}}.
   */
  static void gauge(const String& name, double value,
                    const String& unit,
                    std::initializer_list<std::pair<const char*, const char*>> kvs) {
    std::map<String, String> labels;
    for (auto &kv : kvs) labels[String(kv.first)] = String(kv.second);
    buildAndSendGauge(name, value, unit, labels);
  }

  /**
   * Emit a sum (counter/cumulative) datapoint.
   * @param name         Metric name.
   * @param value        Measured value.
   * @param isMonotonic  True for counters that only increase.
   * @param temporality  "DELTA" or "CUMULATIVE".
   * @param unit         UCUM unit string.
   * @param labels       Per-call attributes merged on top of default labels.
   */
  static void sum(const String& name, double value,
                  bool isMonotonic = false,
                  const String& temporality = "DELTA",
                  const String& unit = "1",
                  const std::map<String,String>& labels = {}) {
    buildAndSendSum(name, value, isMonotonic, temporality, unit, labels);
  }

  /**
   * Emit a sum datapoint using an initializer-list of attributes.
   * @param name         Metric name.
   * @param value        Measured value.
   * @param isMonotonic  True for monotonic counters.
   * @param temporality  "DELTA" or "CUMULATIVE".
   * @param unit         UCUM unit string.
   * @param kvs          Brace-enclosed attribute pairs.
   */
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
  /** Build the OTLP gauge payload and hand it to OTelSender. */
  static void buildAndSendGauge(const String& name, double value,
                                const String& unit,
                                const std::map<String,String>& labels);

  /** Build the OTLP sum payload and hand it to OTelSender. */
  static void buildAndSendSum(const String& name, double value,
                              bool isMonotonic,
                              const String& temporality,
                              const String& unit,
                              const std::map<String,String>& labels);
};

} // namespace OTel

#endif // OTEL_METRICS_H

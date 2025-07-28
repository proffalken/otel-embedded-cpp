#ifndef OTEL_METRICS_H
#define OTEL_METRICS_H

#include <ArduinoJson.h>
#include <OtelDefaults.h>
#include <OtelSender.h>

class OTelMetricBase {
public:
  OTelMetricBase(String metricName, String metricUnit)
      : name(metricName), unit(metricUnit) {}

  virtual ~OTelMetricBase() {}

protected:
  String name;
  String unit;

  void addCommonResource(JsonDocument &doc);
};

class OTelGauge : public OTelMetricBase, public OtelSender {
public:
  OTelGauge(String metricName, String metricUnit = "")
      : OTelMetricBase(metricName, metricUnit) {}

  void set(float value);
};

class OTelCounter : public OTelMetricBase, public OtelSender {
public:
  OTelCounter(String metricName, String metricUnit = "")
      : OTelMetricBase(metricName, metricUnit) {}

  void inc(float value);
};

#endif


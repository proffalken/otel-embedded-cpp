#pragma once
#include <ArduinoJson.h>
#include "OtelSender.h"
#include "OtelDefaults.h"

class OTelMetricBase : public OTelSender {
protected:
    String name;
    String unit;
    OTelResourceConfig config;

    void addCommonResource(JsonDocument &doc);

public:
    OTelMetricBase(String metricName, String metricUnit = "")
        : name(metricName), unit(metricUnit), config(getDefaultResource()) {}
};

class OTelGauge : public OTelMetricBase {
public:
    using OTelMetricBase::OTelMetricBase;
    void set(float value);
};

class OTelCounter : public OTelMetricBase {
public:
    using OTelMetricBase::OTelMetricBase;
    void inc(float value = 1.0f);
};

class OTelHistogram : public OTelMetricBase {
public:
    using OTelMetricBase::OTelMetricBase;
    void record(double value);
};


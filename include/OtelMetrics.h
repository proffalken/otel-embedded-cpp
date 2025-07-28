#pragma once

#include <ArduinoJson.h>
#include <Arduino.h>
#include <HTTPClient.h>

// === Base Transport for Telemetry ===

class OTelSender {
protected:
  static String collectorHost;

  static void sendJson(JsonDocument &doc, const char *endpoint) {
    String json;
    serializeJson(doc, json);
    HTTPClient http;
    http.begin(collectorHost + String(endpoint));
    http.addHeader("Content-Type", "application/json");
    http.POST(json);
    http.end();
  }

public:
  static void setCollectorHost(const String &host) {
    collectorHost = host;
  }
};

// === Resource Configuration ===

struct OTelResourceConfig {
  String serviceName;
  String serviceNamespace;
  String serviceVersion;
  String serviceInstanceId;
  String environment;

  OTelResourceConfig(
    String name = "embedded-app",
    String ns = "",
    String version = "",
    String instanceId = "",
    String env = "dev")
    : serviceName(name), serviceNamespace(ns),
      serviceVersion(version), serviceInstanceId(instanceId),
      environment(env) {}

  void serializeAttributes(JsonArray attrs) const {
    attrs.add(makeAttribute("service.name", serviceName));
    if (serviceNamespace.length()) attrs.add(makeAttribute("service.namespace", serviceNamespace));
    if (serviceVersion.length())   attrs.add(makeAttribute("service.version", serviceVersion));
    if (serviceInstanceId.length())attrs.add(makeAttribute("service.instance.id", serviceInstanceId));
    if (environment.length())      attrs.add(makeAttribute("deployment.environment", environment));
  }

private:
  JsonObject makeAttribute(const String &k, const String &v) const {
    DynamicJsonDocument doc(64);
    JsonObject attr = doc.to<JsonObject>();
    attr["key"] = k;
    attr["value"]["stringValue"] = v;
    return attr;
  }
};

// === Metrics Types ===

class OTelMetricBase : public OTelSender {
protected:
  String name;
  String unit;
  OTelResourceConfig config;

  uint64_t getUnixTime();
  void addCommonResource(JsonDocument &doc);

public:
  OTelMetricBase(String metricName, String metricUnit = "1")
    : name(metricName), unit(metricUnit), config(OTel::getDefaultResource()) {}

  void setResource(const OTelResourceConfig &rc) { config = rc; }
};

class OTelGauge : public OTelMetricBase {
public:
  OTelGauge(String name, String unit = "1") : OTelMetricBase(name, unit) {}
  void set(float value);
};

class OTelCounter : public OTelMetricBase {
private:
  float total = 0;
  uint64_t startTime = 0;

public:
  OTelCounter(String name, String unit = "1") : OTelMetricBase(name, unit) {}
  void inc(float by = 1.0);
};

class OTelHistogram : public OTelMetricBase {
private:
  std::vector<double> bounds;
  std::vector<uint32_t> bucketCounts;
  double sum = 0;
  uint32_t count = 0;
  uint64_t startTime = 0;

public:
  OTelHistogram(String name, std::vector<double> explicitBounds);
  void record(double value);
};


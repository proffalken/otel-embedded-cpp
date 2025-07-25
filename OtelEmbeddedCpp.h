// OTelEmbeddedCpp.h
// Unified header for ESP32, RP2040 (Pico W), and ESP8266

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
#elif defined(ESP32) || defined(ARDUINO_ARCH_RP2040)
  #include <WiFi.h>
  #include <HTTPClient.h>
#else
  #error "Unsupported platform: OTelEmbeddedCpp only supports ESP32, RP2040, and ESP8266."
#endif

#include <sys/time.h>

namespace OTel {

struct KeyValue {
  String key;
  String value;
};

namespace Internal {
  inline uint64_t currentTimeUnixNano() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000000ULL
         + static_cast<uint64_t>(tv.tv_usec) * 1000ULL;
  }
}

class Logger {
public:
  static void begin(const String& serviceName, const String& endpointHost, const String& version) {
    // Initialize NTP (requires WiFi connected)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    struct timeval tv;
    // Wait for time sync
    for (int i = 0; i < 20; ++i) {
      gettimeofday(&tv, nullptr);
      if (tv.tv_sec > 100000) break;
      delay(500);
    }
    _serviceName = serviceName;
    _endpointHost = endpointHost;
    _version = version;
  }

  static void logInfo(const String& message) {
    sendLog("INFO", message);
  }

  static void sendLog(const String& level, const String& message) {
    HTTPClient http;
    String url = String("http://") + _endpointHost + "/v1/logs";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    uint64_t now = Internal::currentTimeUnixNano();

    DynamicJsonDocument doc(1024);
    JsonArray resourceLogs = doc.createNestedArray("resourceLogs");
    JsonObject resourceLog = resourceLogs.createNestedObject();
    JsonArray scopeLogs = resourceLog.createNestedArray("scopeLogs");
    JsonObject scopeLog = scopeLogs.createNestedObject();
    JsonArray logRecords = scopeLog.createNestedArray("logRecords");
    JsonObject logRecord = logRecords.createNestedObject();

    logRecord["time_unix_nano"] = String(now);
    logRecord["severity_text"] = level;
    logRecord["body"]["stringValue"] = message;

    String output;
    serializeJson(doc, output);
    http.POST(output);
    http.end();
  }

private:
  static String _serviceName;
  static String _endpointHost;
  static String _version;
};

class Tracer {
public:
  static String startSpan(const String& name, const String& parentSpanId = "") {
    String spanId = String(random(0xFFFFFFFF), HEX);
    uint64_t startTime = Internal::currentTimeUnixNano();
    _spans[spanId] = startTime;
    _spanNames[spanId] = name;
    _parentMap[spanId] = parentSpanId;
    return spanId;
  }

  static void endSpan(const String& spanId) {
    auto it = _spans.find(spanId);
    if (it == _spans.end()) return;
    uint64_t start = it->second;
    uint64_t end = Internal::currentTimeUnixNano();
    String name = _spanNames[spanId];
    String parent = _parentMap[spanId];

    HTTPClient http;
    String url = String("http://") + Logger::_endpointHost + "/v1/traces";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    DynamicJsonDocument doc(2048);
    JsonArray resourceSpans = doc.createNestedArray("resourceSpans");
    JsonObject resourceSpan = resourceSpans.createNestedObject();
    JsonArray scopeSpans = resourceSpan.createNestedArray("scopeSpans");
    JsonObject scopeSpan = scopeSpans.createNestedObject();
    JsonArray spans = scopeSpan.createNestedArray("spans");
    JsonObject span = spans.createNestedObject();

    span["span_id"] = spanId;
    span["trace_id"] = spanId + "trace";
    span["name"] = name;
    span["start_time_unix_nano"] = String(start);
    span["end_time_unix_nano"] = String(end);
    if (parent.length() > 0) span["parent_span_id"] = parent;

    String output;
    serializeJson(doc, output);
    http.POST(output);
    http.end();
  }

private:
  static std::map<String, uint64_t> _spans;
  static std::map<String, String> _spanNames;
  static std::map<String, String> _parentMap;
};

class Gauge {
public:
  explicit Gauge(const String& name) : _name(name) {}

  void set(float value, std::vector<KeyValue> labels = {}) {
    HTTPClient http;
    String url = String("http://") + Logger::_endpointHost + "/v1/metrics";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    uint64_t now = Internal::currentTimeUnixNano();

    DynamicJsonDocument doc(2048);
    JsonArray resourceMetrics = doc.createNestedArray("resourceMetrics");
    JsonObject resourceMetric = resourceMetrics.createNestedObject();
    JsonArray scopeMetrics = resourceMetric.createNestedArray("scopeMetrics");
    JsonObject scopeMetric = scopeMetrics.createNestedObject();
    JsonArray metrics = scopeMetric.createNestedArray("metrics");
    JsonObject metric = metrics.createNestedObject();

    metric["name"] = _name;
    metric["type"] = "gauge";
    JsonObject gauge = metric.createNestedObject("gauge");
    JsonArray dataPoints = gauge.createNestedArray("dataPoints");
    JsonObject dp = dataPoints.createNestedObject();
    dp["as_double"] = value;
    dp["time_unix_nano"] = String(now);
    dp["observed_time_unix_nano"] = String(now);

    JsonArray attrs = dp.createNestedArray("attributes");
    for (auto& kv : labels) {
      JsonObject attr = attrs.createNestedObject();
      attr["key"] = kv.key;
      attr["value"]["stringValue"] = kv.value;
    }

    String output;
    serializeJson(doc, output);
    http.POST(output);
    http.end();
  }

private:
  String _name;
};

// Static member definitions
inline String Logger::_serviceName = "";
inline String Logger::_endpointHost = "";
inline String Logger::_version = "";
inline std::map<String, uint64_t> Tracer::_spans;
inline std::map<String, String> Tracer::_spanNames;
inline std::map<String, String> Tracer::_parentMap;

} // namespace OTel


#include <Arduino.h>      // must come before ArduinoJson.h to get matching inline namespace
#include <unity.h>
#include <ArduinoJson.h>

#include "OtelDefaults.h"
#include "OtelMetrics.h"
#include "OtelLogger.h"
#include "OtelTracer.h"
#include "fake_sender.h"

// Pull in non-inline implementations in the same TU so static-inline singletons
// (defaultResource, metricsScopeConfig, etc.) are shared with the test fixtures.
#include "../../src/OtelMetrics.cpp"

// ── Fixture ───────────────────────────────────────────────────────────────────

void setUp() {
  FakeSender::reset();

  auto& res = OTel::defaultResource();
  res.set("service.name",        "test-service");
  res.set("service.namespace",   "test-ns");
  res.set("service.instance.id", "test-001");
  res.set("host.name",           "test-host");

  OTel::Metrics::begin("otel-embedded-cpp", "0.0.1");
  OTel::Tracer::begin("otel-embedded-cpp",  "0.0.1");
}

void tearDown() {}

// ── Metrics: gauge ────────────────────────────────────────────────────────────

void test_gauge_sends_to_metrics_endpoint() {
  OTel::Metrics::gauge("cpu.usage", 0.42, "1");
  TEST_ASSERT_EQUAL_STRING("/v1/metrics", FakeSender::lastPath.c_str());
}

void test_gauge_payload_has_metric_name() {
  OTel::Metrics::gauge("cpu.usage", 0.42, "1");
  JsonDocument doc;
  deserializeJson(doc, FakeSender::lastJson);
  const char* name =
    doc["resourceMetrics"][0]["scopeMetrics"][0]["metrics"][0]["name"];
  TEST_ASSERT_EQUAL_STRING("cpu.usage", name);
}

void test_gauge_payload_has_value() {
  OTel::Metrics::gauge("cpu.usage", 0.42, "1");
  JsonDocument doc;
  deserializeJson(doc, FakeSender::lastJson);
  double val =
    doc["resourceMetrics"][0]["scopeMetrics"][0]["metrics"][0]
       ["gauge"]["dataPoints"][0]["asDouble"];
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.42, val);
}

void test_gauge_payload_has_unit() {
  OTel::Metrics::gauge("disk.reads", 100.0, "By");
  JsonDocument doc;
  deserializeJson(doc, FakeSender::lastJson);
  const char* unit =
    doc["resourceMetrics"][0]["scopeMetrics"][0]["metrics"][0]["unit"];
  TEST_ASSERT_EQUAL_STRING("By", unit);
}

void test_gauge_payload_includes_call_labels() {
  OTel::Metrics::gauge("cpu.usage", 0.5, "1", {{"core", "0"}});
  JsonDocument doc;
  deserializeJson(doc, FakeSender::lastJson);
  JsonArray attrs =
    doc["resourceMetrics"][0]["scopeMetrics"][0]["metrics"][0]
       ["gauge"]["dataPoints"][0]["attributes"];
  bool found = false;
  for (JsonObject a : attrs) {
    if (strcmp(a["key"], "core") == 0) { found = true; break; }
  }
  TEST_ASSERT_TRUE(found);
}

// ── Logs ──────────────────────────────────────────────────────────────────────

void test_log_sends_to_logs_endpoint() {
  OTel::Logger::logInfo("hello");
  TEST_ASSERT_EQUAL_STRING("/v1/logs", FakeSender::lastPath.c_str());
}

void test_log_info_severity_text() {
  OTel::Logger::logInfo("hello");
  JsonDocument doc;
  deserializeJson(doc, FakeSender::lastJson);
  const char* sev =
    doc["resourceLogs"][0]["scopeLogs"][0]["logRecords"][0]["severityText"];
  TEST_ASSERT_EQUAL_STRING("INFO", sev);
}

void test_log_warn_severity_number() {
  OTel::Logger::logWarn("watch out");
  JsonDocument doc;
  deserializeJson(doc, FakeSender::lastJson);
  int num =
    doc["resourceLogs"][0]["scopeLogs"][0]["logRecords"][0]["severityNumber"];
  TEST_ASSERT_EQUAL_INT(13, num); // WARN = 13 per OTLP spec
}

void test_log_body_string_value() {
  OTel::Logger::logInfo("hello world");
  JsonDocument doc;
  deserializeJson(doc, FakeSender::lastJson);
  const char* body =
    doc["resourceLogs"][0]["scopeLogs"][0]["logRecords"][0]["body"]["stringValue"];
  TEST_ASSERT_EQUAL_STRING("hello world", body);
}

void test_log_error_severity_text() {
  OTel::Logger::logError("boom");
  JsonDocument doc;
  deserializeJson(doc, FakeSender::lastJson);
  const char* sev =
    doc["resourceLogs"][0]["scopeLogs"][0]["logRecords"][0]["severityText"];
  TEST_ASSERT_EQUAL_STRING("ERROR", sev);
}

// ── Traces ────────────────────────────────────────────────────────────────────

void test_span_sends_to_traces_endpoint() {
  auto span = OTel::Tracer::startSpan("my-op");
  span.end();
  TEST_ASSERT_EQUAL_STRING("/v1/traces", FakeSender::lastPath.c_str());
}

void test_span_name_in_payload() {
  auto span = OTel::Tracer::startSpan("my-op");
  span.end();
  JsonDocument doc;
  deserializeJson(doc, FakeSender::lastJson);
  const char* name =
    doc["resourceSpans"][0]["scopeSpans"][0]["spans"][0]["name"];
  TEST_ASSERT_EQUAL_STRING("my-op", name);
}

void test_span_has_non_empty_trace_id() {
  auto span = OTel::Tracer::startSpan("my-op");
  span.end();
  JsonDocument doc;
  deserializeJson(doc, FakeSender::lastJson);
  const char* tid =
    doc["resourceSpans"][0]["scopeSpans"][0]["spans"][0]["traceId"];
  TEST_ASSERT_NOT_NULL(tid);
  TEST_ASSERT_TRUE(strlen(tid) > 0);
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
  UNITY_BEGIN();

  // Metrics
  RUN_TEST(test_gauge_sends_to_metrics_endpoint);
  RUN_TEST(test_gauge_payload_has_metric_name);
  RUN_TEST(test_gauge_payload_has_value);
  RUN_TEST(test_gauge_payload_has_unit);
  RUN_TEST(test_gauge_payload_includes_call_labels);

  // Logs
  RUN_TEST(test_log_sends_to_logs_endpoint);
  RUN_TEST(test_log_info_severity_text);
  RUN_TEST(test_log_warn_severity_number);
  RUN_TEST(test_log_body_string_value);
  RUN_TEST(test_log_error_severity_text);

  // Traces
  RUN_TEST(test_span_sends_to_traces_endpoint);
  RUN_TEST(test_span_name_in_payload);
  RUN_TEST(test_span_has_non_empty_trace_id);

  return UNITY_END();
}

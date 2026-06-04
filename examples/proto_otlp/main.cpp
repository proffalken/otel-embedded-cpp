/*
 * proto_otlp — send traces, metrics, and logs over OTLP/HTTP using binary
 * protobuf encoding instead of the default JSON.
 *
 * Protobuf produces smaller payloads (typically 30–60 % smaller than JSON for
 * the same telemetry) and is faster to serialise on-device.  It is the
 * preferred encoding for bandwidth-constrained devices or high-frequency
 * telemetry.
 *
 * To enable protobuf, add ONE build flag:
 *
 *   build_flags =
 *     -DOTEL_EXPORTER_OTLP_PROTOCOL=OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF
 *
 * All other configuration (endpoint, headers, TLS) is identical to JSON mode.
 * The application code below is exactly the same as the direct_otlp example —
 * only the build flag differs.
 *
 * Protobuf also requires the nanopb library:
 *
 *   lib_deps =
 *     bblanchon/ArduinoJson@^7.0.0
 *     nanopb/Nanopb@^0.4.91
 *
 * ── Datadog (US1) example ────────────────────────────────────────────────────
 *
 *   build_flags =
 *     -DOTEL_EXPORTER_OTLP_PROTOCOL=OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF
 *     -DOTEL_EXPORTER_OTLP_ENDPOINT="\"https://otlp.datadoghq.com\""
 *     -DOTEL_EXPORTER_OTLP_HEADERS="\"dd-api-key=${sysenv.DD_API_KEY}\""
 *
 * ── Grafana Cloud example ────────────────────────────────────────────────────
 *
 *   build_flags =
 *     -DOTEL_EXPORTER_OTLP_PROTOCOL=OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF
 *     -DOTEL_EXPORTER_OTLP_ENDPOINT="\"https://<instance>.grafana.net/otlp\""
 *     -DOTEL_EXPORTER_OTLP_HEADERS="\"Authorization=Basic <base64-token>\""
 *
 * ── Local OpenTelemetry Collector ────────────────────────────────────────────
 *
 *   build_flags =
 *     -DOTEL_EXPORTER_OTLP_PROTOCOL=OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF
 *     -DOTEL_EXPORTER_OTLP_ENDPOINT="\"http://192.168.1.10:4318\""
 *
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * Buffer sizing: the protobuf encoder uses a stack buffer for each signal.
 * The default is 1024 bytes.  Increase it if you have many attributes or long
 * string values and see silent drops:
 *
 *   -DOTEL_PROTO_BUFFER_SIZE=2048
 *
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <Arduino.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_RP2040)
  #include <WiFi.h>
#else
  #error "Unsupported platform: must be ESP8266, ESP32, or RP2040"
#endif

#include <time.h>

#include "OtelSender.h"
#include "OtelLogger.h"
#include "OtelTracer.h"
#include "OtelMetrics.h"

// ── Build-time defaults (override via -D flags in platformio.ini) ─────────────

#ifndef WIFI_SSID
#define WIFI_SSID "your-ssid"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "your-password"
#endif

#ifndef OTEL_DEPLOY_ENV
#define OTEL_DEPLOY_ENV "dev"
#endif

// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t SEND_INTERVAL_MS = 10000;

void setup() {
  Serial.begin(115200);

  Serial.printf("Connecting to %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println("\nWi-Fi connected");

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP");
  while (time(nullptr) < 1609459200UL) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  OTel::Tracer::begin("proto-otlp-example", "1.0.0");
  OTel::Metrics::begin("proto-otlp-example", "1.0.0");

  OTel::Metrics::setDefaultMetricLabel("device.id",  "esp32-abc123");
  OTel::Metrics::setDefaultMetricLabel("deploy.env", OTEL_DEPLOY_ENV);
  OTel::Logger::setDefaultLabel("device.id",         "esp32-abc123");
  OTel::Logger::setDefaultLabel("deploy.env",        OTEL_DEPLOY_ENV);

#if defined(ARDUINO_ARCH_RP2040)
  OTelSender::beginAsyncWorker();
#endif

#if OTEL_EXPORTER_OTLP_PROTOCOL == OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF
  Serial.println("OTel ready — encoding: protobuf");
#else
  Serial.println("OTel ready — encoding: JSON");
#endif
  Serial.println("Endpoint: " OTEL_EXPORTER_OTLP_ENDPOINT);
}

void loop() {
  {
    auto span = OTel::Tracer::startSpan("sensor-read");

    span.setAttribute("sensor.id",      "1");
    span.setAttribute("sensor.type",    "temperature");
    span.setAttribute("sensor.channel", (int64_t)0);
    span.setAttribute("sensor.enabled", true);

    OTel::Logger::logInfo("Reading sensor", {{"sensor.id", "1"}});

    float temperature  = 20.0f + (random(0, 100) / 10.0f);
    bool  overThreshold = temperature > 28.0f;

    if (overThreshold) {
      span.addEvent("threshold-crossed", {{"threshold.celsius", "28.0"}});
      OTel::Logger::logWarn("Temperature above threshold",
                            {{"sensor.id", "1"},
                             {"value.celsius", String(temperature).c_str()}});
    }

    OTel::Metrics::gauge("sensor.temperature", temperature, "Cel",
                         {{"sensor.id", "1"}});

    OTel::Metrics::sum("sensor.readings.total", 1.0,
                       /*isMonotonic=*/true, "DELTA", "1",
                       {{"sensor.id", "1"}});

    span.setAttribute("reading.celsius", (double)temperature);
    span.end();
  }

  uint32_t dropped = OTelSender::droppedCount();
  if (dropped > 0) {
    Serial.printf("[otel] warning: %u items dropped from send queue\n", dropped);
  }

  delay(SEND_INTERVAL_MS);
}

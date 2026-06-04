/*
 * direct_otlp — send traces, metrics, and logs directly to any OTLP/HTTP
 * vendor endpoint without an intermediate collector.
 *
 * All endpoint and authentication configuration lives in build flags so no
 * credentials are baked into source.  The relevant flags are:
 *
 *   OTEL_EXPORTER_OTLP_ENDPOINT        Base URL for all signals (paths appended
 *                                       automatically).  Overrides the legacy
 *                                       OTEL_COLLECTOR_BASE_URL if both are set.
 *
 *   OTEL_EXPORTER_OTLP_HEADERS         Headers applied to every signal, as a
 *                                       comma-separated "key=value" list.
 *                                       Values containing commas must be
 *                                       percent-encoded (%2C).
 *
 *   OTEL_EXPORTER_OTLP_*_ENDPOINT      Per-signal endpoint override (used
 *   OTEL_EXPORTER_OTLP_*_HEADERS       verbatim, no path appended).  Useful
 *                                       when routing signals to different
 *                                       backends, or when a vendor requires
 *                                       signal-specific authentication.
 *
 * HTTPS is enabled automatically when an endpoint URL starts with "https://".
 * Certificate validation is skipped by default (OTEL_TLS_INSECURE=1).
 *
 * ── Datadog (US1) example ────────────────────────────────────────────────────
 *
 *   build_flags =
 *     -DOTEL_EXPORTER_OTLP_ENDPOINT="\"https://otlp.datadoghq.com\""
 *     -DOTEL_EXPORTER_OTLP_HEADERS="\"dd-api-key=${sysenv.DD_API_KEY}\""
 *
 *   Or route signals to separate endpoints with per-signal headers:
 *
 *     -DOTEL_EXPORTER_OTLP_LOGS_ENDPOINT="\"https://otlp.datadoghq.com/v1/logs\""
 *     -DOTEL_EXPORTER_OTLP_TRACES_ENDPOINT="\"https://otlp.datadoghq.com/v1/traces\""
 *     -DOTEL_EXPORTER_OTLP_METRICS_ENDPOINT="\"https://otlp.datadoghq.com/v1/metrics\""
 *     -DOTEL_EXPORTER_OTLP_METRICS_HEADERS="\"dd-api-key=${sysenv.DD_API_KEY},dd-otel-metric-config=%7B%22resource_attributes_as_tags%22%3Atrue%7D\""
 *
 *   Note: Datadog requires delta temporality for Sum metrics.  This library
 *   defaults sum() to DELTA, so no extra configuration is needed.
 *
 * ── Grafana Cloud example ────────────────────────────────────────────────────
 *
 *   build_flags =
 *     -DOTEL_EXPORTER_OTLP_ENDPOINT="\"https://<instance>.grafana.net/otlp\""
 *     -DOTEL_EXPORTER_OTLP_HEADERS="\"Authorization=Basic <base64-token>\""
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

// OTEL_EXPORTER_OTLP_ENDPOINT and OTEL_EXPORTER_OTLP_HEADERS are defined in
// OtelSender.h with empty defaults.  Set them via build_flags — see above.

#ifndef OTEL_DEPLOY_ENV
#define OTEL_DEPLOY_ENV "dev"
#endif

// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t SEND_INTERVAL_MS = 10000;

void setup() {
  Serial.begin(115200);

  // Connect to Wi-Fi
  Serial.printf("Connecting to %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println("\nWi-Fi connected");

  // Sync NTP so timestamps in telemetry are meaningful
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP");
  while (time(nullptr) < 1609459200UL) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  // Initialise tracer and metrics scope
  OTel::Tracer::begin("direct-otlp-example", "1.0.0");
  OTel::Metrics::begin("direct-otlp-example", "1.0.0");

  // Default attributes — merged into every metric datapoint and log record
  // automatically, without needing to pass them on each call.
  OTel::Metrics::setDefaultMetricLabel("device.id",    "esp32-abc123");
  OTel::Metrics::setDefaultMetricLabel("deploy.env",   OTEL_DEPLOY_ENV);
  OTel::Logger::setDefaultLabel("device.id",           "esp32-abc123");
  OTel::Logger::setDefaultLabel("deploy.env",          OTEL_DEPLOY_ENV);

  // On RP2040, start the core-1 async send worker after Wi-Fi is ready
#if defined(ARDUINO_ARCH_RP2040)
  OTelSender::beginAsyncWorker();
#endif

  Serial.println("OTel ready — sending to: " OTEL_EXPORTER_OTLP_ENDPOINT);
}

void loop() {
  // ── Trace ──────────────────────────────────────────────────────────────────
  {
    auto span = OTel::Tracer::startSpan("sensor-read");

    // Span attributes support multiple types: string, int, double, bool.
    // These appear as tags on the span in your backend.
    span.setAttribute("sensor.id",       "1");
    span.setAttribute("sensor.type",     "temperature");
    span.setAttribute("sensor.channel",  (int64_t)0);
    span.setAttribute("sensor.enabled",  true);

    // ── Log (correlated to the active span via traceId/spanId) ───────────────
    // Per-call labels are merged with the defaults set in setup().
    OTel::Logger::logInfo("Reading sensor", {{"sensor.id", "1"}});

    // Simulate a sensor read
    float temperature = 20.0f + (random(0, 100) / 10.0f);
    bool  overThreshold = temperature > 28.0f;

    // Span event: a timestamped annotation within the span's duration.
    if (overThreshold) {
      span.addEvent("threshold-crossed", {{"threshold.celsius", "28.0"}});
      OTel::Logger::logWarn("Temperature above threshold",
                            {{"sensor.id", "1"}, {"value.celsius", String(temperature).c_str()}});
    }

    // ── Metrics ───────────────────────────────────────────────────────────────
    // gauge: point-in-time value, no temporality required.
    // Per-call labels are merged with the defaults set in setup().
    OTel::Metrics::gauge("sensor.temperature", temperature, "Cel",
                         {{"sensor.id", "1"}});

    // sum: monotonic counter. Pass 1.0 per call with DELTA temporality so
    // the value represents the increment since the last report, not a running
    // total. Use CUMULATIVE + an accumulator if your backend requires it.
    OTel::Metrics::sum("sensor.readings.total", 1.0,
                       /*isMonotonic=*/true, "DELTA", "1",
                       {{"sensor.id", "1"}});

    span.setAttribute("reading.celsius", (double)temperature);
    span.end();
  }

  // ── Health diagnostics (optional) ─────────────────────────────────────────
  uint32_t dropped = OTelSender::droppedCount();
  if (dropped > 0) {
    Serial.printf("[otel] warning: %u items dropped from send queue\n", dropped);
  }

  delay(SEND_INTERVAL_MS);
}

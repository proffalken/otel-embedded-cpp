#include <Arduino.h>

// ── Platform networking ───────────────────────────────────────────────────────
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
#elif defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_RP2040)
  #include <WiFi.h>
  #include <HTTPClient.h>
#else
  #error "Unsupported platform: must be ESP8266, ESP32, or RP2040"
#endif

#include <time.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <esp_system.h>
#endif

#include "OtelSender.h"
#include "OtelLogger.h"
#include "OtelTracer.h"
#include "OtelMetrics.h"

// ── Build-time defaults (override via -D flags in platformio.ini) ─────────────

#ifndef WIFI_SSID
#define WIFI_SSID "default"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "default"
#endif

// Collector endpoint — set OTEL_EXPORTER_OTLP_ENDPOINT (standard) or the
// legacy OTEL_COLLECTOR_BASE_URL.  See OtelSender.h for full priority chain.

#ifndef OTEL_SERVICE_NAME
#define OTEL_SERVICE_NAME "demo_service"
#endif

#ifndef OTEL_SERVICE_INSTANCE
#define OTEL_SERVICE_INSTANCE "demo_instance"
#endif

#ifndef OTEL_SERVICE_VERSION
#define OTEL_SERVICE_VERSION "v1.0.0"
#endif

#ifndef OTEL_SERVICE_NAMESPACE
#define OTEL_SERVICE_NAMESPACE "demo_namespace"
#endif

#ifndef OTEL_DEPLOY_ENV
#define OTEL_DEPLOY_ENV "dev"
#endif

// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t HEARTBEAT_INTERVAL = 5000;

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

  // Sync NTP so telemetry timestamps are meaningful
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP");
  while (time(nullptr) < 1609459200UL) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  // Seed PRNG for fresh trace IDs each boot
#if defined(ARDUINO_ARCH_ESP32)
  randomSeed(esp_random());
#else
  randomSeed(micros());
#endif

  // Initialise tracer and metrics (scopeName, scopeVersion)
  OTel::Tracer::begin("otel-embedded", OTEL_SERVICE_VERSION);
  OTel::Metrics::begin("otel-embedded", OTEL_SERVICE_VERSION);

  // Tag every metric datapoint with deployment metadata
  OTel::Metrics::setDefaultMetricLabel("deploy.environment", OTEL_DEPLOY_ENV);
  OTel::Metrics::setDefaultMetricLabel("service.namespace",  OTEL_SERVICE_NAMESPACE);

  // On RP2040, start the core-1 async send worker after Wi-Fi is ready
#if defined(ARDUINO_ARCH_RP2040)
  OTelSender::beginAsyncWorker();
#endif

  Serial.println("OTel ready");
}

void loop() {
  // Open a trace span
  auto span = OTel::Tracer::startSpan("heartbeat");

  // Log correlated to the active span
  OTel::Logger::logInfo("Heartbeat event");

  // Gauge: current value (no temporality)
  OTel::Metrics::gauge("heartbeat.uptime_seconds",
                       static_cast<double>(millis() / 1000), "s");

  span.end();

  delay(HEARTBEAT_INTERVAL);
}

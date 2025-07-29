#include <Arduino.h>
// ——————————————————————————————————————————————————————————
// Platform-specific networking includes
// ——————————————————————————————————————————————————————————
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
#elif defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_RP2040)
  #include <WiFi.h>
  #include <HTTPClient.h>
#else
  #error "Unsupported platform: must be ESP8266, ESP32 or RP2040"
#endif

#include "OtelDefaults.h"
#include "OtelSender.h"
#include "OtelLogger.h"
#include "OtelTracer.h"
#include "OtelMetrics.h"

// ————————————————————————————————————————————————
// Build‐time defaults (override with -D OTEL_* flags)
// ————————————————————————————————————————————————
#ifndef OTEL_WIFI_SSID
#define OTEL_WIFI_SSID     "YourDefaultSSID"
#endif

#ifndef OTEL_WIFI_PASS
#define OTEL_WIFI_PASS     "YourDefaultPassword"
#endif

#ifndef OTEL_COLLECTOR_HOST
#define OTEL_COLLECTOR_HOST "http://localhost:4318"
#endif

#ifndef OTEL_COLLECTOR_PORT
#define OTEL_COLLECTOR_PORT 4318
#endif

#ifndef OTEL_SERVICE_NAME
#define OTEL_SERVICE_NAME    "demo_service"
#endif

#ifndef OTEL_SERVICE_INSTANCE
#define OTEL_SERVICE_INSTANCE "instance-1"
#endif

#ifndef OTEL_SERVICE_VERSION
#define OTEL_SERVICE_VERSION "v1.0.0"
#endif

#ifndef OTEL_SERVICE_NAMESPACE
#define OTEL_SERVICE_NAMESPACE "demo_namespace"
#endif

#ifndef OTEL_DEPLOY_ENV
#define OTEL_DEPLOY_ENV      "dev"
#endif

// Delay between heartbeats (ms)
static constexpr uint32_t HEARTBEAT_INTERVAL = 5000;

void setup() {
  Serial.begin(115200);

  // Connect Wi-Fi using the macros you passed in build_flags
  WiFi.begin(OTEL_WIFI_SSID, OTEL_WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println("\nWiFi connected!");

  // Initialize the logger with service name, instance ID, and version
  OTel::Logger::begin(
    OTEL_SERVICE_NAME,
    OTEL_SERVICE_INSTANCE,
    OTEL_SERVICE_VERSION
  );
}

void loop() {
  // Start a new trace span called "heartbeat"
  auto span = OTel::Tracer::startSpan("heartbeat");

  // Emit a simple INFO log
  OTel::Logger::logInfo("Heartbeat event");

  // Record a gauge
  static OTel::OTelGauge heartbeatGauge("heartbeat.gauge", "1");
  heartbeatGauge.set(1.0f);

  // Increment a counter
  static OTel::OTelCounter heartbeatCounter("heartbeat.count", "1");
  heartbeatCounter.inc(1.0f);

  // Measure loop latency in a histogram
  static OTel::OTelHistogram loopLatencyHist("loop.latency", "ms");
  static unsigned long lastTime = micros();
  unsigned long now = micros();
  double latencyMs = (now - lastTime) / 1000.0;
  loopLatencyHist.record(latencyMs);
  lastTime = now;

  // End the span (sends the trace)
  span.end();

  delay(HEARTBEAT_INTERVAL);
}


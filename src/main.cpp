#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

// ---------------------------------------------------------
// Import Open Telemetry Libraries
// ---------------------------------------------------------
#include "OtelDefaults.h"
#include "OtelTracer.h"
#include "OtelLogger.h"
#include "OtelMetrics.h"
#include "OtelDebug.h"

static constexpr uint32_t HEARTBEAT_INTERVAL = 5000;

void setup() {
  Serial.begin(115200);

  // Seed PRNG (fresh trace IDs each boot)
  #if defined(ARDUINO_ARCH_ESP32)
    randomSeed(esp_random());
  #else
    randomSeed(micros());
  #endif

  // Connect to Wi‑Fi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }

  // Sync NTP
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 1609459200UL) { delay(500); }

  // Initialise Logger & Tracer
  
  // Set the defaults for the resources
  auto &res = OTel::defaultResource();
  res.set("service",        OTEL_SERVICE_NAME);
  res.set("service.name",        OTEL_SERVICE_NAME);
  res.set("service.namespace",   OTEL_SERVICE_NAMESPACE);
  res.set("service.instance.id", OTEL_SERVICE_INSTANCE);
  res.set("host.name", "my-embedded device");

  // Setup our tracing engine
  OTel::Tracer::begin("otel-embedded", "1.0.1");

  // Make sure that we start with empty trace and span ID's
  OTel::currentTraceContext().traceId = "";
  OTel::currentTraceContext().spanId  = "";

  // Setup the metrics engine
  OTel::Metrics::begin("otel-embedded", "1.0.1");
  OTel::Metrics::setDefaultMetricLabel("device.role", "test-device");
  OTel::Metrics::setDefaultMetricLabel("device.id", "device-chip-id-or-mac");
}

void loop() {
  // Heartbeat trace
  auto span = OTel::Tracer::startSpan("heartbeat");

  OTel::Logger::logInfo("Heartbeat event");
  OTel::Metrics::gauge(
          "heartbeat.gauge",                              // The name of the gauge
          1.0,                                            // The value 
          "1",                                            // The unit - "1" means no default unit
          { {"source", "main_loop"}, {"state", "alive"} } // A map of strings that will be converted to labels
          );
  span.end();

  delay(HEARTBEAT_INTERVAL);
}


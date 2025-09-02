#include <Arduino.h>

// ——————————————————————————————————————————————————————————
// Platform‐specific networking includes
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

// NTP / time
#include <time.h>

// For PRNG seeding
#if defined(ARDUINO_ARCH_ESP32)
  #include <esp_system.h>
#endif

// OTLP library
#include "OtelDebug.h"
#include "OtelDefaults.h"
#include "OtelSender.h"
#include "OtelLogger.h"
#include "OtelTracer.h"
#include "OtelMetrics.h"

// ————————————————————————————————————————————————
// Build‐time defaults (override with -D OTEL_* flags)
// ————————————————————————————————————————————————
#ifndef OTEL_WIFI_SSID
#define OTEL_WIFI_SSID     "default"
#endif

#ifndef OTEL_WIFI_PASS
#define OTEL_WIFI_PASS     "default"
#endif

#ifndef OTEL_COLLECTOR_HOST
#define OTEL_COLLECTOR_HOST "http://192.168.1.100:4318"
#endif

#ifndef OTEL_SERVICE_NAME
#define OTEL_SERVICE_NAME    "demo_service"
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
#define OTEL_DEPLOY_ENV      "dev"
#endif

// Delay between heartbeats (ms)
static constexpr uint32_t HEARTBEAT_INTERVAL = 5000;

void setup() {
  Serial.begin(115200);

  // ——————————
  // 0) Seed the PRNG for truly fresh IDs each boot
  // ——————————
#if defined(ARDUINO_ARCH_ESP32)
  randomSeed(esp_random());
#else
  randomSeed(micros());
#endif

  // 1) Connect to Wi-Fi
  Serial.printf("Connecting to %s …\n", OTEL_WIFI_SSID);
  WiFi.begin(OTEL_WIFI_SSID, OTEL_WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DBG_PRINT('.');
  }
  DBG_PRINTLN("\nWi-Fi connected!");

  // 2) Sync NTP (configTime works on Pico W, ESP32 & ESP8266 in Arduino land)
  //    We're polling until we get something > Jan 1 2020 (1609459200).
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  DBG_PRINT("Waiting NTP sync");
  while (time(nullptr) < 1609459200UL) {
    DBG_PRINT('.');
    delay(500);
  }
  DBG_PRINTLN();

  // 3) (Optional) print the calendar time
  {
    time_t now = time(nullptr);
    struct tm tm;
    gmtime_r(&now, &tm);
    Serial.printf("NTP time: %s", asctime(&tm));
  }

    // Set resource attributes once (service/host/instance/etc.)
  auto &res = OTel::defaultResource();
  res.set("service.name",        "guidance-sytem");
  res.set("service.instance.id", "818b08");
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP8266)
  res.set("host.name", WiFi.getHostname());
#else
  res.set("host.name", "rp2040");
#endif

  // Optional: default labels for all log lines from this process
  OTel::Logger::setDefaultLabel("role", "gps");
  OTel::Logger::setDefaultLabel("sensor_type", "neo-m6");

  OTel::Logger::logInfo("Logger initialised");
}

void loop() {
  // Print the calendar time
  {
    time_t now = time(nullptr);
    struct tm tm;
    gmtime_r(&now, &tm);
    Serial.printf("NTP time: %s", asctime(&tm));
  }

  // Start a new trace span called "heartbeat"
  auto span = OTel::Tracer::startSpan("heartbeat");

  // Emit a simple INFO log
  OTel::Logger::logInfo("Heartbeat event");

  // Record a gauge
  static OTel::OTelGauge heartbeatGauge("heartbeat.gauge", "1");
  heartbeatGauge.set(1.0f);

   // Log with labels that ADD and OVERWRITE defaults:
  // - adds event="startup"
  // - overwrites zone="ls" (was "chest")
  OTel::Logger::logInfo("Starting step",
    { {"event","startup"}, {"sensor_type","neo-m9"} });

  // End the trace span (this actually sends the trace)
  span.end();

  delay(HEARTBEAT_INTERVAL);
}


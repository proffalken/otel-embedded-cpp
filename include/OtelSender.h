#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>

// Optional compile-time on/off switch for all network sends.
// You can set -DOTEL_SEND_ENABLE=0 in platformio.ini for latency tests.
#ifndef OTEL_SEND_ENABLE
#define OTEL_SEND_ENABLE 1
#endif

#ifndef OTEL_WORKER_BURST
#define OTEL_WORKER_BURST 8
#endif

#ifndef OTEL_WORKER_SLEEP_MS
#define OTEL_WORKER_SLEEP_MS 0
#endif

#ifndef OTEL_QUEUE_CAPACITY
#define OTEL_QUEUE_CAPACITY 128
#endif
// Base URL of your OTLP/HTTP collector (no trailing slash), e.g. "http://192.168.8.50:4318"
// You can override this via build_flags: -DOTEL_COLLECTOR_BASE_URL="\"http://…:4318\""
#ifndef OTEL_COLLECTOR_BASE_URL
#define OTEL_COLLECTOR_BASE_URL "http://192.168.8.50:4318"
#endif

// Internal queue capacity for async sender on RP2040.
// Keep small to bound RAM; increase if you see drops.
#ifndef OTEL_QUEUE_CAPACITY
#define OTEL_QUEUE_CAPACITY 16
#endif

struct OTelQueuedItem {
  const char* path;   // "/v1/logs", "/v1/traces", "/v1/metrics"
  String payload;     // serialized JSON
};

class OTelSender {
public:
  // Main API: called by logger/tracer/metrics to send serialized JSON to OTLP/HTTP
  static void sendJson(const char* path, JsonDocument& doc);

  // Start the RP2040 core-1 worker (no-op on non-RP2040). Call once after Wi-Fi is ready.
  static void beginAsyncWorker();

  // Diagnostics (published via your health metrics if you like)
  static uint32_t droppedCount();   // number of items dropped due to full queue
  static bool     queueIsHealthy(); // worker started?

private:
  // ---------- SPSC ring buffer (core0 producer -> core1 consumer) ----------
  static constexpr size_t QCAP = OTEL_QUEUE_CAPACITY;
  static OTelQueuedItem q_[QCAP];
  static std::atomic<size_t> head_;   // producer writes
  static std::atomic<size_t> tail_;   // consumer writes
  static std::atomic<uint32_t> drops_;
  static std::atomic<bool>    worker_started_;

  static bool enqueue_(const char* path, String&& payload);
  static bool dequeue_(OTelQueuedItem& out);

  // ---------- Worker ----------
  static void pumpOnce_();   // send one item if present
  static void workerLoop_(); // runs on core 1 (RP2040)
  static void launchWorkerOnce_();

  // ---------- Utilities ----------
  static String fullUrl_(const char* path); // build collector URL + path

  // inside class OTelSender (near the bottom)
#ifdef ARDUINO_ARCH_RP2040
  friend void otel_worker_entry();
#endif
};


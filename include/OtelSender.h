#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>
#include <vector>
#include <cstdint>
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

// Base URL for all signals. Signal paths (/v1/traces, /v1/metrics, /v1/logs)
// are appended automatically, matching the OTLP exporter spec:
//   https://opentelemetry.io/docs/specs/otel/protocol/exporter/
//
// Standard name (preferred):
//   -DOTEL_EXPORTER_OTLP_ENDPOINT="\"http://192.168.8.50:4318\""
// Legacy name (still accepted, overridden by the standard name if both are set):
//   -DOTEL_COLLECTOR_BASE_URL="\"http://192.168.8.50:4318\""
#ifndef OTEL_EXPORTER_OTLP_ENDPOINT
#define OTEL_EXPORTER_OTLP_ENDPOINT ""
#endif
#ifndef OTEL_COLLECTOR_BASE_URL
#define OTEL_COLLECTOR_BASE_URL "http://192.168.8.50:4318"
#endif

// Per-signal endpoint overrides (full URL used as-is, no path appending).
// These override OTEL_EXPORTER_OTLP_ENDPOINT for their respective signal.
// Example (Datadog US1):
//   -DOTEL_EXPORTER_OTLP_LOGS_ENDPOINT="\"https://otlp.datadoghq.com/v1/logs\""
#ifndef OTEL_EXPORTER_OTLP_LOGS_ENDPOINT
#define OTEL_EXPORTER_OTLP_LOGS_ENDPOINT ""
#endif
#ifndef OTEL_EXPORTER_OTLP_TRACES_ENDPOINT
#define OTEL_EXPORTER_OTLP_TRACES_ENDPOINT ""
#endif
#ifndef OTEL_EXPORTER_OTLP_METRICS_ENDPOINT
#define OTEL_EXPORTER_OTLP_METRICS_ENDPOINT ""
#endif

// HTTP headers added to every outgoing OTLP request (all signals).
// Format: comma-separated "key=value" pairs.
// Example: -DOTEL_EXPORTER_OTLP_HEADERS="\"dd-api-key=abc123,dd-otlp-source=myapp\""
#ifndef OTEL_EXPORTER_OTLP_HEADERS
#define OTEL_EXPORTER_OTLP_HEADERS ""
#endif

// Per-signal header overrides, merged on top of OTEL_EXPORTER_OTLP_HEADERS.
// Same "key=value,…" format.
#ifndef OTEL_EXPORTER_OTLP_LOGS_HEADERS
#define OTEL_EXPORTER_OTLP_LOGS_HEADERS ""
#endif
#ifndef OTEL_EXPORTER_OTLP_TRACES_HEADERS
#define OTEL_EXPORTER_OTLP_TRACES_HEADERS ""
#endif
#ifndef OTEL_EXPORTER_OTLP_METRICS_HEADERS
#define OTEL_EXPORTER_OTLP_METRICS_HEADERS ""
#endif

// TLS: when the endpoint URL starts with "https://", a WiFiClientSecure is used.
// Set OTEL_TLS_INSECURE=0 and supply a CA cert via OTEL_TLS_CA_CERT to enable
// certificate validation. Defaults to skipping validation (common on embedded).
#ifndef OTEL_TLS_INSECURE
#define OTEL_TLS_INSECURE 1
#endif

// OTLP wire protocol.
// Matches the standard OTEL_EXPORTER_OTLP_PROTOCOL environment variable values.
// Use the named constants below rather than raw integers.
//
//   Default (JSON):
//     no flag needed — http/json is the default
//
//   Protobuf:
//     -DOTEL_EXPORTER_OTLP_PROTOCOL=OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF
//
// Protobuf requires the nanopb/Nanopb library in lib_deps.
#define OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_JSON      0
#define OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF  1

#ifndef OTEL_EXPORTER_OTLP_PROTOCOL
#define OTEL_EXPORTER_OTLP_PROTOCOL OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_JSON
#endif

// Stack buffer size for protobuf encoding (bytes). Increase if you have many
// attributes or long string values and see encoding failures.
#ifndef OTEL_PROTO_BUFFER_SIZE
#define OTEL_PROTO_BUFFER_SIZE 1024
#endif
#if OTEL_PROTO_BUFFER_SIZE <= 0
#error "OTEL_PROTO_BUFFER_SIZE must be > 0"
#endif

// Internal queue capacity for async sender on RP2040.
// Keep small to bound RAM; increase if you see drops.
#ifndef OTEL_QUEUE_CAPACITY
#define OTEL_QUEUE_CAPACITY 16
#endif
#if OTEL_QUEUE_CAPACITY < 2
#error "OTEL_QUEUE_CAPACITY must be >= 2 (one slot is unusable in an SPSC ring buffer)"
#endif

/**
 * Internal queue item for the RP2040 SPSC ring buffer (core-0 → core-1).
 * The payload is stored as a raw byte buffer so that protobuf bodies (which
 * may contain embedded null bytes) round-trip through the queue and the
 * binary HTTPClient::POST(uint8_t*, size_t) overload without truncation.
 */
struct OTelQueuedItem {
  const char* path;              // "/v1/logs", "/v1/traces", "/v1/metrics"
  const char* contentType;       // "application/json" or "application/x-protobuf"
  std::vector<uint8_t> payload;  // serialized body (JSON text or raw protobuf bytes)
};

/**
 * Low-level OTLP/HTTP sender used by Logger, Tracer, and Metrics.
 *
 * On RP2040 the actual HTTP calls run on core 1 via a SPSC ring buffer so
 * that loop() on core 0 is never blocked.  On ESP32/ESP8266 the send is
 * synchronous.  Call @c beginAsyncWorker() once after Wi-Fi comes up.
 */
class OTelSender {
public:
  /**
   * Serialise @p doc to JSON and send it to the collector at @p path.
   * @param path  OTLP signal path: "/v1/logs", "/v1/metrics", or "/v1/traces".
   * @param doc   ArduinoJson document representing the OTLP payload.
   */
  static void sendJson(const char* path, JsonDocument& doc);

  /**
   * Send a pre-encoded protobuf buffer to the collector at @p path.
   * Used by OtelProtoEncoder when @c OTEL_EXPORTER_OTLP_PROTOCOL is set to
   * @c OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF.
   * @param path  OTLP signal path.
   * @param buf   Pointer to the encoded protobuf bytes.
   * @param len   Length of the buffer in bytes.
   */
  static void sendProto(const char* path, const uint8_t* buf, size_t len);

  /**
   * Start the RP2040 core-1 worker thread.
   * No-op on ESP32/ESP8266.  Call once after Wi-Fi is ready.
   */
  static void beginAsyncWorker();

  /**
   * @return Number of payloads dropped because the internal queue was full.
   *         Monitor this with a gauge metric to detect back-pressure.
   */
  static uint32_t droppedCount();

  /**
   * @return True if the async worker has been started (RP2040) or always
   *         true on synchronous platforms.
   */
  static bool     queueIsHealthy();

  /**
   * Add a custom HTTP header that will be sent on every OTLP request for the
   * given signal path. Useful for backend-specific configuration when the
   * value is awkward to embed via -DOTEL_EXPORTER_OTLP_*_HEADERS build flag
   * (e.g. JSON values containing quotes/commas).
   *
   * Headers added at runtime are layered on top of any parsed from the build
   * flags. Call from setup() or after `Tracer::begin()` — must be called
   * before the first send. Once the first send happens the header lists are
   * frozen so the RP2040 worker on core 1 can read them without locking;
   * later addHeader() calls are silently dropped.
   *
   * @param path   OTLP signal path: "/v1/logs", "/v1/metrics", or "/v1/traces".
   * @param key    HTTP header name.
   * @param value  HTTP header value (sent verbatim — caller is responsible
   *               for any required encoding).
   *
   * Example (Datadog OTLP-to-tag promotion):
   *   OTelSender::addHeader("/v1/metrics", "dd-otel-metric-config",
   *                         "{\"resource_attributes_as_tags\":true}");
   */
  static void addHeader(const char* path, const String& key, const String& value);

private:
  // ---------- SPSC ring buffer (core0 producer -> core1 consumer) ----------
  static constexpr size_t QCAP = OTEL_QUEUE_CAPACITY;
  static OTelQueuedItem q_[QCAP];
  static std::atomic<size_t> head_;   // producer writes
  static std::atomic<size_t> tail_;   // consumer writes
  static std::atomic<uint32_t> drops_;
  static std::atomic<bool>    worker_started_;

  static bool enqueue_(const char* path, const char* contentType, std::vector<uint8_t>&& payload);
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


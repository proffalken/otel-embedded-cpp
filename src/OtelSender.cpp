#include "OtelSender.h"
#include <vector>
#include <utility>

// --- HTTP + WiFi includes (portable) ---
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#elif defined(ARDUINO_ARCH_RP2040)
#include <WiFi.h>       // Earle Philhower core
#include <HTTPClient.h> // Arduino HTTPClient
#include <WiFiClientSecure.h>
#else
#error "Unsupported platform: need WiFi + HTTPClient"
#endif

#ifdef ARDUINO_ARCH_RP2040
#include "pico/multicore.h"
#endif

// ===== statics =====
OTelQueuedItem OTelSender::q_[QCAP];
std::atomic<size_t> OTelSender::head_{0};
std::atomic<size_t> OTelSender::tail_{0};
std::atomic<uint32_t> OTelSender::drops_{0};
std::atomic<bool> OTelSender::worker_started_{false};

// ---------- Header parsing ----------

// Append "key=value,…" pairs from raw into out. Existing entries are preserved
// so callers can layer global headers then per-signal headers.
static void appendParsedHeaders_(const char *raw,
                                 std::vector<std::pair<String, String>> &out)
{
  if (!raw || !*raw)
    return;
  String s(raw);
  int start = 0;
  while (start <= (int)s.length())
  {
    int comma = s.indexOf(',', start);
    if (comma < 0)
      comma = (int)s.length();
    String token = s.substring(start, comma);
    token.trim();
    int eq = token.indexOf('=');
    if (eq > 0)
    {
      String k = token.substring(0, eq);
      String v = token.substring(eq + 1);
      k.trim();
      v.trim();
      if (k.length())
        out.push_back({k, v});
    }
    start = comma + 1;
  }
}

// Per-signal header lists. Initialized lazily from build flags on first
// access, and extended at runtime via OTelSender::addHeader().
struct SignalHeaders {
  std::vector<std::pair<String, String>> log, trace, metric;
};

// Once a send happens, headers are read by the RP2040 worker on core 1.
// We freeze the SignalHeaders vectors at that point so further addHeader()
// calls from core 0 cannot reallocate them mid-iteration (UB).
static std::atomic<bool> headers_frozen_{false};

static SignalHeaders &mutableHeaders_()
{
  static SignalHeaders s;
  static bool initialized = false;
  if (!initialized)
  {
    initialized = true;
    // Global headers first, then per-signal overrides from build flags.
    appendParsedHeaders_(OTEL_EXPORTER_OTLP_HEADERS, s.log);
    appendParsedHeaders_(OTEL_EXPORTER_OTLP_HEADERS, s.trace);
    appendParsedHeaders_(OTEL_EXPORTER_OTLP_HEADERS, s.metric);
    appendParsedHeaders_(OTEL_EXPORTER_OTLP_LOGS_HEADERS, s.log);
    appendParsedHeaders_(OTEL_EXPORTER_OTLP_TRACES_HEADERS, s.trace);
    appendParsedHeaders_(OTEL_EXPORTER_OTLP_METRICS_HEADERS, s.metric);
  }
  return s;
}

// Touch the headers on the current core to force lazy init, then freeze.
// After this, the RP2040 worker on core 1 only ever reads the vectors so
// no synchronisation is required for the read path in doPost_().
static void freezeHeaders_()
{
  if (!headers_frozen_.load(std::memory_order_acquire))
  {
    (void)mutableHeaders_();
    headers_frozen_.store(true, std::memory_order_release);
  }
}

// Returns the merged header list for the given OTLP path.
static const std::vector<std::pair<String, String>> &headersForPath_(const char *path)
{
  auto &s = mutableHeaders_();
  if (path && strcmp(path, "/v1/logs") == 0)
    return s.log;
  if (path && strcmp(path, "/v1/traces") == 0)
    return s.trace;
  return s.metric;
}

// Public API: add a header at runtime to a specific OTLP signal path.
// Must be called before the first send; once headers are frozen the call
// is rejected to avoid racing with the RP2040 worker reading the vectors.
void OTelSender::addHeader(const char *path, const String &key, const String &value)
{
  if (headers_frozen_.load(std::memory_order_acquire)) return;
  auto &s = mutableHeaders_();
  if (path && strcmp(path, "/v1/logs") == 0)         s.log.push_back({key, value});
  else if (path && strcmp(path, "/v1/traces") == 0)  s.trace.push_back({key, value});
  else if (path && strcmp(path, "/v1/metrics") == 0) s.metric.push_back({key, value});
}

// ---------- URL resolution ----------

// Returns the full URL for the given OTLP signal path.
//
// Priority (highest to lowest):
//   1. Per-signal endpoint (OTEL_EXPORTER_OTLP_{LOGS,TRACES,METRICS}_ENDPOINT)
//      → used as-is, no path appended (spec requirement)
//   2. Standard base URL (OTEL_EXPORTER_OTLP_ENDPOINT)
//      → signal path appended automatically
//   3. Legacy base URL (OTEL_COLLECTOR_BASE_URL)
//      → signal path appended automatically
String OTelSender::fullUrl_(const char *path)
{
  // 1. Per-signal overrides (used verbatim per spec)
  if (path)
  {
    if (strcmp(path, "/v1/logs") == 0 && strlen(OTEL_EXPORTER_OTLP_LOGS_ENDPOINT) > 0)
      return String(OTEL_EXPORTER_OTLP_LOGS_ENDPOINT);
    if (strcmp(path, "/v1/traces") == 0 && strlen(OTEL_EXPORTER_OTLP_TRACES_ENDPOINT) > 0)
      return String(OTEL_EXPORTER_OTLP_TRACES_ENDPOINT);
    if (strcmp(path, "/v1/metrics") == 0 && strlen(OTEL_EXPORTER_OTLP_METRICS_ENDPOINT) > 0)
      return String(OTEL_EXPORTER_OTLP_METRICS_ENDPOINT);
  }

  // 2/3. Base URL with signal path appended
  String base = strlen(OTEL_EXPORTER_OTLP_ENDPOINT) > 0
                    ? String(OTEL_EXPORTER_OTLP_ENDPOINT)
                    : String(OTEL_COLLECTOR_BASE_URL);
  if (base.endsWith("/"))
    base.remove(base.length() - 1);
  if (path && *path == '/')
    return base + String(path);
  return base + "/" + String(path ? path : "");
}

// ---------- HTTP/HTTPS POST ----------

// Persistent transport. Allocating a fresh WiFiClientSecure + HTTPClient on
// every send caused two problems on memory-tight ESP32 silicon:
//   1. ~30 KB of mbedtls TLS context is allocated per call, and the
//      BIGNUM/ECP/MD modules need contiguous heap blocks for handshake
//      working memory. Under heap pressure (BT + WiFi + tasks resident),
//      these allocations fail with err=-19840 (ECP) or err=-20864 (MD)
//      and every send silently drops.
//   2. Even when handshake succeeds, doing a full TLS negotiation per
//      send blocks the worker for ~500 ms per item and pegs the CPU on
//      mbedtls EC math.
//
// We keep one WiFiClientSecure + HTTPClient at file scope, pay the
// handshake cost once on the first send, and reuse the socket via
// HTTPClient::setReuse(true) for every subsequent send. The OTLP host
// rarely changes (signals all live under the same OTEL_EXPORTER_OTLP_-
// ENDPOINT base), so the connection survives across metric/log/trace
// boundaries.
static WiFiClientSecure tlsClient_;
static HTTPClient       httpClient_;
static String           tlsClientHost_;
static bool             httpInit_ = false;

static void ensureHttpInit_()
{
  if (httpInit_) return;
#if OTEL_TLS_INSECURE
  tlsClient_.setInsecure();
#elif defined(OTEL_TLS_CA_CERT)
  tlsClient_.setCACert(OTEL_TLS_CA_CERT);
#else
#error "OTEL_TLS_INSECURE=0 requires -DOTEL_TLS_CA_CERT=\"...PEM...\" to be defined."
#endif
  // setHandshakeTimeout() is only available on ESP32 (NetworkClientSecure).
  // It is absent from both the ESP8266 BearSSL and the Earle Philhower RP2040
  // WiFiClientSecure implementations. The socket-level setTimeout() below
  // provides a connection timeout on all three platforms.
#if defined(ESP32)
  tlsClient_.setHandshakeTimeout(20);
#endif
  tlsClient_.setTimeout(15);
  httpClient_.setReuse(true);
  httpClient_.setTimeout(15000);
  httpInit_ = true;
}

// Executes a single POST. Handles plain HTTP and HTTPS transparently based on
// the URL scheme. Custom headers are applied after Content-Type.
static void doPost_(const String &url, const std::vector<uint8_t> &payload,
                    const char *path, const char *contentType)
{
  const auto &hdrs = headersForPath_(path);

  auto fire = [&](bool ok)
  {
    if (!ok)
      return;
    httpClient_.addHeader("Content-Type", contentType);
    for (const auto &h : hdrs)
      httpClient_.addHeader(h.first, h.second);
    (void)httpClient_.POST(const_cast<uint8_t *>(payload.data()), payload.size());
    httpClient_.end();
  };

  if (url.startsWith("https://"))
  {
    ensureHttpInit_();
    fire(httpClient_.begin(tlsClient_, url));
  }
  else
  {
    // Plain HTTP path keeps the per-call allocation pattern — exporters
    // rarely sit behind plain HTTP in production, and we don't want the
    // persistent secure client to leak its mbedtls state into a different
    // unrelated http session.
    HTTPClient http;
#if defined(ESP8266)
    WiFiClient wc;
    fire(http.begin(wc, url));
#else
    fire(http.begin(url));
#endif
  }
}

// ---------- Queue (SPSC) ----------

bool OTelSender::enqueue_(const char *path, const char *contentType, std::vector<uint8_t> &&payload)
{
  size_t h = head_.load(std::memory_order_relaxed);
  size_t t = tail_.load(std::memory_order_acquire);
  size_t next = (h + 1) % QCAP;

  if (next == t)
  {
    // Full: drop oldest (advance tail)
    size_t new_t = (t + 1) % QCAP;
    tail_.store(new_t, std::memory_order_release);
    drops_.fetch_add(1, std::memory_order_relaxed);
  }

  q_[h].path = path;
  q_[h].contentType = contentType;
  q_[h].payload = std::move(payload);
  head_.store(next, std::memory_order_release);
  return true;
}

bool OTelSender::dequeue_(OTelQueuedItem &out)
{
  size_t t = tail_.load(std::memory_order_relaxed);
  size_t h = head_.load(std::memory_order_acquire);
  if (t == h)
    return false;

  out = std::move(q_[t]);
  // Free the slot's allocation now rather than waiting for the next
  // overwrite, so a long idle period doesn't pin RAM proportional to
  // the largest payload ever sent.
  std::vector<uint8_t>().swap(q_[t].payload);
  size_t next = (t + 1) % QCAP;
  tail_.store(next, std::memory_order_release);
  return true;
}

// ---------- Worker ----------

void OTelSender::pumpOnce_()
{
  OTelQueuedItem it;
  if (!dequeue_(it))
    return;
#if OTEL_SEND_ENABLE
  doPost_(fullUrl_(it.path), it.payload, it.path, it.contentType);
#endif
}

void OTelSender::workerLoop_()
{
  for (;;)
  {
    for (int i = 0; i < OTEL_WORKER_BURST; ++i)
    {
      OTelQueuedItem it;
      if (!dequeue_(it))
        break;
#if OTEL_SEND_ENABLE
      doPost_(fullUrl_(it.path), it.payload, it.path, it.contentType);
#endif
    }
    delay(OTEL_WORKER_SLEEP_MS);
  }
}

#ifdef ARDUINO_ARCH_RP2040
void otel_worker_entry() { OTelSender::workerLoop_(); }
#endif

void OTelSender::launchWorkerOnce_()
{
#ifdef ARDUINO_ARCH_RP2040
  bool expected = false;
  if (worker_started_.compare_exchange_strong(expected, true))
  {
    multicore_launch_core1(otel_worker_entry);
  }
#endif
}

void OTelSender::beginAsyncWorker()
{
  launchWorkerOnce_();
}

uint32_t OTelSender::droppedCount()
{
  return drops_.load(std::memory_order_relaxed);
}

bool OTelSender::queueIsHealthy()
{
#ifdef ARDUINO_ARCH_RP2040
  return worker_started_.load(std::memory_order_relaxed);
#else
  // No async worker on synchronous platforms — sends happen inline, so
  // there is no queue health to report. Always healthy by definition.
  return true;
#endif
}

// ---------- Public send API ----------

static constexpr const char *kContentTypeJson = "application/json";
static constexpr const char *kContentTypeProto = "application/x-protobuf";

void OTelSender::sendJson(const char *path, JsonDocument &doc)
{
#if !OTEL_SEND_ENABLE
  (void)path;
  (void)doc;
  return;
#else
  freezeHeaders_();
  // Serialise JSON to a temporary String, then copy bytes into a binary
  // buffer for the queue/send path. JSON is UTF-8 text without embedded
  // null bytes, so the copy is safe; we use a vector here for symmetry
  // with the protobuf path and to share the binary-safe POST overload.
  String text;
  serializeJson(doc, text);
  const uint8_t *begin = reinterpret_cast<const uint8_t *>(text.c_str());
  std::vector<uint8_t> payload(begin, begin + text.length());

#ifdef ARDUINO_ARCH_RP2040
  launchWorkerOnce_();
  enqueue_(path, kContentTypeJson, std::move(payload));
#else
  doPost_(fullUrl_(path), payload, path, kContentTypeJson);
#endif
#endif
}

void OTelSender::sendProto(const char *path, const uint8_t *buf, size_t len)
{
#if !OTEL_SEND_ENABLE
  (void)path;
  (void)buf;
  (void)len;
  return;
#else
  freezeHeaders_();
  // Binary buffer round-trips embedded null bytes in protobuf payloads
  // through the queue and the HTTPClient::POST(uint8_t*, size_t) overload
  // without truncation.
  std::vector<uint8_t> payload(buf, buf + len);

#ifdef ARDUINO_ARCH_RP2040
  launchWorkerOnce_();
  enqueue_(path, kContentTypeProto, std::move(payload));
#else
  doPost_(fullUrl_(path), payload, path, kContentTypeProto);
#endif
#endif
}

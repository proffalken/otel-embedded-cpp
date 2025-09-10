#include "OtelSender.h"

// --- HTTP + WiFi includes (portable) ---
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
#elif defined(ARDUINO_ARCH_RP2040)
  #include <WiFi.h>        // Earle Philhower core
  #include <HTTPClient.h>  // Arduino HTTPClient
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
std::atomic<bool>    OTelSender::worker_started_{false};
// Begin HTTP on all platforms (ESP8266 requires WiFiClient)
static bool httpBeginCompat(HTTPClient& http, const String& url) {
#if defined(ESP8266)
  WiFiClient client;                // or WiFiClientSecure if you later do HTTPS
  return http.begin(client, url);   // new API on ESP8266
#else
  return http.begin(url);           // ESP32 / RP2040
#endif
}


// Build "http://host:4318" + "/v1/…"
String OTelSender::fullUrl_(const char* path) {
  // Avoid double slashes if a user accidentally sets a trailing slash
  String base = String(OTEL_COLLECTOR_BASE_URL);
  if (base.endsWith("/")) base.remove(base.length() - 1);
  if (path && *path == '/') return base + String(path);
  return base + "/" + String(path ? path : "");
}

// ---------- Queue (SPSC) ----------
// Single-producer (core0) enqueue; drop oldest on overflow
bool OTelSender::enqueue_(const char* path, String&& payload) {
  size_t h = head_.load(std::memory_order_relaxed);
  size_t t = tail_.load(std::memory_order_acquire);
  size_t next = (h + 1) % QCAP;

  if (next == t) {
    // Full: drop oldest (advance tail)
    size_t new_t = (t + 1) % QCAP;
    tail_.store(new_t, std::memory_order_release);
    drops_.fetch_add(1, std::memory_order_relaxed);
  }

  q_[h].path = path;
  q_[h].payload = std::move(payload);
  head_.store(next, std::memory_order_release);
  return true;
}

bool OTelSender::dequeue_(OTelQueuedItem& out) {
  size_t t = tail_.load(std::memory_order_relaxed);
  size_t h = head_.load(std::memory_order_acquire);
  if (t == h) return false; // empty

  out = std::move(q_[t]);
  q_[t].payload = String();  // release memory
  size_t next = (t + 1) % QCAP;
  tail_.store(next, std::memory_order_release);
  return true;
}

// ---------- Worker ----------
void OTelSender::pumpOnce_() {
#if OTEL_SEND_ENABLE
  OTelQueuedItem it;
  if (!dequeue_(it)) return;

  HTTPClient http;
  if (httpBeginCompat(http, fullUrl_(it.path))) {
    http.addHeader("Content-Type", "application/json");
    // Fire the POST; the blocking happens on core 1, not in the control path.
    (void)http.POST(it.payload);
    http.end();
  }
#else
  // If globally disabled, just drain the queue without sending.
  OTelQueuedItem sink;
  (void)dequeue_(sink);
#endif
}

void OTelSender::workerLoop_() {
  for (;;) {
    for (int i = 0; i < OTEL_WORKER_BURST; ++i) {
      OTelQueuedItem it;
      if (!dequeue_(it)) break;

      HTTPClient http;
      // Keep-alive where supported; harmless otherwise
      #if defined(HTTPCLIENT_1_2_COMPATIBLE) || defined(ESP8266) || defined(ESP32)
      http.setReuse(true);
      #endif
      if (httpBeginCompat(http,fullUrl_(it.path))) {
        http.addHeader("Content-Type", "application/json");
        (void)http.POST(it.payload);
        http.end();
      }
    }
    delay(OTEL_WORKER_SLEEP_MS);
  }
}


#ifdef ARDUINO_ARCH_RP2040
void otel_worker_entry() { OTelSender::workerLoop_(); }
#endif


void OTelSender::launchWorkerOnce_() {
#ifdef ARDUINO_ARCH_RP2040
  bool expected = false;
  if (worker_started_.compare_exchange_strong(expected, true)) {
    multicore_launch_core1(otel_worker_entry);
  }
#endif
}

void OTelSender::beginAsyncWorker() {
  launchWorkerOnce_();
}

uint32_t OTelSender::droppedCount() {
  return drops_.load(std::memory_order_relaxed);
}

bool OTelSender::queueIsHealthy() {
  return worker_started_.load(std::memory_order_relaxed);
}

// ---------- Public send API ----------
void OTelSender::sendJson(const char* path, JsonDocument& doc) {
#if !OTEL_SEND_ENABLE
  // Compile-time: completely disable sends (useful for latency tests)
  (void)path; (void)doc;
  return;
#else
  // Serialize on the caller's core (cheap), then:
  //  - RP2040: enqueue for core-1 worker to POST (non-blocking for control path)
  //  - others: POST synchronously (unchanged behaviour)
  String payload;
  serializeJson(doc, payload);

  #ifdef ARDUINO_ARCH_RP2040
    // Ensure worker is launched (safe to call repeatedly)
    launchWorkerOnce_();
    enqueue_(path, std::move(payload));
  #else
    HTTPClient http;
    if (httpBeginCompat(http, fullUrl_(path))) {
      http.addHeader("Content-Type", "application/json");
      (void)http.POST(payload);
      http.end();
    }
  #endif
#endif
}


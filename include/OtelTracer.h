// OtelTracer.h
#ifndef OTEL_TRACER_H
#define OTEL_TRACER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include "OtelDefaults.h"   // expects: nowUnixNano()
#include "OtelSender.h"     // expects: OTelSender::sendJson(const char* path, const JsonDocument&)

namespace OTel {
    
// ---- Active Trace Context ---------------------------------------------------
struct TraceContext {
  String traceId;  // 32 hex chars
  String spanId;   // 16 hex chars
  bool valid() const { return traceId.length() == 32 && spanId.length() == 16; }
};

static inline TraceContext& currentTraceContext() {
  static TraceContext ctx;
  return ctx;
}

// --- New: Context Propagation (extract + scope) ------------------------------
struct ExtractedContext {
  TraceContext ctx;
  String tracestate;   // optional; unused for now but kept for future injection
  bool sampled = true; // from flags; default true if unknown
  bool valid() const { return ctx.valid(); }
};

// Simple key/value view for header-like maps (HTTP headers, MQTT user props)
struct KeyValuePairs {
  // Provide a lambda to look up case-insensitive keys. Returns empty String if missing.
  std::function<String(const String&)> get;
};

// W3C "traceparent": 00-<32 hex traceId>-<16 hex parentId>-<2 hex flags>
static inline bool parseTraceparent(const String& tp, ExtractedContext& out) {
  // Minimal, allocation-light parser
  // Expect 55 chars with version "00" or at least the 4 parts separated by '-'
  int p1 = tp.indexOf('-'); if (p1 < 0) return false;
  int p2 = tp.indexOf('-', p1+1); if (p2 < 0) return false;
  int p3 = tp.indexOf('-', p2+1); if (p3 < 0) return false;

  String ver  = tp.substring(0, p1);
  String tid  = tp.substring(p1+1, p2);
  String psid = tp.substring(p2+1, p3);
  String flg  = tp.substring(p3+1);

  // Validate lengths per spec
  if (tid.length() != 32 || psid.length() != 16 || flg.length() != 2) return false;

  out.ctx.traceId = tid;
  out.ctx.spanId  = psid;
  // Flags: bit 0 = sampled
  out.sampled = (strtoul(flg.c_str(), nullptr, 16) & 0x01) == 0x01;
  return out.valid();
}

// B3 single header: b3 = traceId-spanId-sampled
static inline bool parseB3Single(const String& b3, ExtractedContext& out) {
  // Minimal split (traceId-spanId-[sampling?])
  int p1 = b3.indexOf('-'); if (p1 < 0) return false;
  int p2 = b3.indexOf('-', p1+1);

  String tid = (p1 > 0) ? b3.substring(0, p1) : "";
  String sid = (p2 > 0) ? b3.substring(p1+1, p2) : b3.substring(p1+1);
  String smp = (p2 > 0) ? b3.substring(p2+1) : "";

  if (tid.length() != 32 || sid.length() != 16) return false;
  out.ctx.traceId = tid;
  out.ctx.spanId  = sid;
  out.sampled = (smp == "1" || smp == "d");
  return out.valid();
}

struct Propagators {
  // 1) Extract from header-like key/values (HTTP headers, MQTT v5 user props)
  static ExtractedContext extract(const KeyValuePairs& kv) {
    ExtractedContext out;

    // Prefer W3C traceparent
    if (kv.get) {
      String tp = kv.get("traceparent");
      if (tp.length() == 0) tp = kv.get("Traceparent"); // some stacks capitalise
      if (tp.length() && parseTraceparent(tp, out)) {
        String ts = kv.get("tracestate"); if (ts.length() == 0) ts = kv.get("Tracestate");
        out.tracestate = ts;
        return out;
      }

      // Fallback: B3 single
      String b3 = kv.get("b3"); if (b3.length() == 0) b3 = kv.get("B3");
      if (b3.length() && parseB3Single(b3, out)) return out;
    }
    return out; // invalid
  }

  // 2) Extract directly from JSON payload
  static ExtractedContext extractFromJson(const String& json) {
    ExtractedContext out;
    if (json.length() == 0) return out;

    // Use a small document to avoid heap bloat; adjust if payloads are larger
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return out;

    if (doc["traceparent"].is<const char*>()) {
  out = ExtractedContext{};
  String tp = doc["traceparent"].as<String>();
  parseTraceparent(tp, out);
  if (doc["tracestate"].is<const char*>()) {
    out.tracestate = doc["tracestate"].as<String>();
  }
  return out;
}

if (doc["trace_id"].is<const char*>() && doc["span_id"].is<const char*>()) {
  out = ExtractedContext{};
  String tid = doc["trace_id"].as<String>();
  String sid = doc["span_id"].as<String>();
  // (retain whatever validation / assignment you had here)
  // Example:
  out.ctx.traceId = tid;
  out.ctx.spanId  = sid;
  if (doc["trace_flags"].is<const char*>()) {
    out.sampled = (String(doc["trace_flags"].as<const char*>()).indexOf("01") >= 0);
  } else if (doc["trace_flags"].is<uint8_t>()) {
    out.sampled = (doc["trace_flags"].as<uint8_t>() & 0x01) != 0;
  }
  return out;
}

if (doc["b3"].is<const char*>()) {
  out = ExtractedContext{};
  String b3 = doc["b3"].as<String>();
  parseB3Single(b3, out);
  return out;
}

    return out; // invalid if none matched
  }
};

// RAII helper: temporarily install a remote parent context as the active one
class RemoteParentScope {
public:
  RemoteParentScope(const TraceContext& incoming) {
    // Save current
    prev_ = currentTraceContext();
    // Install incoming (only if valid; otherwise leave as-is)
    if (incoming.valid()) {
      currentTraceContext().traceId = incoming.traceId;
      currentTraceContext().spanId  = incoming.spanId;
      installed_ = true;
    }
  }
  ~RemoteParentScope() {
    if (installed_) {
      currentTraceContext() = prev_;
    }
  }
private:
  TraceContext prev_;
  bool installed_ = false;
};



// ---- Utilities --------------------------------------------------------------
static inline String u64ToStr(uint64_t v) {
  // Avoid ambiguous String(uint64_t) on some cores
  char buf[32];
  char *p = buf + sizeof(buf);
  *--p = '\0';
  if (v == 0) { *--p = '0'; }
  while (v > 0) {
    *--p = char('0' + (v % 10));
    v /= 10;
  }
  return String(p);
}

static inline String randomHex(size_t len) {
  static const char* hexDigits = "0123456789abcdef";
  String out;
  out.reserve(len);
  for (size_t i = 0; i < len; ++i) out += hexDigits[random(0, 16)];
  return out;
}

// Best-effort chip id (used for defaults)
static inline String chipIdHex() {
#if defined(ESP8266)
  uint32_t id = ESP.getChipId();
  char b[9]; snprintf(b, sizeof(b), "%06x", id);
  return String(b);
#elif defined(ESP32)
  uint64_t id = ESP.getEfuseMac();
  char b[17]; snprintf(b, sizeof(b), "%012llx", static_cast<unsigned long long>(id));
  return String(b);
#else
  return String("000000");
#endif
}

// Defaults for resource fields (compile-time overrides win)
static inline String defaultServiceName() {
#ifdef OTEL_SERVICE_NAME
  return String(OTEL_SERVICE_NAME);
#else
  return String("embedded-service");
#endif
}
static inline String defaultServiceInstanceId() {
#ifdef OTEL_SERVICE_INSTANCE_ID
  return String(OTEL_SERVICE_INSTANCE_ID);
#else
  return chipIdHex();
#endif
}
static inline String defaultHostName() {
#ifdef OTEL_HOST_NAME
  return String(OTEL_HOST_NAME);
#else
  return String("ESP-") + chipIdHex();
#endif
}

// Add one string attribute to a resource attributes array
static inline void addResAttr(JsonArray& arr, const char* key, const String& value) {
  JsonObject a = arr.add<JsonObject>();
  a["key"] = key;
  a["value"].to<JsonObject>()["stringValue"] = value;
}

// ---- Tracer configuration ---------------------------------------------------
struct TracerConfig {
  String scopeName{"otel-embedded"};
  String scopeVersion{"0.1.0"};
};

static inline TracerConfig& tracerConfig() {
  static TracerConfig cfg;
  return cfg;
}

// ---- Span -------------------------------------------------------------------
class Span {
public:
  explicit Span(const String& name)
  : name_(name),
    traceId_(currentTraceContext().valid() ? currentTraceContext().traceId : randomHex(32)),
    spanId_(randomHex(16)),
    startNs_(nowUnixNano())
  {
    // Save previous context and install this span's ids
    prevTraceId_ = currentTraceContext().traceId;
    prevSpanId_  = currentTraceContext().spanId;
    currentTraceContext().traceId = traceId_;
    currentTraceContext().spanId  = spanId_;
  }

  // RAII: if user forgets to call end(), do it at scope exit.
  ~Span() {
    if (!ended_) {
      end();
    }
  }

  // You can still call this manually; it's safe to call more than once.
  void end() {
    if (ended_) return;               // idempotent guard
    ended_ = true;

    const uint64_t endNs = nowUnixNano();

    // Build minimal OTLP/HTTP JSON payload for a single span
    JsonDocument doc;

    // resourceSpans[0].resource.attributes[...]
    JsonArray rattrs = doc["resourceSpans"][0]["resource"]["attributes"].to<JsonArray>();
    addResAttr(rattrs, "service.name",        defaultServiceName());
    addResAttr(rattrs, "service.instance.id", defaultServiceInstanceId());
    addResAttr(rattrs, "host.name",           defaultHostName());

    // instrumentation scope
    JsonObject scope = doc["resourceSpans"][0]["scopeSpans"][0]["scope"].to<JsonObject>();
    scope["name"]    = tracerConfig().scopeName;
    scope["version"] = tracerConfig().scopeVersion;

    // span body
    JsonObject s = doc["resourceSpans"][0]["scopeSpans"][0]["spans"][0].to<JsonObject>();
    s["traceId"]           = traceId_;
    s["spanId"]            = spanId_;
    s["name"]              = name_;
    s["kind"]              = 2; // SERVER by default; adjust if you have a setter
    s["startTimeUnixNano"] = u64ToStr(startNs_);
    s["endTimeUnixNano"]   = u64ToStr(endNs);

    // If we have a parent, set it correctly
    if (prevSpanId_.length() == 16) {
      s["parentSpanId"] = prevSpanId_;
    }

    // Send
    OTelSender::sendJson("/v1/traces", doc);

    // Restore previous active context
    currentTraceContext().traceId = prevTraceId_;
    currentTraceContext().spanId  = prevSpanId_;
  }

  // Optional helpers (if you have them already, keep yours)
  const String& traceId() const { return traceId_; }
  const String& spanId()  const { return spanId_;  }

private:
  // Utility to add a resource attribute
  static inline void addResAttr(JsonArray& arr, const char* key, const String& val) {
    JsonObject a = arr.add<JsonObject>();
    a["key"] = key;
    a["value"]["stringValue"] = val;
  }

  static inline String u64ToStr(uint64_t v) {
    // Avoid ambiguous Arduino String(uint64_t) by formatting manually
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v));
    return String(buf);
  }

private:
  String name_;
  String traceId_;
  String spanId_;
  uint64_t startNs_;

  // Previous active context (for parent linkage and restoration)
  String prevTraceId_;
  String prevSpanId_;

  // RAII guard
  bool ended_ = false;
};

// ---- Tracer facade ----------------------------------------------------------
class Tracer {
public:
  static void begin(const String& scopeName, const String& scopeVersion) {
    tracerConfig().scopeName    = scopeName;
    tracerConfig().scopeVersion = scopeVersion;
  }

  static Span startSpan(const String& name) {
    return Span(name);
  }
};

} // namespace OTel

#endif // OTEL_TRACER_H


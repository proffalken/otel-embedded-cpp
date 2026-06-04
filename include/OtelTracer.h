// OtelTracer.h
#ifndef OTEL_TRACER_H
#define OTEL_TRACER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <utility>
#include <functional>
#include <vector>              // NEW: needed for attributes/events buffers
#include "OtelDebug.h"
#include "OtelDefaults.h"   // expects: nowUnixNano()
#include "OtelSender.h"     // expects: OTelSender::sendJson(const char* path, const JsonDocument&)
#include "OtelProtoEncoder.h"

#if defined(ESP32)
  #include <esp_system.h>   // esp_random, esp_fill_random
#elif defined(ESP8266)
  extern "C" {
    #include <user_interface.h>  // os_random(), system_get_rtc_time()
  }
#elif defined(ARDUINO_ARCH_RP2040)
  #include <pico/rand.h>    // get_rand_32()
#endif

namespace OTel {
    
/**
 * Active W3C trace context (traceId + spanId) for the current execution scope.
 * Updated automatically when a @c Span is created or destroyed.
 */
struct TraceContext {
  String traceId;  // 32 hex chars
  String spanId;   // 16 hex chars
  /** @return True when both IDs have the correct lengths per the W3C spec. */
  bool valid() const { return traceId.length() == 32 && spanId.length() == 16; }
};

/** Returns the process-wide active TraceContext singleton. */
inline TraceContext& currentTraceContext() {
  static TraceContext ctx;
  return ctx;
}

/**
 * Result of extracting a remote trace context from inbound headers or a payload.
 * Pass to @c RemoteParentScope to make it the active context for child spans.
 */
struct ExtractedContext {
  TraceContext ctx;
  String tracestate;   // optional; unused for now but kept for future injection
  bool sampled = true; // from flags; default true if unknown
  /** @return True when the embedded TraceContext is valid. */
  bool valid() const { return ctx.valid(); }
};

/**
 * Thin abstraction over a key/value lookup used by context propagators.
 * Wrap HTTP headers, MQTT user properties, or any string-keyed map.
 */
struct KeyValuePairs {
  /** Callable that returns the value for a key, or an empty String if absent. */
  std::function<String(const String&)> get;
};

/**
 * Parse a W3C @c traceparent header value into @p out.
 * Format: "00-<32 hex traceId>-<16 hex parentId>-<2 hex flags>".
 * @return True on success, false if the string is malformed.
 */
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

/**
 * Parse a B3 single-header value into @p out.
 * Format: "<32 hex traceId>-<16 hex spanId>[-<sampling flag>]".
 * @return True on success, false if the string is malformed.
 */
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

/**
 * W3C TraceContext and B3 context propagation helpers.
 * Use @c extract() / @c extractFromJson() to read a remote parent context
 * and @c inject() / @c injectToJson() / @c injectToHeaders() to forward it.
 */
struct Propagators {
  /**
   * Extract a remote trace context from header-like key/value pairs.
   * Tries W3C @c traceparent first, then B3 single-header as a fallback.
   * @param kv  Wrapper providing a case-insensitive key lookup.
   * @return    Extracted context; check @c valid() before use.
   */
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

  /**
   * Extract a remote trace context from a JSON payload string.
   * Recognises @c traceparent, @c trace_id/@c span_id, and @c b3 fields.
   * @param json  Serialised JSON string.
   * @return      Extracted context; check @c valid() before use.
   */
  static ExtractedContext extractFromJson(const String& json) {
    ExtractedContext out;
    if (json.length() == 0) return out;

    // Use a small document to avoid heap bloat; adjust if payloads are larger
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json.c_str());
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

  /**
   * Inject the active trace context into any key/value store via a setter callable.
   * Writes a W3C @c traceparent value.  No-op if no active span is present.
   * @param set    Callable accepting @c (const char* key, const char* value).
   * @param flags  W3C trace flags byte (bit 0 = sampled; default 0x01).
   */
  template <typename Setter>
  static inline void inject(Setter set, uint8_t flags = 0x01) {
  const auto& ctx = OTel::currentTraceContext();

  // Only inject if we actually have a valid active context
  if (ctx.traceId.length() != 32 || ctx.spanId.length() != 16) {
    return; // no active span/context; skip injection rather than invent IDs
  }

  char tpbuf[64];
  // "00-" + 32 + "-" + 16 + "-" + 2 = 55 chars
  snprintf(tpbuf, sizeof(tpbuf), "00-%s-%s-%02x",
           ctx.traceId.c_str(),
           ctx.spanId.c_str(),
           static_cast<unsigned>(flags));

  set("traceparent", tpbuf);

  // NOTE: your TraceContext doesn't have tracestate; omit it to avoid compile errors.
  // If you add tracestate in future, you can forward it here.
}

  /**
   * Inject the active trace context as a @c traceparent key into an ArduinoJson document.
   * @param doc    JSON document to write into.
   * @param flags  W3C trace flags byte.
   */
  static inline void injectToJson(JsonDocument& doc, uint8_t flags = 0x01) {
  inject([&](const char* k, const char* v){ doc[k] = v; }, flags);
}

  /**
   * Inject the active trace context into HTTP headers via a generic adder callable.
   * @param add    Callable accepting @c (const char* name, const char* value)
   *               — e.g. pass @c http.addHeader directly.
   * @param flags  W3C trace flags byte.
   */
  template <typename HeaderAdder>
  static inline void injectToHeaders(HeaderAdder add, uint8_t flags = 0x01) {
  inject(add, flags);
}


};

/**
 * RAII guard that installs a remote parent context as the active TraceContext
 * for the duration of its lifetime, then restores the previous context on
 * destruction.  Use when processing inbound requests or messages that carry
 * a W3C traceparent so child spans are linked to the upstream trace.
 */
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

/** Convert uint64 to its decimal String representation without printf (RP2040-safe). */
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
/** Best-effort chip ID as a hex string; used as a default for service.instance.id. */
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

/** @return Default service name from @c OTEL_SERVICE_NAME, or "embedded-service". */
static inline String defaultServiceName() {
#ifdef OTEL_SERVICE_NAME
  return String(OTEL_SERVICE_NAME);
#else
  return String("embedded-service");
#endif
}
/** @return Default service instance ID from @c OTEL_SERVICE_INSTANCE, or the chip ID hex. */
static inline String defaultServiceInstanceId() {
#ifdef OTEL_SERVICE_INSTANCE
  return String(OTEL_SERVICE_INSTANCE);
#else
  return chipIdHex();
#endif
}
/** @return Default host name from @c OTEL_HOST_NAME, or "ESP-" + chip ID hex. */
static inline String defaultHostName() {
#ifdef OTEL_HOST_NAME
  return String(OTEL_HOST_NAME);
#else
  return String("ESP-") + chipIdHex();
#endif
}

// ---- Entropy + ID helpers ---------------------------------------------------

/**
 * XOR a boot-time salt derived from the system clock and service instance ID
 * into @p b to reduce cross-boot ID collisions on platforms with weak RNGs.
 */
static inline void mix_boot_salt(uint8_t* b, size_t len) {
  uint64_t t = nowUnixNano();
  uint32_t salt = (uint32_t)t ^ (uint32_t)(t >> 32);

#if defined(ARDUINO_ARCH_RP2040)
  // Mix in fast timers for jitter
  salt ^= (uint32_t)micros();
  salt ^= (uint32_t)millis();
#endif

  String inst = defaultServiceInstanceId(); // "000000" on Pico
  for (size_t i = 0; i < inst.length(); ++i)
    salt = (salt * 16777619u) ^ (uint8_t)inst[i];

  for (size_t i = 0; i < len; ++i) {
    uint8_t s = (uint8_t)((salt >> ((i & 3) * 8)) & 0xFF);
    b[i] ^= s;
  }
}


/** Seed the PRNG with hardware entropy sources and mix in boot-time jitter. */
static inline void seedEntropy() {
  uint32_t seed = 0;

#if defined(ESP32)
  seed ^= esp_random();
  seed ^= (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFFULL);
#elif defined(ESP8266)
  seed ^= os_random();
  seed ^= system_get_rtc_time();
  seed ^= ESP.getChipId();
#elif defined(ARDUINO_ARCH_RP2040)
  seed ^= get_rand_32();
#else
  seed ^= (uint32_t)micros();
  seed ^= (uint32_t)millis();
#endif

  // Also mix in our time util so different boots don’t repeat
  const uint64_t now = nowUnixNano();
  seed ^= (uint32_t)now;
  seed ^= (uint32_t)(now >> 32);

  randomSeed(seed);
  // Stir a little so early values aren’t correlated
  for (int i = 0; i < 8; ++i) (void)random();
}

/**
 * Fill @p out with @p len cryptographically random bytes using the best
 * hardware source available for the target platform.
 */
static inline void fillRandom(uint8_t* out, size_t len) {
#if defined(ESP32)
  esp_fill_random(out, len);
#elif defined(ESP8266)
  for (size_t i = 0; i < len; i += 4) {
    uint32_t r = os_random();
    size_t n = (len - i < 4) ? (len - i) : 4;
    memcpy(out + i, &r, n);
  }
#elif defined(ARDUINO_ARCH_RP2040)
  for (size_t i = 0; i < len; i += 4) {
    uint32_t r = get_rand_32();
    size_t n = (len - i < 4) ? (len - i) : 4;
    memcpy(out + i, &r, n);
  }
#else
  for (size_t i = 0; i < len; ++i) out[i] = (uint8_t)random(0, 256);
#endif
}

/** Encode @p len bytes of @p data as a lowercase hex string. */
static inline String toHex(const uint8_t* data, size_t len) {
  static const char* hex = "0123456789abcdef";
  String out; out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += hex[data[i] >> 4];
    out += hex[data[i] & 0x0F];
  }
  return out;
}

/** Generate a random 128-bit trace ID as a 32-char lowercase hex string. */
static inline String generateTraceId() {
  uint8_t b[16];
  fillRandom(b, sizeof b);

  Serial.printf("[otel] pre-salt trace bytes: %02x %02x %02x %02x\n", b[0], b[1], b[2], b[3]);

  mix_boot_salt(b, sizeof b);      // <— always mix boot salt
                                   //
  Serial.printf("[otel] post-salt trace bytes: %02x %02x %02x %02x\n", b[0], b[1], b[2], b[3]);

  // Ensure not all zeros (W3C requirement)
  bool allZero = true; for (uint8_t v : b) { if (v) { allZero = false; break; } }
  static uint32_t seq = 0;
  if (allZero) {
    // Fallback: mix time and a sequence
    uint64_t t = nowUnixNano();
    memcpy(b, &t, (sizeof b < sizeof t) ? sizeof b : sizeof t);
  }
  // Mix in a boot-local monotonic to avoid intra-process collisions
  uint32_t s = ++seq;
  b[12] ^= (uint8_t)(s >> 24);
  b[13] ^= (uint8_t)(s >> 16);
  b[14] ^= (uint8_t)(s >> 8);
  b[15] ^= (uint8_t)(s);

  String h = toHex(b, sizeof b);
  Serial.printf("[otel] traceId=%s\n", h.c_str());   // DEBUG
  return h;
}

/** Generate a random 64-bit span ID as a 16-char lowercase hex string. */
static inline String generateSpanId() {
  uint8_t b[8];
  fillRandom(b, sizeof b);
  mix_boot_salt(b, sizeof b);      // <— always mix boot salt

  bool allZero = true; for (uint8_t v : b) { if (v) { allZero = false; break; } }
  static uint32_t seq = 0;
  if (allZero) {
    uint32_t t = (uint32_t)micros();
    memcpy(b, &t, (sizeof b < sizeof t) ? sizeof b : sizeof t);
  }
  uint32_t s = ++seq;
  b[4] ^= (uint8_t)(s >> 24);
  b[5] ^= (uint8_t)(s >> 16);
  b[6] ^= (uint8_t)(s >> 8);
  b[7] ^= (uint8_t)(s);

  String h = toHex(b, sizeof b);
  Serial.printf("[otel] spanId=%s\n", h.c_str());   // DEBUG
  return h;
}



/** Append a string-valued OTLP KeyValue object to a resource attributes array. */
static inline void addResAttr(JsonArray& arr, const char* key, const String& value) {
  JsonObject a = arr.add<JsonObject>();
  a["key"] = key;
  a["value"].to<JsonObject>()["stringValue"] = value;
}

/** Instrumentation scope name and version emitted on every trace payload. */
struct TracerConfig {
  String scopeName{"otel-embedded"};
  String scopeVersion{"0.1.0"};
};

/** Returns the process-wide TracerConfig singleton. */
inline TracerConfig& tracerConfig() {
  static TracerConfig cfg;
  return cfg;
}

/**
 * A single OTLP span.  Create via @c Tracer::startSpan(); the span is
 * automatically sent when @c end() is called or the object goes out of scope.
 *
 * Spans are not copyable.  They are movable so you can store them in a
 * container or return them from a factory function.
 */
class Span {
public:
  explicit Span(const String& name)
  : name_(name),
    traceId_(currentTraceContext().valid() ? currentTraceContext().traceId : generateTraceId()),
    spanId_(generateSpanId()),
    startNs_(nowUnixNano())
  {
    Serial.printf("[otel] Span('%s') trace=%s\n", name.c_str(), traceId_.c_str());
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

  Span(const Span&) = delete;
  Span& operator=(const Span&) = delete;

  // Movable — transfer ownership so the source won't end() later
  Span(Span&& o) noexcept
  : name_(std::move(o.name_)),
    traceId_(std::move(o.traceId_)),
    spanId_(std::move(o.spanId_)),
    startNs_(o.startNs_),
    prevTraceId_(std::move(o.prevTraceId_)),
    prevSpanId_(std::move(o.prevSpanId_)),
    attrs_(std::move(o.attrs_)),
    events_(std::move(o.events_)),
    ended_(o.ended_)
  {
    o.ended_ = true;          // source dtor becomes a no-op
    o.prevTraceId_ = "";
    o.prevSpanId_  = "";
  }

  Span& operator=(Span&& o) noexcept {
    if (this != &o) {
      if (!ended_) end();     // finish our current span if still open
      name_        = std::move(o.name_);
      traceId_     = std::move(o.traceId_);
      spanId_      = std::move(o.spanId_);
      startNs_     = o.startNs_;
      prevTraceId_ = std::move(o.prevTraceId_);
      prevSpanId_  = std::move(o.prevSpanId_);
      attrs_       = std::move(o.attrs_);
      events_      = std::move(o.events_);
      ended_       = o.ended_;
      o.ended_     = true;    // source won't end() again
      o.prevTraceId_ = "";
      o.prevSpanId_  = "";
    }
    return *this;
  }

  /** @{ Add a typed attribute to the span.  Attributes are buffered until @c end(). */
  Span& setAttribute(const String& key, const String& v) {
    //attrs_.push_back(Attr{key, Type::Str, v, 0, 0.0, false});
    Attr a;
    a.key  = key;
    a.type = Type::Str;
    a.s    = v;
    a.i    = 0;
    a.d    = 0.0;
    a.b    = false;
    attrs_.push_back(a);
    return *this;
  }
  Span& setAttribute(const String& key, const char* v) {
    return setAttribute(key, String(v));
  }
  Span& setAttribute(const String& key, int64_t v) {
    Attr a; a.key=key; a.type=Type::Int; a.i=v; attrs_.push_back(a); return *this;
  }
  Span& setAttribute(const String& key, double v) {
    Attr a; a.key=key; a.type=Type::Dbl; a.d=v; attrs_.push_back(a); return *this;
  }
  Span& setAttribute(const String& key, bool v) {
    Attr a; a.key=key; a.type=Type::Bool; a.b=v; attrs_.push_back(a); return *this;
  }
  /** @} */

  /**
   * Record a timestamped event on this span.
   * @param name  Human-readable event name, e.g. "cache.miss".
   */
  Span& addEvent(const String& name) {
    //events_.push_back(Event{name, nowUnixNano(), {}});
    Event e;
    e.name = name;
    e.t    = nowUnixNano();
    events_.push_back(e);
    return *this;
  }
  /**
   * Record a timestamped event with string attributes.
   * @param name   Human-readable event name.
   * @param attrs  Key/value pairs attached to the event.
   */
  Span& addEvent(const String& name, const std::vector<std::pair<String,String>>& attrs) {
    //Event e{name, nowUnixNano(), {}};
    // NEW
    Event e;
    e.name = name;
    e.t    = nowUnixNano();
    e.attrs.reserve(attrs.size());
    for (const auto& kv : attrs) {
      //e.attrs.push_back(Attr{kv.first, Type::Str, kv.second, 0, 0.0, false});
      Attr a;
        a.key  = kv.first;
        a.type = Type::Str;
        a.s    = kv.second;
        a.i    = 0;
        a.d    = 0.0;
        a.b    = false;
        e.attrs.push_back(a);
    }
    events_.push_back(e);
    return *this;
  }

  /**
   * Finalise and transmit the span to the collector.
   * Safe to call more than once (idempotent).  Called automatically by the
   * destructor if the user does not call it explicitly.
   */
  void end() {
    if (ended_) return;               // idempotent guard
    ended_ = true;

    const uint64_t endNs = nowUnixNano();

#if OTEL_EXPORTER_OTLP_PROTOCOL == OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF
    {
      std::vector<OTel::Proto::Attr> protoAttrs;
      protoAttrs.reserve(attrs_.size());
      for (const auto& a : attrs_) {
        OTel::Proto::Attr pa;
        pa.key  = a.key;
        pa.type = static_cast<OTel::Proto::AttrType>(static_cast<int>(a.type));
        pa.s    = a.s;
        pa.i    = a.i;
        pa.d    = a.d;
        pa.b    = a.b;
        protoAttrs.push_back(std::move(pa));
      }
      std::vector<OTel::Proto::Event> protoEvents;
      protoEvents.reserve(events_.size());
      for (const auto& ev : events_) {
        OTel::Proto::Event pe;
        pe.name = ev.name;
        pe.t    = ev.t;
        pe.attrs.reserve(ev.attrs.size());
        for (const auto& a : ev.attrs) {
          OTel::Proto::Attr pa;
          pa.key  = a.key;
          pa.type = static_cast<OTel::Proto::AttrType>(static_cast<int>(a.type));
          pa.s    = a.s;
          pa.i    = a.i;
          pa.d    = a.d;
          pa.b    = a.b;
          pe.attrs.push_back(std::move(pa));
        }
        protoEvents.push_back(std::move(pe));
      }
      OTel::Proto::sendSpan(name_, traceId_, spanId_, prevSpanId_,
                            startNs_, endNs, protoAttrs, protoEvents);
    }
#else
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

    // ---------- NEW: serialise span attributes (if any) -----------------------
    if (!attrs_.empty()) {
      JsonArray a = s["attributes"].to<JsonArray>();
      for (const auto& at : attrs_) {
        JsonObject el = a.add<JsonObject>();
        el["key"] = at.key;
        JsonObject v = el["value"].to<JsonObject>();
        switch (at.type) {
          case Type::Str:  v["stringValue"] = at.s; break;
          case Type::Int:  v["intValue"]    = at.i; break;
          case Type::Dbl:  v["doubleValue"] = at.d; break;
          case Type::Bool: v["boolValue"]   = at.b; break;
        }
      }
    }

    // ---------- NEW: serialise span events (if any) ---------------------------
    if (!events_.empty()) {
      JsonArray evs = s["events"].to<JsonArray>();
      for (const auto& ev : events_) {
        JsonObject e = evs.add<JsonObject>();
        e["timeUnixNano"] = u64ToStr(ev.t);
        e["name"] = ev.name;

        if (!ev.attrs.empty()) {
          JsonArray ea = e["attributes"].to<JsonArray>();
          for (const auto& at : ev.attrs) {
            JsonObject el = ea.add<JsonObject>();
            el["key"] = at.key;
            JsonObject v = el["value"].to<JsonObject>();
            switch (at.type) {
              case Type::Str:  v["stringValue"] = at.s; break;
              case Type::Int:  v["intValue"]    = at.i; break;
              case Type::Dbl:  v["doubleValue"] = at.d; break;
              case Type::Bool: v["boolValue"]   = at.b; break;
            }
          }
        }
      }
    }

    // Send
    OTelSender::sendJson("/v1/traces", doc);
#endif  // OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF

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

  // NEW: small typed attribute/event storage kept until end()
  enum class Type { Str, Int, Dbl, Bool };
  struct Attr {
    String key;
    Type   type{Type::Str};
    String s;     // for strings
    int64_t i{0}; // for ints
    double  d{0}; // for doubles
    bool    b{false}; // for bools
  };
  struct Event {
    String name;
    uint64_t t{0};
    std::vector<Attr> attrs;
  };

private:
  String name_;
  String traceId_;
  String spanId_;
  uint64_t startNs_;

  // Previous active context (for parent linkage and restoration)
  String prevTraceId_;
  String prevSpanId_;

  // NEW: buffers
  std::vector<Attr>  attrs_;
  std::vector<Event> events_;

  // RAII guard
  bool ended_ = false;
};

/**
 * Static façade for creating OTLP spans.
 *
 * Call @c begin() once after connecting to Wi-Fi to seed the PRNG and set the
 * instrumentation scope name/version.  Then call @c startSpan() to instrument
 * any operation.
 */
class Tracer {
public:
  /**
   * Initialise the tracer: seed entropy, clear any stale context, and
   * configure the instrumentation scope.
   * @param scopeName    Library/component name, e.g. "my-firmware".
   * @param scopeVersion Semver string, e.g. "1.0.0".
   */
  static void begin(const String& scopeName, const String& scopeVersion) {
    seedEntropy();

    // NEW: nuke any stale IDs so the first Span *must* generate fresh ones
    currentTraceContext().traceId = "";
    currentTraceContext().spanId  = "";

    tracerConfig().scopeName    = scopeName;
    tracerConfig().scopeVersion = scopeVersion;
  }

  /**
   * Start a new span.  If a span is already active its IDs become the parent.
   * @param name  Human-readable operation name, e.g. "mqtt.publish".
   * @return      A @c Span object; call @c end() or let it go out of scope.
   */
  static Span startSpan(const String& name) {
    return Span(name);
  }
};

} // namespace OTel

#endif // OTEL_TRACER_H


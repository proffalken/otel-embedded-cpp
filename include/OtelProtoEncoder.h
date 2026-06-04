#pragma once

// Ensure the protocol macros are defined before we evaluate the guard below.
// OTEL_EXPORTER_OTLP_PROTOCOL defaults to 0 (JSON) and
// OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF is 1, both defined in OtelSender.h.
// Without this include, an undefined macro would expand to 0 on both sides,
// making the guard evaluate to true and silently activating the protobuf path.
#include "OtelSender.h"

// This header is only active when the protobuf protocol is selected.
// It provides encode functions called by Metrics, Logger, and Tracer
// instead of building a JsonDocument.
#if OTEL_EXPORTER_OTLP_PROTOCOL == OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF

#include <Arduino.h>
#include <map>
#include <vector>
#include <utility>

namespace OTel {
namespace Proto {

/** Discriminator for typed span/log attributes. */
enum class AttrType { Str, Int, Dbl, Bool };

/**
 * A single typed key/value attribute used in spans and log records.
 * Mirrors @c Span::Attr; forward-declared here to avoid a circular dependency
 * between OtelTracer.h and OtelProtoEncoder.h.
 */
struct Attr {
  String    key;
  AttrType  type{AttrType::Str};
  String    s;
  int64_t   i{0};
  double    d{0.0};
  bool      b{false};
};

/**
 * A span event (timestamped annotation) with an optional set of attributes.
 */
struct Event {
  String          name;
  uint64_t        t{0};
  std::vector<Attr> attrs;
};

// ── Signal encoders ──────────────────────────────────────────────────────────

/**
 * Encode and transmit a Gauge metric datapoint via OTLP/Protobuf.
 * @param name   Metric name.
 * @param value  Gauge value.
 * @param unit   UCUM unit string.
 * @param labels Merged default + per-call attributes.
 */
void sendGauge(const String& name, double value, const String& unit,
               const std::map<String,String>& labels);

/**
 * Encode and transmit a Sum metric datapoint via OTLP/Protobuf.
 * @param name         Metric name.
 * @param value        Sum value.
 * @param isMonotonic  True for monotonically increasing counters.
 * @param temporality  1 = DELTA, 2 = CUMULATIVE.
 * @param unit         UCUM unit string.
 * @param labels       Merged default + per-call attributes.
 */
void sendSum(const String& name, double value, bool isMonotonic,
             int temporality, const String& unit,
             const std::map<String,String>& labels);

/**
 * Encode and transmit a LogRecord via OTLP/Protobuf.
 * @param severity      Severity text ("INFO", "WARN", etc.).
 * @param severityNum   OTLP severity number (see severityNumberFromText()).
 * @param message       Log body.
 * @param callLabels    Per-call attributes.
 * @param defaultLabels Process-wide default labels.
 * @param traceId       Active trace ID (32 hex chars), or empty string.
 * @param spanId        Active span ID (16 hex chars), or empty string.
 */
void sendLog(const String& severity, int severityNum, const String& message,
             const std::map<String,String>& callLabels,
             const std::map<String,String>& defaultLabels,
             const String& traceId, const String& spanId);

/**
 * Encode and transmit a Span via OTLP/Protobuf.
 * @param name         Operation name.
 * @param traceId      32 hex-char trace identifier.
 * @param spanId       16 hex-char span identifier.
 * @param parentSpanId 16 hex-char parent span ID, or empty string for root spans.
 * @param startNs      Span start time in nanoseconds since UNIX epoch.
 * @param endNs        Span end time in nanoseconds since UNIX epoch.
 * @param attrs        Span attributes.
 * @param events       Span events (timestamped annotations).
 */
void sendSpan(const String& name,
              const String& traceId,
              const String& spanId,
              const String& parentSpanId,
              uint64_t startNs,
              uint64_t endNs,
              const std::vector<Attr>& attrs,
              const std::vector<Event>& events);

} // namespace Proto
} // namespace OTel

#endif // OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF

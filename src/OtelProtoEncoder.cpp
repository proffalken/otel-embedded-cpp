#include "OtelSender.h"

#if OTEL_EXPORTER_OTLP_PROTOCOL == OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF

// Nanopb-generated OTLP descriptors — provided by aodtorusan/opentelemetry_proto.
// PlatformIO compiles the library's .pb.c files automatically; no .pb.inc hack needed.
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"
#include "opentelemetry/proto/logs/v1/logs.pb.h"

#include "OtelProtoEncoder.h"
#include "OtelTracer.h"   // defaultServiceName(), defaultServiceInstanceId(), defaultHostName()
#include "OtelMetrics.h"  // metricsScopeConfig()
#include "OtelLogger.h"   // logScopeConfig()

#include <pb_encode.h>

namespace OTel {
namespace Proto {

// ── Primitive callbacks ──────────────────────────────────────────────────────

// Encode a single C-string as a length-delimited protobuf bytes/string field.
static bool cb_cstr(pb_ostream_t* s, const pb_field_t* f, void* const* arg) {
  const char* str = *(const char* const*)arg;
  if (!str || !*str) return true; // skip empty strings
  return pb_encode_tag_for_field(s, f) &&
         pb_encode_string(s, (const pb_byte_t*)str, strlen(str));
}

// ── Map<String,String> → repeated KeyValue ───────────────────────────────────

struct MapCtx { const std::map<String,String>* m; };

static bool cb_map_attrs(pb_ostream_t* s, const pb_field_t* f, void* const* arg) {
  auto* ctx = *(MapCtx**)arg;
  for (const auto& kv : *ctx->m) {
    opentelemetry_proto_common_v1_KeyValue entry = opentelemetry_proto_common_v1_KeyValue_init_zero;
    entry.key.funcs.encode = cb_cstr;
    entry.key.arg          = (void*)kv.first.c_str();
    entry.has_value        = true;
    entry.value.which_value = opentelemetry_proto_common_v1_AnyValue_string_value_tag;
    entry.value.value.string_value.funcs.encode = cb_cstr;
    entry.value.value.string_value.arg          = (void*)kv.second.c_str();

    if (!pb_encode_tag_for_field(s, f)) return false;
    if (!pb_encode_submessage(s, opentelemetry_proto_common_v1_KeyValue_fields, &entry)) return false;
  }
  return true;
}

// Merged map: encodes defaultLabels first, then callLabels.
struct MergedMapCtx {
  const std::map<String,String>* defaults;
  const std::map<String,String>* call;
};

static bool cb_merged_map_attrs(pb_ostream_t* s, const pb_field_t* f, void* const* arg) {
  auto* ctx = *(MergedMapCtx**)arg;
  // Build a single merged map so call-site labels override defaults without
  // emitting duplicate keys (OTLP backends differ on which duplicate wins).
  std::map<String, String> merged(*ctx->defaults);
  for (const auto& kv : *ctx->call)
    merged[kv.first] = kv.second;

  for (const auto& kv : merged) {
    opentelemetry_proto_common_v1_KeyValue entry = opentelemetry_proto_common_v1_KeyValue_init_zero;
    entry.key.funcs.encode = cb_cstr;
    entry.key.arg          = (void*)kv.first.c_str();
    entry.has_value        = true;
    entry.value.which_value = opentelemetry_proto_common_v1_AnyValue_string_value_tag;
    entry.value.value.string_value.funcs.encode = cb_cstr;
    entry.value.value.string_value.arg          = (void*)kv.second.c_str();

    if (!pb_encode_tag_for_field(s, f)) return false;
    if (!pb_encode_submessage(s, opentelemetry_proto_common_v1_KeyValue_fields, &entry)) return false;
  }
  return true;
}

// ── Resource builder ─────────────────────────────────────────────────────────

// Fills a Resource with service.name / service.instance.id / host.name.
// Backed by three static String values so the c_str() pointers are stable
// for the duration of the encode call.
static void fillResource(opentelemetry_proto_resource_v1_Resource& res,
                         String& svcName, String& svcInst, String& hostName) {
  svcName  = defaultServiceName();
  svcInst  = defaultServiceInstanceId();
  hostName = defaultHostName();

  // Inline KeyValue list via a callback that emits exactly three entries.
  struct ResCtx { const char* sn; const char* si; const char* hn; };
  static ResCtx rctx;
  rctx = {svcName.c_str(), svcInst.c_str(), hostName.c_str()};

  res.attributes.funcs.encode = [](pb_ostream_t* s, const pb_field_t* f, void* const* arg) -> bool {
    auto* c = *(ResCtx**)arg;
    const char* keys[]   = {"service.name", "service.instance.id", "host.name"};
    const char* values[] = {c->sn, c->si, c->hn};
    for (int i = 0; i < 3; ++i) {
      opentelemetry_proto_common_v1_KeyValue kv = opentelemetry_proto_common_v1_KeyValue_init_zero;
      kv.key.funcs.encode = cb_cstr; kv.key.arg = (void*)keys[i];
      kv.has_value = true;
      kv.value.which_value = opentelemetry_proto_common_v1_AnyValue_string_value_tag;
      kv.value.value.string_value.funcs.encode = cb_cstr;
      kv.value.value.string_value.arg = (void*)values[i];
      if (!pb_encode_tag_for_field(s, f)) return false;
      if (!pb_encode_submessage(s, opentelemetry_proto_common_v1_KeyValue_fields, &kv)) return false;
    }
    return true;
  };
  res.attributes.arg = &rctx;
}

// ── Scope builder ────────────────────────────────────────────────────────────

static void fillScope(opentelemetry_proto_common_v1_InstrumentationScope& scope,
                      const char* name, const char* version) {
  scope.name.funcs.encode    = cb_cstr; scope.name.arg    = (void*)name;
  scope.version.funcs.encode = cb_cstr; scope.version.arg = (void*)version;
}

// ── Hex string → raw bytes ───────────────────────────────────────────────────

static void hexToBytes(const String& hex, uint8_t* out, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    size_t ci = i * 2;
    if (ci + 1 >= (size_t)hex.length()) { out[i] = 0; continue; }
    auto n = [](char c) -> uint8_t {
      return (c >= 'a') ? c - 'a' + 10 : (c >= 'A') ? c - 'A' + 10 : c - '0';
    };
    out[i] = (n(hex[ci]) << 4) | n(hex[ci + 1]);
  }
}

// ── Encode + send helpers ────────────────────────────────────────────────────

static void encodeAndSend(const char* path,
                          const pb_msgdesc_t* fields,
                          const void* msg) {
  uint8_t buf[OTEL_PROTO_BUFFER_SIZE];
  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  if (pb_encode(&stream, fields, msg)) {
    OTelSender::sendProto(path, buf, stream.bytes_written);
  }
  // Silent drop on encode failure — same behaviour as JSON OOM.
}

// ════════════════════════════════════════════════════════════════════════════
// Public API
// ════════════════════════════════════════════════════════════════════════════

// ── Metrics ──────────────────────────────────────────────────────────────────

static void buildAndSendNumberMetric(const String& name, double value,
                                     const String& unit,
                                     const std::map<String,String>& labels,
                                     bool isGauge,
                                     bool isMonotonic,
                                     int  temporality) {
  using namespace OTel;

  String svcName, svcInst, hostName;

  // DataPoint
  opentelemetry_proto_metrics_v1_NumberDataPoint dp =
      opentelemetry_proto_metrics_v1_NumberDataPoint_init_zero;
  dp.time_unix_nano      = nowUnixNano();
  dp.which_value         = opentelemetry_proto_metrics_v1_NumberDataPoint_as_double_tag;
  dp.value.as_double     = value;
  MapCtx dpCtx{&labels};
  dp.attributes.funcs.encode = cb_map_attrs;
  dp.attributes.arg          = &dpCtx;

  // Metric (Gauge or Sum)
  opentelemetry_proto_metrics_v1_Metric metric =
      opentelemetry_proto_metrics_v1_Metric_init_zero;
  metric.name.funcs.encode = cb_cstr; metric.name.arg = (void*)name.c_str();
  metric.unit.funcs.encode = cb_cstr; metric.unit.arg = (void*)unit.c_str();

  // DataPoint callback for the metric type
  struct DpCtx { opentelemetry_proto_metrics_v1_NumberDataPoint* dp; };
  static DpCtx dpWrap;
  dpWrap.dp = &dp;
  auto dp_cb = [](pb_ostream_t* s, const pb_field_t* f, void* const* arg) -> bool {
    auto* c = *(DpCtx**)arg;
    return pb_encode_tag_for_field(s, f) &&
           pb_encode_submessage(s, opentelemetry_proto_metrics_v1_NumberDataPoint_fields, c->dp);
  };

  if (isGauge) {
    metric.which_data = opentelemetry_proto_metrics_v1_Metric_gauge_tag;
    metric.data.gauge.data_points.funcs.encode = dp_cb;
    metric.data.gauge.data_points.arg          = &dpWrap;
  } else {
    metric.which_data = opentelemetry_proto_metrics_v1_Metric_sum_tag;
    metric.data.sum.is_monotonic           = isMonotonic;
    metric.data.sum.aggregation_temporality =
        (opentelemetry_proto_metrics_v1_AggregationTemporality)temporality;
    metric.data.sum.data_points.funcs.encode = dp_cb;
    metric.data.sum.data_points.arg          = &dpWrap;
  }

  // ScopeMetrics
  opentelemetry_proto_metrics_v1_ScopeMetrics sm =
      opentelemetry_proto_metrics_v1_ScopeMetrics_init_zero;
  fillScope(sm.scope, metricsScopeConfig().scopeName.c_str(),
                      metricsScopeConfig().scopeVersion.c_str());
  struct SmCtx { opentelemetry_proto_metrics_v1_Metric* m; };
  static SmCtx smCtx; smCtx.m = &metric;
  sm.metrics.funcs.encode = [](pb_ostream_t* s, const pb_field_t* f, void* const* arg) -> bool {
    auto* c = *(SmCtx**)arg;
    return pb_encode_tag_for_field(s, f) &&
           pb_encode_submessage(s, opentelemetry_proto_metrics_v1_Metric_fields, c->m);
  };
  sm.metrics.arg = &smCtx;

  // ResourceMetrics
  opentelemetry_proto_metrics_v1_ResourceMetrics rm =
      opentelemetry_proto_metrics_v1_ResourceMetrics_init_zero;
  rm.has_resource = true;
  fillResource(rm.resource, svcName, svcInst, hostName);
  struct RmCtx { opentelemetry_proto_metrics_v1_ScopeMetrics* sm; };
  static RmCtx rmCtx; rmCtx.sm = &sm;
  rm.scope_metrics.funcs.encode = [](pb_ostream_t* s, const pb_field_t* f, void* const* arg) -> bool {
    auto* c = *(RmCtx**)arg;
    return pb_encode_tag_for_field(s, f) &&
           pb_encode_submessage(s, opentelemetry_proto_metrics_v1_ScopeMetrics_fields, c->sm);
  };
  rm.scope_metrics.arg = &rmCtx;

  // MetricsData
  opentelemetry_proto_metrics_v1_MetricsData data =
      opentelemetry_proto_metrics_v1_MetricsData_init_zero;
  struct DataCtx { opentelemetry_proto_metrics_v1_ResourceMetrics* rm; };
  static DataCtx dataCtx; dataCtx.rm = &rm;
  data.resource_metrics.funcs.encode = [](pb_ostream_t* s, const pb_field_t* f, void* const* arg) -> bool {
    auto* c = *(DataCtx**)arg;
    return pb_encode_tag_for_field(s, f) &&
           pb_encode_submessage(s, opentelemetry_proto_metrics_v1_ResourceMetrics_fields, c->rm);
  };
  data.resource_metrics.arg = &dataCtx;

  encodeAndSend("/v1/metrics",
                opentelemetry_proto_metrics_v1_MetricsData_fields, &data);
}

void sendGauge(const String& name, double value, const String& unit,
               const std::map<String,String>& labels) {
  buildAndSendNumberMetric(name, value, unit, labels, true, false, 0);
}

void sendSum(const String& name, double value, bool isMonotonic,
             int temporality, const String& unit,
             const std::map<String,String>& labels) {
  buildAndSendNumberMetric(name, value, unit, labels, false, isMonotonic, temporality);
}

// ── Logs ─────────────────────────────────────────────────────────────────────

void sendLog(const String& severity, int severityNum, const String& message,
             const std::map<String,String>& callLabels,
             const std::map<String,String>& defaultLabels,
             const String& traceId, const String& spanId) {

  String svcName, svcInst, hostName;

  // LogRecord
  opentelemetry_proto_logs_v1_LogRecord lr =
      opentelemetry_proto_logs_v1_LogRecord_init_zero;
  lr.time_unix_nano    = nowUnixNano();
  lr.severity_number   = (opentelemetry_proto_logs_v1_SeverityNumber)severityNum;
  lr.severity_text.funcs.encode = cb_cstr;
  lr.severity_text.arg          = (void*)severity.c_str();
  lr.has_body          = true;
  lr.body.which_value  = opentelemetry_proto_common_v1_AnyValue_string_value_tag;
  lr.body.value.string_value.funcs.encode = cb_cstr;
  lr.body.value.string_value.arg          = (void*)message.c_str();

  MergedMapCtx logAttrs{&defaultLabels, &callLabels};
  lr.attributes.funcs.encode = cb_merged_map_attrs;
  lr.attributes.arg          = &logAttrs;

  // Trace correlation
  uint8_t traceBytes[16]{}, spanBytes[8]{};
  if (traceId.length() == 32) {
    hexToBytes(traceId, traceBytes, 16);
    lr.trace_id.funcs.encode = [](pb_ostream_t* s, const pb_field_t* f, void* const* arg) -> bool {
      return pb_encode_tag_for_field(s, f) &&
             pb_encode_string(s, *(const pb_byte_t* const*)arg, 16);
    };
    lr.trace_id.arg = traceBytes;
  }
  if (spanId.length() == 16) {
    hexToBytes(spanId, spanBytes, 8);
    lr.span_id.funcs.encode = [](pb_ostream_t* s, const pb_field_t* f, void* const* arg) -> bool {
      return pb_encode_tag_for_field(s, f) &&
             pb_encode_string(s, *(const pb_byte_t* const*)arg, 8);
    };
    lr.span_id.arg = spanBytes;
  }

  // ScopeLogs
  opentelemetry_proto_logs_v1_ScopeLogs sl =
      opentelemetry_proto_logs_v1_ScopeLogs_init_zero;
  fillScope(sl.scope, logScopeConfig().scopeName.c_str(),
                      logScopeConfig().scopeVersion.c_str());
  struct SlCtx { opentelemetry_proto_logs_v1_LogRecord* lr; };
  static SlCtx slCtx; slCtx.lr = &lr;
  sl.log_records.funcs.encode = [](pb_ostream_t* s, const pb_field_t* f, void* const* arg) -> bool {
    auto* c = *(SlCtx**)arg;
    return pb_encode_tag_for_field(s, f) &&
           pb_encode_submessage(s, opentelemetry_proto_logs_v1_LogRecord_fields, c->lr);
  };
  sl.log_records.arg = &slCtx;

  // ResourceLogs
  opentelemetry_proto_logs_v1_ResourceLogs rl =
      opentelemetry_proto_logs_v1_ResourceLogs_init_zero;
  rl.has_resource = true;
  fillResource(rl.resource, svcName, svcInst, hostName);
  struct RlCtx { opentelemetry_proto_logs_v1_ScopeLogs* sl; };
  static RlCtx rlCtx; rlCtx.sl = &sl;
  rl.scope_logs.funcs.encode = [](pb_ostream_t* s, const pb_field_t* f, void* const* arg) -> bool {
    auto* c = *(RlCtx**)arg;
    return pb_encode_tag_for_field(s, f) &&
           pb_encode_submessage(s, opentelemetry_proto_logs_v1_ScopeLogs_fields, c->sl);
  };
  rl.scope_logs.arg = &rlCtx;

  // LogsData
  opentelemetry_proto_logs_v1_LogsData ld =
      opentelemetry_proto_logs_v1_LogsData_init_zero;
  struct LdCtx { opentelemetry_proto_logs_v1_ResourceLogs* rl; };
  static LdCtx ldCtx; ldCtx.rl = &rl;
  ld.resource_logs.funcs.encode = [](pb_ostream_t* s, const pb_field_t* f, void* const* arg) -> bool {
    auto* c = *(LdCtx**)arg;
    return pb_encode_tag_for_field(s, f) &&
           pb_encode_submessage(s, opentelemetry_proto_logs_v1_ResourceLogs_fields, c->rl);
  };
  ld.resource_logs.arg = &ldCtx;

  encodeAndSend("/v1/logs",
                opentelemetry_proto_logs_v1_LogsData_fields, &ld);
}

// ── Traces ───────────────────────────────────────────────────────────────────
// Traces are not in the reference proto set, so we encode manually using
// nanopb's streaming API with the field numbers from the OTLP trace proto.
// Field numbers: https://github.com/open-telemetry/opentelemetry-proto
//   TracesData.resource_spans        = 1
//   ResourceSpans.resource           = 1
//   ResourceSpans.scope_spans        = 2
//   ScopeSpans.scope                 = 1
//   ScopeSpans.spans                 = 2
//   Span.trace_id                    = 1  (bytes, 16)
//   Span.span_id                     = 2  (bytes, 8)
//   Span.parent_span_id              = 4  (bytes, 8)
//   Span.name                        = 5
//   Span.kind                        = 6  (SERVER = 2)
//   Span.start_time_unix_nano        = 7  (fixed64)
//   Span.end_time_unix_nano          = 8  (fixed64)
//   Span.attributes                  = 9  (repeated KeyValue)
//   Span.events                      = 11 (repeated Span.Event)
//   Span.Event.time_unix_nano        = 1  (fixed64)
//   Span.Event.name                  = 2
//   Span.Event.attributes            = 3  (repeated KeyValue)
//
//   InstrumentationScope.name        = 1
//   InstrumentationScope.version     = 2
//   Resource.attributes              = 1  (repeated KeyValue)
//   KeyValue.key                     = 1
//   KeyValue.value                   = 2 → AnyValue
//   AnyValue.string_value            = 1 (oneof)

static bool writeVarInt(pb_ostream_t* s, uint64_t v) {
  return pb_encode_varint(s, v);
}
static bool writeTag(pb_ostream_t* s, uint32_t field, pb_wire_type_t wt) {
  return pb_encode_tag(s, wt, field);
}
static bool writeFixed64(pb_ostream_t* s, uint64_t v) {
  uint8_t buf[8];
  for (int i = 0; i < 8; ++i) { buf[i] = v & 0xFF; v >>= 8; }
  return pb_write(s, buf, 8);
}
static bool writeBytes(pb_ostream_t* s, uint32_t field,
                        const uint8_t* data, size_t len) {
  return writeTag(s, field, PB_WT_STRING) &&
         pb_encode_varint(s, len) &&
         pb_write(s, data, len);
}
static bool writeString(pb_ostream_t* s, uint32_t field, const char* str) {
  if (!str || !*str) return true;
  size_t len = strlen(str);
  return writeTag(s, field, PB_WT_STRING) &&
         pb_encode_varint(s, len) &&
         pb_write(s, (const pb_byte_t*)str, len);
}

// Encode one KeyValue with a string value into stream.
static bool writeKVStr(pb_ostream_t* s, uint32_t field,
                        const char* key, const char* val) {
  // KV submessage: two-pass (size then content)
  auto writeKVBody = [](pb_ostream_t* s, const char* key, const char* val) -> bool {
    if (!writeString(s, 1, key)) return false; // KeyValue.key
    // KeyValue.value = AnyValue (field 2), AnyValue.string_value (field 1)
    // AnyValue submessage
    size_t avLen = 0;
    pb_ostream_t sz = PB_OSTREAM_SIZING;
    writeString(&sz, 1, val); // AnyValue.string_value
    avLen = sz.bytes_written;
    if (!writeTag(s, 2, PB_WT_STRING)) return false;
    if (!pb_encode_varint(s, avLen)) return false;
    return writeString(s, 1, val);
  };

  pb_ostream_t sz = PB_OSTREAM_SIZING;
  writeKVBody(&sz, key, val);
  if (!writeTag(s, field, PB_WT_STRING)) return false;
  if (!pb_encode_varint(s, sz.bytes_written)) return false;
  return writeKVBody(s, key, val);
}

// Encode one typed Attr as a KeyValue submessage.
static bool writeAttr(pb_ostream_t* s, uint32_t field, const Attr& a) {
  auto writeBody = [](pb_ostream_t* s, const Attr& a) -> bool {
    if (!writeString(s, 1, a.key.c_str())) return false;
    // Build AnyValue body
    auto writeAVBody = [](pb_ostream_t* s, const Attr& a) -> bool {
      switch (a.type) {
        case AttrType::Str:
          return writeString(s, 1, a.s.c_str());
        case AttrType::Bool:
          return writeTag(s, 4, PB_WT_VARINT) && writeVarInt(s, a.b ? 1 : 0);
        case AttrType::Int:
          return writeTag(s, 3, PB_WT_VARINT) && writeVarInt(s, (uint64_t)a.i);
        case AttrType::Dbl: {
          if (!writeTag(s, 5, PB_WT_64BIT)) return false;
          uint64_t v; memcpy(&v, &a.d, 8);
          return writeFixed64(s, v);
        }
      }
      return true;
    };
    pb_ostream_t sz = PB_OSTREAM_SIZING;
    writeAVBody(&sz, a);
    if (!writeTag(s, 2, PB_WT_STRING)) return false;
    if (!pb_encode_varint(s, sz.bytes_written)) return false;
    return writeAVBody(s, a);
  };

  pb_ostream_t sz = PB_OSTREAM_SIZING;
  writeBody(&sz, a);
  if (!writeTag(s, field, PB_WT_STRING)) return false;
  if (!pb_encode_varint(s, sz.bytes_written)) return false;
  return writeBody(s, a);
}

// Encode a Span.Event submessage.
static bool writeEvent(pb_ostream_t* s, uint32_t field, const Event& ev) {
  auto writeBody = [](pb_ostream_t* s, const Event& ev) -> bool {
    if (!writeTag(s, 1, PB_WT_64BIT)) return false; // time_unix_nano
    if (!writeFixed64(s, ev.t)) return false;
    if (!writeString(s, 2, ev.name.c_str())) return false; // name
    for (const auto& a : ev.attrs)
      if (!writeAttr(s, 3, a)) return false;               // attributes
    return true;
  };
  pb_ostream_t sz = PB_OSTREAM_SIZING;
  writeBody(&sz, ev);
  if (!writeTag(s, field, PB_WT_STRING)) return false;
  if (!pb_encode_varint(s, sz.bytes_written)) return false;
  return writeBody(s, ev);
}

void sendSpan(const String& name,
              const String& traceId,
              const String& spanId,
              const String& parentSpanId,
              uint64_t startNs,
              uint64_t endNs,
              const std::vector<Attr>& attrs,
              const std::vector<Event>& events) {

  String svcName  = defaultServiceName();
  String svcInst  = defaultServiceInstanceId();
  String hostName = defaultHostName();

  uint8_t traceBytes[16]{}, spanBytes[8]{}, parentBytes[8]{};
  bool hasParent = parentSpanId.length() == 16;
  if (traceId.length()  == 32) hexToBytes(traceId,       traceBytes,  16);
  if (spanId.length()   == 16) hexToBytes(spanId,        spanBytes,    8);
  if (hasParent)               hexToBytes(parentSpanId,  parentBytes,  8);

  // Two-pass encode for the entire TracesData message.
  auto writeSpanBody = [&](pb_ostream_t* s) -> bool {
    if (!writeBytes(s, 1, traceBytes, 16)) return false;           // trace_id
    if (!writeBytes(s, 2, spanBytes,   8)) return false;           // span_id
    if (hasParent)
      if (!writeBytes(s, 4, parentBytes, 8)) return false;         // parent_span_id
    if (!writeString(s, 5, name.c_str())) return false;            // name
    if (!writeTag(s, 6, PB_WT_VARINT)) return false;               // kind = SERVER
    if (!writeVarInt(s, 2)) return false;
    if (!writeTag(s, 7, PB_WT_64BIT)) return false;                // start_time
    if (!writeFixed64(s, startNs)) return false;
    if (!writeTag(s, 8, PB_WT_64BIT)) return false;                // end_time
    if (!writeFixed64(s, endNs)) return false;
    for (const auto& a : attrs)
      if (!writeAttr(s, 9, a)) return false;                       // attributes
    for (const auto& ev : events)
      if (!writeEvent(s, 11, ev)) return false;                    // events
    return true;
  };

  auto writeScopeSpansBody = [&](pb_ostream_t* s) -> bool {
    // scope (field 1): InstrumentationScope.name + version
    auto writeScopeBody = [](pb_ostream_t* s) -> bool {
      return writeString(s, 1, tracerConfig().scopeName.c_str()) &&
             writeString(s, 2, tracerConfig().scopeVersion.c_str());
    };
    pb_ostream_t sz = PB_OSTREAM_SIZING; writeScopeBody(&sz);
    if (!writeTag(s, 1, PB_WT_STRING)) return false;
    if (!pb_encode_varint(s, sz.bytes_written)) return false;
    if (!writeScopeBody(s)) return false;
    // spans (field 2)
    pb_ostream_t sz2 = PB_OSTREAM_SIZING; writeSpanBody(&sz2);
    if (!writeTag(s, 2, PB_WT_STRING)) return false;
    if (!pb_encode_varint(s, sz2.bytes_written)) return false;
    return writeSpanBody(s);
  };

  auto writeResourceSpansBody = [&](pb_ostream_t* s) -> bool {
    // resource (field 1): three KeyValue entries
    auto writeResBody = [&](pb_ostream_t* s) -> bool {
      return writeKVStr(s, 1, "service.name",        svcName.c_str())  &&
             writeKVStr(s, 1, "service.instance.id", svcInst.c_str())  &&
             writeKVStr(s, 1, "host.name",           hostName.c_str());
    };
    pb_ostream_t sz = PB_OSTREAM_SIZING; writeResBody(&sz);
    if (!writeTag(s, 1, PB_WT_STRING)) return false;
    if (!pb_encode_varint(s, sz.bytes_written)) return false;
    if (!writeResBody(s)) return false;
    // scope_spans (field 2)
    pb_ostream_t sz2 = PB_OSTREAM_SIZING; writeScopeSpansBody(&sz2);
    if (!writeTag(s, 2, PB_WT_STRING)) return false;
    if (!pb_encode_varint(s, sz2.bytes_written)) return false;
    return writeScopeSpansBody(s);
  };

  // TracesData { resource_spans = 1 }
  pb_ostream_t sz = PB_OSTREAM_SIZING;
  writeResourceSpansBody(&sz);

  uint8_t buf[OTEL_PROTO_BUFFER_SIZE];
  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  if (writeTag(&stream, 1, PB_WT_STRING) &&
      pb_encode_varint(&stream, sz.bytes_written) &&
      writeResourceSpansBody(&stream)) {
    OTelSender::sendProto("/v1/traces", buf, stream.bytes_written);
  }
}

} // namespace Proto
} // namespace OTel

#endif // OTEL_EXPORTER_OTLP_PROTOCOL_HTTP_PROTOBUF

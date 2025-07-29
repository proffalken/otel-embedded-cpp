#ifndef OTEL_TRACER_H
#define OTEL_TRACER_H

#include <ArduinoJson.h>
#include "OtelDefaults.h"
#include "OtelSender.h"

namespace OTel {

class Span {
public:
  String traceId;
  String spanId;
  unsigned long start;
  String name;

  Span(const String& n, const String& tId, const String& sId)
      : name(n), traceId(tId), spanId(sId), start(millis()) {}

  void end();
};

class Tracer {
public:
  static Span startSpan(const String& name, const String& parentId = "") {
    String traceId = randomHex(32);
    String spanId = randomHex(16);
    return Span(name, traceId, spanId);
  }

private:
  static String randomHex(int length) {
    const char* chars = "0123456789abcdef";
    String output;
    for (int i = 0; i < length; i++) {
      output += chars[random(16)];
    }
    return output;
  }
};

} // namespace OTel

#endif


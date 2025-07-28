#ifndef OTEL_TRACER_H
#define OTEL_TRACER_H

#include <ArduinoJson.h>
#include <OtelDefaults.h>
#include <OtelSender.h>

namespace OTel {

class Span {
public:
  Span(String name);
  void end();

private:
  String name;
  uint64_t startTime;
};

class Tracer : public OtelSender {
public:
  static void begin(const String &serviceName, const String &host,
                    const String &version = "");
  static Span startSpan(const char *name);
  static void endSpan(Span &span);
};

} // namespace OTel

#endif


#ifndef OTEL_SENDER_H
#define OTEL_SENDER_H

#include <ArduinoJson.h>

#ifndef OTEL_COLLECTOR_HOST
#define OTEL_COLLECTOR_HOST "http://localhost:4318"
#endif

namespace OTelSender {
  void sendJson(const char* path, JsonDocument& doc);
}

#endif


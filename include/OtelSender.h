#ifndef OTEL_SENDER_H
#define OTEL_SENDER_H

#include <ArduinoJson.h>

#ifndef OTEL_COLLECTOR_HOST
#define OTEL_COLLECTOR_HOST "http://192.168.8.10:4318"
#endif

namespace OTel {

class OTelSender {
public:
  static void sendJson(const char* path, JsonDocument& doc);
};

} // namespace OTel

#endif


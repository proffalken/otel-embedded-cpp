#pragma once

#include <ArduinoJson.h>

namespace OTel {
  class OtelSender {
  public:
    static void sendJson(const char *path, JsonDocument &doc);
  };
}


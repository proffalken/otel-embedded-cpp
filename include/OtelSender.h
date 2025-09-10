#ifndef OTEL_SENDER_H
#define OTEL_SENDER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <queue>

#ifndef OTEL_COLLECTOR_HOST
#define OTEL_COLLECTOR_HOST "http://192.168.8.10:4318"
#endif

namespace OTel {

/**
 * Non-blocking sender for OTLP/HTTP JSON.
 *  - ESP32: FreeRTOS task on core 1
 *  - RP2040: worker on core 1 (pico/multicore) + pico mutex_t
 *  - ESP8266: cooperative pump + short IRQ-off critical sections
 */
class OTelSender {
public:
  static void begin();
  static void sendJson(const char* path, JsonDocument& doc);
  static size_t pending();

private:
  struct Item {
    String path;
    String payload;
  };

  static void enqueue_(Item&& it);
  static bool dequeue_(Item& out);

  static void startWorker_();
  static void workerLoop_();
  static void doHttpPost_(const Item& it);

  static std::queue<Item> q_;

  // --- Platform-specific locking ---------------------------------
#if defined(ARDUINO_ARCH_ESP32)
  static SemaphoreHandle_t q_mtx_;
#elif defined(ARDUINO_ARCH_RP2040)
  // pico-sdk mutex (multi-core safe)
  // (include only in .cpp to avoid leaking headers here)
  struct rp2040_mutex_wrapper;
  static rp2040_mutex_wrapper* q_mtx_;
#elif defined(ESP8266)
  // no dedicated lock object; we use noInterrupts()/interrupts()
#endif
  // ---------------------------------------------------------------

  static volatile bool started_;

#if defined(ARDUINO_ARCH_ESP32)
  static TaskHandle_t task_;
#elif defined(ARDUINO_ARCH_RP2040)
  static bool core1_launched_;
#elif defined(ESP8266)
  static volatile bool pump_scheduled_;
  static void pumpOnce_();
#endif
};

} // namespace OTel

#endif


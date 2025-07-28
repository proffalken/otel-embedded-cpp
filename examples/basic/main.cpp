#include <Arduino.h>
#include <OtelEmbeddedCpp.h>

OTel::Gauge tickGauge("loop.tick");
OTel::Counter eventCounter("loop.event");

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialise OpenTelemetry logging with service metadata
  OTel::Logger::begin(OTEL_SERVICE_NAME, OTEL_SERVICE_NAMESPACE, OTEL_SERVICE_VERSION);

  // Log startup
  OTel::Logger::logInfo("Startup complete");

  // Optional: start a trace
  auto setupSpan = OTel::Tracer::startSpan("setup_initialisation");
  delay(100); // simulate some setup task
  OTel::Tracer::endSpan(setupSpan);
}

void loop() {
  static int ticks = 0;

  ticks++;
  tickGauge.set(ticks);
  eventCounter.inc();

  if (ticks % 10 == 0) {
    OTel::Logger::logInfo("10 ticks passed");

    auto span = OTel::Tracer::startSpan("ten_tick_block");
    delay(50); // simulate some work
    OTel::Tracer::endSpan(span);
  }

  delay(1000);
}


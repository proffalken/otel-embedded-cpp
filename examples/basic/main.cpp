#include <Arduino.h>
#include <OtelEmbeddedCpp.h>

// Setup a few components
OTel::Gauge loopCounter("loop.counter");
OTel::Counter sensorReadings("sensor.reads");
OTel::Histogram readDurations("sensor.duration", {10, 50, 100, 500}); // ms

int counter = 0;

void setup() {
  Serial.begin(115200);
  delay(500); // Let Serial settle

  // Set collector from compile-time define
  OTel::Logger::begin(OTEL_SERVICE_NAME, OTEL_COLLECTOR_HOST, OTEL_SERVICE_VERSION, OTEL_SERVICE_INSTANCE);
  OTel::Tracer::setResource(OTel::getDefaultResource());
  loopCounter.setResource(OTel::getDefaultResource());
  sensorReadings.setResource(OTel::getDefaultResource());
  readDurations.setResource(OTel::getDefaultResource());

  OTel::Logger::logInfo("Startup complete.");
}

void loop() {
  uint64_t spanStart = OTel::Tracer::startSpan("loop");

  // === Simulated work ===
  unsigned long start = millis();

  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

  loopCounter.set(counter);
  sensorReadings.inc();

  delay(random(10, 100)); // simulate variable task duration

  unsigned long end = millis();
  readDurations.record(end - start);

  OTel::Tracer::endSpan(spanStart);

  counter++;
  delay(2000); // Run every 2s
}


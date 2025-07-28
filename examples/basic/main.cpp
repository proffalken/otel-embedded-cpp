#include <Arduino.h>
#include <OtelEmbeddedCpp.h>

// Setup the loop id so we can increment it
int loop_id = 0;

// Create the gauge
OTel::Gauge loop_id("loop.id");

void setup(){
  OTel::Logger::begin("storper_bot", "storper.local", "v0.0.1");
  auto pinSpan = OTel::Tracer::startSpan("setup_pins", mainSpan);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LEFT_A, OUTPUT);
  pinMode(LEFT_B, OUTPUT);
  pinMode(RIGHT_A, OUTPUT);
  pinMode(RIGHT_B, OUTPUT);
  OTel::Tracer::endSpan(pinSpan);
};

void loop(){
  OTel::Logger::logInfo("Setup started.");
  loop_id.set(loop_id, {{"test", "ci"}});
  loop_id++;
};

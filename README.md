# otel-embedded-cpp

**OpenTelemetry logging, tracing, and metrics for embedded C++ devices**

Supported platforms:
- **ESP32** (Arduino core)
- **RP2040 Pico W** (Arduino-pico core)
- **ESP8266** (Arduino core)

---

## Features

- **Real-time timestamps** via NTP synchronization (pool.ntp.org, time.nist.gov)
- **Structured logs** (info, error) in OpenTelemetry JSON format
- **Trace spans** (start/end) for performance and flow analysis
- **Gauge metrics** with optional labels
- **Lightweight**: header-only API, minimal dependencies
- **PlatformIO-ready**: install via Git or registry

---

## Installation

Add to your **platformio.ini**:

```ini
[env:myboard]
platform = espressif32      ; or espressif8266, raspberrypi
framework = arduino
board = esp32dev           ; or pico_w, esp8266

; Install from GitHub
lib_deps =
    https://github.com/your-username/otel-embedded-cpp.git

; Or (if published) from PlatformIO Registry
; lib_deps = your-username/otel-embedded-cpp @ ^1.0.0
```

PlatformIO will automatically fetch **ArduinoJson** (v7.x) as a dependency.

---

## Usage Example

```cpp
#include <OTelEmbeddedCpp.h>
#include <WiFi.h>           // or <ESP8266WiFi.h>

// Telemetry objects
OTel::Gauge current_temperature("system.temperature");

void setup() {
  // 1) Connect WiFi in your code
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  // 2) Initialize library (NTP sync happens here)
  OTel::Logger::begin(
    "my-service",           // service name
    "collector.local:4318", // OpenTelemetry Collector host:port
    "v1.0.0"                // service version
  );

  // 3) Log an informational message
  OTel::Logger::logInfo("Setup complete.");

  // 4) Start a trace span
  String span = OTel::Tracer::startSpan("loop_cycle");
  // ... your code here ...
  OTel::Tracer::endSpan(span);
}

void loop() {
  float temp_c = 0.5f;
  current_temperature.set(temp_c, {{"location","packing_bay"}, {"factory": "Oxford"});
  delay(1000);
}
```

---

## API Reference

### `Logger`
- `Logger::begin(serviceName, endpointHost, version)`
- `Logger::logInfo(message)`

### `Tracer`
- `String spanId = Tracer::startSpan(name, [parentId])`
- `Tracer::endSpan(spanId)`

### `Gauge`
- `Gauge gauge("metric.name")`
- `gauge.set(value, vector<KeyValue> labels)`

---

## Repository Structure

```
otel-embedded-cpp/
├── src/
│   └── OTelEmbeddedCpp.h
├── library.json
└── README.md
```

---

## License

MIT © Matthew Macdonald-Wallace


# otel-embedded-cpp

A C++ library for instrumenting constrained embedded devices with OpenTelemetry. Send traces, logs and metrics over OTLP/HTTP to your favourite back‑end (Grafana, Jaeger, Prometheus, etc.) with minimal footprint.

---

## 🌐 Supported Platforms

* **ESP8266** (Arduino)
* **ESP32** (Arduino)
* **Raspberry Pi Pico W (RP2040)** (Arduino)

All examples assume use of the Arduino framework under PlatformIO, and that you have already implemented the network connection stack along with any code needed to set the clock on your embedded device to the correct time.

The example code shows how to do this with the `time` library and NTP.

---

## 🚀 Installation with PlatformIO

1. **Add the library** to your `platformio.ini`, pointing at the latest `main` branch:

   ```ini
   [platformio]
   default_envs = esp32dev

   [env:esp32dev]
   platform    = espressif32
   board       = esp32dev
   framework   = arduino

   lib_deps =
     https://github.com/proffalken/otel-embedded-cpp.git#main
   ```

2. **Configure your build flags** (either hard‑coded or via `${sysenv.*}`):

   ```ini
   build_flags =
     -DOTEL_WIFI_SSID="${sysenv.OTEL_WIFI_SSID}"
     -DOTEL_WIFI_PASS="${sysenv.OTEL_WIFI_PASS}"
     -DOTEL_COLLECTOR_HOST="${sysenv.OTEL_COLLECTOR_HOST}"
     -DOTEL_COLLECTOR_PORT=${sysenv.OTEL_COLLECTOR_PORT}
     -DOTEL_SERVICE_NAME="${sysenv.OTEL_SERVICE_NAME}"
     -DOTEL_SERVICE_NAMESPACE="${sysenv.OTEL_SERVICE_NAMESPACE}"
     -DOTEL_SERVICE_VERSION="${sysenv.OTEL_SERVICE_VERSION}"
     -DOTEL_SERVICE_INSTANCE="${sysenv.OTEL_SERVICE_INSTANCE}"
     -DOTEL_DEPLOY_ENV="${sysenv.OTEL_DEPLOY_ENV}"
   ```

3. **(Optional)** Use a `.env` file and load it into your shell:

   ```dotenv
   OTEL_WIFI_SSID=default
   OTEL_WIFI_PASS=default
   OTEL_COLLECTOR_HOST=192.168.1.100
   OTEL_COLLECTOR_PORT=4318
   OTEL_SERVICE_NAME=demo_service
   OTEL_SERVICE_NAMESPACE=demo_namespace
   OTEL_SERVICE_VERSION=v1.0.0
   OTEL_SERVICE_INSTANCE=otel_embedded_cpp
   OTEL_DEPLOY_ENV=dev
   ```

   Then:

   ```bash
   export $(grep -v '^#' .env | xargs)
   pio run -e esp32dev
   ```

---

## 🔧 Quick‑Start Example

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "OtelDefaults.h"
#include "OtelSender.h"
#include "OtelLogger.h"
#include "OtelTracer.h"
#include "OtelMetrics.h"

static constexpr uint32_t HEARTBEAT_INTERVAL = 5000;

void setup() {
  Serial.begin(115200);

  // Seed PRNG (fresh trace IDs each boot)
  #if defined(ARDUINO_ARCH_ESP32)
    randomSeed(esp_random());
  #else
    randomSeed(micros());
  #endif

  // Connect to Wi‑Fi
  WiFi.begin(OTEL_WIFI_SSID, OTEL_WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }

  // Sync NTP
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 1609459200UL) { delay(500); }

  // Initialise Logger & Tracer
  OTel::Logger::begin(
    OTEL_SERVICE_NAME,
    OTEL_SERVICE_NAMESPACE,
    WiFi.localIP().toString(),    // optional collector info
    WiFi.macAddress(),            // host identifier
    OTEL_SERVICE_VERSION
  );
  OTel::Tracer::begin(
    OTEL_SERVICE_NAME,
    OTEL_SERVICE_NAMESPACE,
    OTEL_SERVICE_INSTANCE,
    OTEL_SERVICE_VERSION
  );
}

void loop() {
  // Heartbeat trace
  auto span = OTel::Tracer::startSpan("heartbeat");

  OTel::Logger::logInfo("Heartbeat event");
  static OTel::OTelGauge gauge("heartbeat.gauge", "1");
  gauge.set(1.0f);
  span.end();

  delay(HEARTBEAT_INTERVAL);
}
```

This will emit:

* **Traces** for each `startSpan("heartbeat")`
* **Logs** with `service.*` resource attributes
* **Metrics** via `OTelGauge`, `OTelCounter` and `OTelHistogram`

All data is sent over OTLP/HTTP to the configured collector.

---

## 🛠 Configuration Macros

Override defaults in `OtelDefaults.h` or via `-D` flags:

| Macro                    | Default            | Description                                     |
| ------------------------ | ------------------ | ----------------------------------------------- |
| `OTEL_WIFI_SSID`         | `"default"`        | Wi‑Fi SSID                                      |
| `OTEL_WIFI_PASS`         | `"default"`        | Wi‑Fi password                                  |
| `OTEL_COLLECTOR_HOST`    | `"http://…:4318"`  | OTLP HTTP endpoint                              |
| `OTEL_COLLECTOR_PORT`    | `4318`             | OTLP HTTP port                                  |
| `OTEL_SERVICE_NAME`      | `"demo_service"`   | Name of your service                            |
| `OTEL_SERVICE_NAMESPACE` | `"demo_namespace"` | Service namespace                               |
| `OTEL_SERVICE_VERSION`   | `"v1.0.0"`         | Semantic version                                |
| `OTEL_SERVICE_INSTANCE`  | `"instance-1"`     | Unique instance ID                              |
| `OTEL_DEPLOY_ENV`        | `"dev"`            | Deployment environment (e.g. `prod`, `staging`) |

---

## 🤝 Contributing

We welcome contributions of all kinds! To help us maintain a high standard:

1. **Fork** the repository and create a feature branch.
2. **Follow** the existing code style (header‑only, minimal macros, clear names).
3. **Document** any new APIs or changes in this README.
4. **Issue** a pull request against `refactor/headers` once your changes are ready.
5. **Respond** to review comments promptly.

Please open an issue for:

* Bugs or unexpected behaviour
* Requests for new features or platform support
* Documentation improvements

---

## 📄 License

This project is released under the [MIT License](./LICENSE).


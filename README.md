# otel-embedded-cpp

A C++ library for instrumenting constrained embedded devices with OpenTelemetry. Send traces, logs and metrics over OTLP/HTTP to your favourite back‑end (Grafana, Jaeger, Prometheus, etc.) with minimal footprint.

---

## 🌐 Supported Platforms

* **ESP8266** (Arduino)
* **ESP32** (Arduino)
* **Raspberry Pi Pico W (RP2040)** (Arduino)

All examples assume use of the Arduino framework under PlatformIO, and that you have already implemented the network connection stack along with any code needed to set the clock on your embedded device to the correct time.

**WE STRONGLY RECOMMEND YOU USE NTP TO SET THE INTERNAL CLOCK, ALONG WITH AN RTC MODULE IF AVAILABLE**

The example code shows how to do this with the `time` library and NTP.

### Concurrency and performance

Where supported (RP2040 and *some* ESP32 boards), the code to process and send the data is moved to the second core of the device.

This removes any blocking code and ensures that the HTTP POST call does not interfere with the main loop.

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

2. **Configure your build flags in platformio.ini** (either hard‑coded or via `${sysenv.*}`):

   ```ini
   build_flags =
     -DWIFI_SSID="${sysenv.WIFI_SSID}"
     -DWIFI_PASS="${sysenv.WIFI_PASS}"
     -DOTEL_EXPORTER_OTLP_ENDPOINT="\"${sysenv.OTEL_EXPORTER_OTLP_ENDPOINT}\""
     -DOTEL_EXPORTER_OTLP_HEADERS="\"${sysenv.OTEL_EXPORTER_OTLP_HEADERS}\""
     -DOTEL_SERVICE_NAME="${sysenv.OTEL_SERVICE_NAME}"
     -DOTEL_SERVICE_NAMESPACE="${sysenv.OTEL_SERVICE_NAMESPACE}"
     -DOTEL_SERVICE_VERSION="${sysenv.OTEL_SERVICE_VERSION}"
     -DOTEL_SERVICE_INSTANCE="${sysenv.OTEL_SERVICE_INSTANCE}"
     -DOTEL_DEPLOY_ENV="${sysenv.OTEL_DEPLOY_ENV}"
   ```

3. **(Optional)** Use a `.env` file and load it into your shell:

   ```dotenv
   WIFI_SSID=default
   WIFI_PASS=default
   OTEL_EXPORTER_OTLP_ENDPOINT=http://192.168.1.100:4318
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
#include <time.h>

#if defined(ESP32)
  #include <WiFi.h>
  #include <esp_system.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_RP2040)
  #include <WiFi.h>
#else
  #error "This example targets ESP32, ESP8266, or RP2040 (Pico W) with WiFi."
#endif

#include "OtelSender.h"
#include "OtelTracer.h"
#include "OtelLogger.h"
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

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }

  // Sync NTP
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 1609459200UL) { delay(500); }

  // Initialise tracer and metrics (scopeName, scopeVersion)
  OTel::Tracer::begin("otel-embedded", "1.0.0");
  OTel::Metrics::begin("otel-embedded", "1.0.0");

  // On RP2040, start the core-1 async send worker after Wi-Fi is ready
#if defined(ARDUINO_ARCH_RP2040)
  OTelSender::beginAsyncWorker();
#endif
}

void loop() {
  auto span = OTel::Tracer::startSpan("heartbeat");

  OTel::Logger::logInfo("Heartbeat event");

  OTel::Metrics::gauge("heartbeat.uptime_seconds",
                       static_cast<double>(millis() / 1000), "s");

  span.end();
  delay(HEARTBEAT_INTERVAL);
}
```

This will emit:

* **Traces** for each `startSpan("heartbeat")`
* **Logs** correlated to the active span via `traceId`/`spanId`
* **Metrics** as a gauge via `Metrics::gauge()`

All data is sent over OTLP/HTTP to the configured collector.

---

## 🛠 Configuration Macros

Set via `-D` flags in `platformio.ini` `build_flags`.

### Endpoint

| Macro                                      | Default                        | Description |
| ------------------------------------------ | ------------------------------ | ----------- |
| `OTEL_EXPORTER_OTLP_ENDPOINT`              | *(empty)*                      | Base URL for all signals; `/v1/traces`, `/v1/metrics`, `/v1/logs` are appended automatically. Follows the [OTel exporter spec](https://opentelemetry.io/docs/specs/otel/protocol/exporter/). Takes priority over `OTEL_COLLECTOR_BASE_URL`. |
| `OTEL_EXPORTER_OTLP_LOGS_ENDPOINT`         | *(empty)*                      | Per-signal endpoint override, used verbatim (no path appended). Overrides `OTEL_EXPORTER_OTLP_ENDPOINT` for logs. |
| `OTEL_EXPORTER_OTLP_TRACES_ENDPOINT`       | *(empty)*                      | Same, for traces. |
| `OTEL_EXPORTER_OTLP_METRICS_ENDPOINT`      | *(empty)*                      | Same, for metrics. |
| `OTEL_COLLECTOR_BASE_URL`                  | `http://192.168.8.50:4318`     | Legacy base URL fallback. Prefer `OTEL_EXPORTER_OTLP_ENDPOINT` for new setups. |

### Headers & Authentication

| Macro                                      | Default   | Description |
| ------------------------------------------ | --------- | ----------- |
| `OTEL_EXPORTER_OTLP_HEADERS`               | *(empty)* | Comma-separated `key=value` headers added to every request. Values containing commas must be percent-encoded. Example: `"dd-api-key=abc123"` |
| `OTEL_EXPORTER_OTLP_LOGS_HEADERS`          | *(empty)* | Per-signal header overrides, merged on top of `OTEL_EXPORTER_OTLP_HEADERS`. |
| `OTEL_EXPORTER_OTLP_TRACES_HEADERS`        | *(empty)* | Same, for traces. |
| `OTEL_EXPORTER_OTLP_METRICS_HEADERS`       | *(empty)* | Same, for metrics. |

### TLS

| Macro                | Default | Description |
| -------------------- | ------- | ----------- |
| `OTEL_TLS_INSECURE`  | `1`     | When `1`, HTTPS connections skip certificate validation. Set to `0` for strict validation (requires a CA cert — see `OtelSender.h`). |

### Service identity

| Macro                    | Default            | Description                                     |
| ------------------------ | ------------------ | ----------------------------------------------- |
| `OTEL_SERVICE_NAME`      | `"embedded-service"` | Name of your service                          |
| `OTEL_SERVICE_INSTANCE`  | chip ID            | Unique instance identifier                      |
| `OTEL_HOST_NAME`         | `"ESP-<chipid>"`   | Host name reported in resource attributes       |

### Send behaviour

| Macro                 | Default | Description |
| --------------------- | ------- | ----------- |
| `OTEL_SEND_ENABLE`    | `1`     | Set to `0` to compile out all network sends (useful for latency benchmarking). |
| `OTEL_WORKER_BURST`   | `8`     | Items dequeued and sent per worker loop iteration (RP2040). |
| `OTEL_WORKER_SLEEP_MS`| `0`     | Delay between worker iterations in ms. |
| `OTEL_QUEUE_CAPACITY` | `16`    | SPSC queue depth for the RP2040 core-1 sender. |
| `DEBUG`               | *(unset)* | Print verbose output including OTLP payloads to Serial. |


---

## 🤝 Contributing

We welcome contributions of all kinds! To help us maintain a high standard:

1. **Fork** the repository and create a feature branch.
2. **Follow** the existing code style (header‑only, minimal macros, clear names).
3. **Document** any new APIs or changes in this README.
4. **Issue** a pull request against the main repo once your changes are ready.

Please open an issue for:

* Bugs or unexpected behaviour
* Requests for new features or platform support
* Documentation improvements

---

## 📄 License

This project is released under the [MIT License](./LICENSE).


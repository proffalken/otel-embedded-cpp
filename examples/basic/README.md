# basic example

Minimal working sketch that sends a heartbeat trace, a correlated log, and a
gauge metric on a fixed interval.

## What it does

Every 5 seconds the main loop:

1. Opens a trace span called `heartbeat`
2. Emits an `INFO` log correlated to that span (the `traceId` and `spanId` are
   attached automatically)
3. Records a gauge metric (`heartbeat.uptime_seconds`)
4. Closes the span, which triggers the OTLP send

## Configuration

Set these via `-D` flags in your `platformio.ini` `build_flags`:

| Flag | Purpose |
|---|---|
| `WIFI_SSID` / `WIFI_PASS` | Wi-Fi credentials |
| `OTEL_EXPORTER_OTLP_ENDPOINT` | Base URL of your collector, e.g. `http://192.168.1.100:4318` |
| `OTEL_SERVICE_NAME` | Service name reported in all telemetry |

See the root [README](../../README.md) for the full list of configuration flags.

## Running

```ini
; platformio.ini
[env:esp32dev]
platform  = espressif32
board     = esp32dev
framework = arduino
lib_deps  = https://github.com/proffalken/otel-embedded-cpp.git#main
build_flags =
  -DWIFI_SSID="\"your-ssid\""
  -DWIFI_PASS="\"your-password\""
  -DOTEL_EXPORTER_OTLP_ENDPOINT="\"http://192.168.1.100:4318\""
  -DOTEL_SERVICE_NAME="\"my-device\""
```

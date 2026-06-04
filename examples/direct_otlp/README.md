# direct_otlp example

Demonstrates sending traces, metrics, and logs directly to a vendor OTLP/HTTP
endpoint (no intermediate collector required), and shows the full attribute and
tagging API across all three signals.

## What it does

Every 10 seconds the main loop:

1. Opens a trace span with typed attributes (string, int, double, bool)
2. Emits a correlated log with per-call labels merged with setup-time defaults
3. Adds a timestamped span event if the simulated reading crosses a threshold
4. Records a gauge metric and a delta-temporality sum, both with per-call and
   default labels
5. Closes the span

## Attributes and tags

| Where | API | Notes |
|---|---|---|
| Span | `span.setAttribute(key, value)` | String, int64, double, or bool |
| Span | `span.addEvent(name, attrs)` | Timestamped annotation within span |
| Metric (per-call) | last arg of `gauge()` / `sum()` | `{{"key", "val"}, ...}` |
| Metric (default) | `Metrics::setDefaultMetricLabel(k, v)` | Applied to every datapoint |
| Log (per-call) | last arg of `logInfo()` etc. | `{{"key", "val"}, ...}` |
| Log (default) | `Logger::setDefaultLabel(k, v)` | Applied to every log record |

## Configuration

All endpoint and authentication config lives in build flags — no credentials in
source.

```ini
; platformio.ini
build_flags =
  -DWIFI_SSID="\"your-ssid\""
  -DWIFI_PASS="\"your-password\""
  -DOTEL_EXPORTER_OTLP_ENDPOINT="\"https://your-collector\""
  -DOTEL_EXPORTER_OTLP_HEADERS="\"Authorization=Bearer your-token\""
```

### Datadog (US1)

```ini
build_flags =
  -DOTEL_EXPORTER_OTLP_ENDPOINT="\"https://otlp.datadoghq.com\""
  -DOTEL_EXPORTER_OTLP_HEADERS="\"dd-api-key=${sysenv.DD_API_KEY}\""
```

### Grafana Cloud

```ini
build_flags =
  -DOTEL_EXPORTER_OTLP_ENDPOINT="\"https://<instance>.grafana.net/otlp\""
  -DOTEL_EXPORTER_OTLP_HEADERS="\"Authorization=Basic <base64-token>\""
```

HTTPS is enabled automatically when the endpoint URL starts with `https://`.
See the root [README](../../README.md) for the full list of configuration flags.

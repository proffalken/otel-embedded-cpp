#pragma once

#include "OtelLogger.h"
#include "OtelTracer.h"
#include "OtelMetrics.h"

// === Default Configurable Attributes ===
#ifndef OTEL_SERVICE_NAME
#define OTEL_SERVICE_NAME "embedded-app"
#endif

#ifndef OTEL_SERVICE_NAMESPACE
#define OTEL_SERVICE_NAMESPACE ""
#endif

#ifndef OTEL_SERVICE_VERSION
#define OTEL_SERVICE_VERSION "0.1.0"
#endif

#ifndef OTEL_SERVICE_INSTANCE
#define OTEL_SERVICE_INSTANCE ""
#endif

#ifndef OTEL_DEPLOY_ENV
#define OTEL_DEPLOY_ENV "dev"
#endif

namespace OTel {
  // Re-export core components
  using Logger = OTelLogger;
  using Tracer = OTelTracer;
  using Gauge = OTelGauge;
  using Counter = OTelCounter;
  using Histogram = OTelHistogram;
  using ResourceConfig = OTelResourceConfig;

  // Shared utility to get default resource attributes
  inline ResourceConfig getDefaultResource() {
    return ResourceConfig(
      OTEL_SERVICE_NAME,
      OTEL_SERVICE_NAMESPACE,
      OTEL_SERVICE_VERSION,
      OTEL_SERVICE_INSTANCE,
      OTEL_DEPLOY_ENV
    );
  }
}


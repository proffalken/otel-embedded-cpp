#pragma once

#include "OtelDebug.h"

#include "OtelLogger.h"
#include "OtelTracer.h"
#include "OtelMetrics.h"

#ifndef OTEL_SERVICE_NAME
#define OTEL_SERVICE_NAME "embedded-app"
#endif

#ifndef OTEL_SERVICE_NAMESPACE
#define OTEL_SERVICE_NAMESPACE ""
#endif

#ifndef OTEL_SERVICE_VERSION
#define OTEL_SERVICE_VERSION "v0.0.1"
#endif

#ifndef OTEL_SERVICE_INSTANCE
#define OTEL_SERVICE_INSTANCE "device-001"
#endif

#ifndef OTEL_DEPLOY_ENV
#define OTEL_DEPLOY_ENV "dev"
#endif

namespace OTel {
  using Logger = OTelLogger;
  using Tracer = OTelTracer;
  using Gauge = OTelGauge;
  using Counter = OTelCounter;
  using Histogram = OTelHistogram;
  using ResourceConfig = OTelResourceConfig;

  inline OTelResourceConfig getDefaultResource() {
    return OTelResourceConfig(
      OTEL_SERVICE_NAME,
      OTEL_SERVICE_NAMESPACE,
      OTEL_SERVICE_VERSION,
      OTEL_SERVICE_INSTANCE,
      OTEL_DEPLOY_ENV
    );
  }
}


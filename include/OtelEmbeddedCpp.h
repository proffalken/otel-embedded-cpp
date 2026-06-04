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

// OTel::Logger, OTel::Tracer, OTel::Metrics are defined in their respective headers above.


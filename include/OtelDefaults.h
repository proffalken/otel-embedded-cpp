#pragma once
#include <ArduinoJson.h>

#ifndef OTEL_SERVICE_NAME
#define OTEL_SERVICE_NAME "default_service"
#endif

#ifndef OTEL_SERVICE_NAMESPACE
#define OTEL_SERVICE_NAMESPACE "default_namespace"
#endif

#ifndef OTEL_SERVICE_VERSION
#define OTEL_SERVICE_VERSION "v0.1.0"
#endif

#ifndef OTEL_SERVICE_INSTANCE
#define OTEL_SERVICE_INSTANCE "instance-001"
#endif

#ifndef OTEL_DEPLOY_ENV
#define OTEL_DEPLOY_ENV "development"
#endif

namespace OTel {

  struct OTelResourceConfig {
    String serviceName;
    String serviceNamespace;
    String serviceVersion;
    String serviceInstanceId;
    String deploymentEnv;

    OTelResourceConfig(
      const String& name,
      const String& ns,
      const String& ver,
      const String& instance,
      const String& env
    ) : serviceName(name),
        serviceNamespace(ns),
        serviceVersion(ver),
        serviceInstanceId(instance),
        deploymentEnv(env) {}

    void addResourceAttributes(JsonObject& resource) const {
      JsonArray attrs = resource.createNestedArray("attributes");
      serializeKeyValue(attrs, "service.name", serviceName);
      serializeKeyValue(attrs, "service.namespace", serviceNamespace);
      serializeKeyValue(attrs, "service.version", serviceVersion);
      serializeKeyValue(attrs, "service.instance.id", serviceInstanceId);
      serializeKeyValue(attrs, "deployment.environment", deploymentEnv);
    }

    void serializeKeyValue(JsonArray& arr, const String& key, const String& value) const {
      JsonObject attr = arr.add<JsonObject>();
      attr["key"] = key;
      JsonObject val = attr.createNestedObject("value");
      val["stringValue"] = value;
    }

    void setAttribute(const String& key, const String& value) {
      if (key == "service.name") serviceName = value;
      else if (key == "service.namespace") serviceNamespace = value;
      else if (key == "service.version") serviceVersion = value;
      else if (key == "service.instance.id") serviceInstanceId = value;
      else if (key == "deployment.environment") deploymentEnv = value;
    }

    void serializeAttributes(JsonArray& arr) const {
      serializeKeyValue(arr, "service.name", serviceName);
      serializeKeyValue(arr, "service.namespace", serviceNamespace);
      serializeKeyValue(arr, "service.version", serviceVersion);
      serializeKeyValue(arr, "service.instance.id", serviceInstanceId);
      serializeKeyValue(arr, "deployment.environment", deploymentEnv);
    }
  };

  inline OTelResourceConfig getDefaultResource() {
    return OTelResourceConfig(
      OTEL_SERVICE_NAME,
      OTEL_SERVICE_NAMESPACE,
      OTEL_SERVICE_VERSION,
      OTEL_SERVICE_INSTANCE,
      OTEL_DEPLOY_ENV
    );
  }

}  // namespace OTel


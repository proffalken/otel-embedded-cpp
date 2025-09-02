// main.cpp — ESP32 / ESP8266 / Raspberry Pi Pico W (Arduino-Pico) compatible
#include <Arduino.h>

#if defined(ESP32)
  #include <WiFi.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_RP2040)
  // Earle Philhower’s Arduino-Pico core exposes a WiFi.h for Pico W
  #include <WiFi.h>
#else
  #error "This example targets ESP32, ESP8266, or RP2040 (Pico W) with WiFi."
#endif

#include <functional>
#include <map>
#include <vector>
#include <ArduinoJson.h>

#include "OtelTracer.h"     // Requires <functional> and TraceContext defined before ExtractedContext
#include "OtelLogger.h"
#include "OtelDebug.h"
#ifdef __has_include
  #if __has_include("OtelMetrics.h")
    #include "OtelMetrics.h"
    #define HAVE_OTEL_METRICS 1
  #endif
#endif

// ---------- WiFi setup (use build_flags -DSSID=\"...\" -DPASS=\"...\") ----------
#ifndef SSID
  #define SSID "WIFI"
#endif
#ifndef PASS
  #define PASS "SSID"
#endif

#ifndef OTEL_SERVICE_NAME
  #define OTEL_SERVICE_NAME "embedded-device"
#endif
#ifndef OTEL_SERVICE_VERSION
  #define OTEL_SERVICE_VERSION "0.1.0"
#endif

static WiFiServer server(80);
static volatile uint32_t g_request_count = 0;

// ---------- Helpers ----------
static inline String toLowerCopy(const String& s) {
  String out = s;
  out.toLowerCase();
  return out;
}

static inline void sendJson(WiFiClient& client, int code, const String& json) {
  client.printf("HTTP/1.1 %d\r\n", code);
  client.println(F("Content-Type: application/json"));
  client.printf("Content-Length: %d\r\n", json.length());
  client.println(F("Connection: close\r\n"));
  client.print(json);
}

static bool readHttpRequest(WiFiClient& client,
                            String& method,
                            String& uri,
                            std::map<String,String>& headers,
                            String& body) {
  client.setTimeout(5000);

  // 1) Request line
  String line = client.readStringUntil('\n');
  if (!line.length()) return false;
  line.trim(); // remove \r

  int sp1 = line.indexOf(' ');
  int sp2 = sp1 >= 0 ? line.indexOf(' ', sp1 + 1) : -1;
  if (sp1 < 0 || sp2 < 0) return false;
  method = line.substring(0, sp1);
  uri    = line.substring(sp1 + 1, sp2);

  // 2) Headers until blank line
  int contentLength = 0;
  while (true) {
    String h = client.readStringUntil('\n');
    if (!h.length()) break;
    h.trim();
    if (h.length() == 0) break; // end of headers

    int colon = h.indexOf(':');
    if (colon > 0) {
      String key = h.substring(0, colon);
      String val = h.substring(colon + 1);
      val.trim();
      // store as lower-case key for case-insensitive lookup
      headers[toLowerCopy(key)] = val;
    }
  }

  // 3) Body (if Content-Length present)
  auto it = headers.find(F("content-length"));
  if (it != headers.end()) {
    contentLength = it->second.toInt();
  }
  if (contentLength > 0) {
    body.reserve(contentLength);
    while ((int)body.length() < contentLength && client.connected()) {
      int c = client.read();
      if (c < 0) break;
      body += (char)c;
    }
  } else {
    body = "";
  }
  return true;
}

// ---------- Request handler ----------
static void handleRequest(const String& method,
                          const String& uri,
                          const std::map<String,String>& headers,
                          const String& body,
                          WiFiClient& client) {
  g_request_count++;

#ifdef HAVE_OTEL_METRICS
  // simple metric for visibility
  OTel::Metrics::gauge("http.requests.total", (double)g_request_count, "1", {
    {"method", method},
    {"uri", uri}
  });
#endif

  // Build a KeyValuePairs adapter for header extraction
  OTel::KeyValuePairs kv;
  kv.get = [&](const String& key) -> String {
    String k = toLowerCopy(key);
    auto it = headers.find(k);
    if (it == headers.end()) return String();
    return it->second;
  };

  // 1) Try to extract from headers
  OTel::Logger::logInfo("Extracting content from headers");
  OTel::ExtractedContext ext = OTel::Propagators::extract(kv);

  // 2) If not found, try body (JSON)
  if (!ext.valid() && body.length()) {
    OTel::Logger::logInfo("Couldn't find context in headers, trying body instead");
    ext = OTel::Propagators::extractFromJson(body);
  }

  // Install remote parent for the duration of this handler (no-op if invalid)
  {
    OTel::RemoteParentScope parent(ext.ctx);

    // Server span
    auto serverSpan = OTel::Tracer::startSpan("http.request");
    // It’s fine to enrich the span via attributes later when the Tracer class supports it.
    // For now, we just demonstrate a nested child-span doing "work".

    {
      auto childSpan = OTel::Tracer::startSpan("do_work");
      OTel::Logger::logInfo("Doing some work");
      // Simulate some work
      delay(10);
      // childSpan ends on scope exit
      childSpan.end();
    }

    // Prepare response
    JsonDocument doc;
    doc["ok"] = true;
    doc["method"] = method;
    doc["uri"] = uri;
    if (ext.valid()) {
      doc["parent"]["trace_id"] = ext.ctx.traceId;
      doc["parent"]["span_id"]  = ext.ctx.spanId;
      doc["parent"]["sampled"]  = ext.sampled;
      if (ext.tracestate.length()) doc["parent"]["tracestate"] = ext.tracestate;
    } else {
      doc["parent"] = nullptr;
    }

    String out;
    serializeJson(doc, out);
    sendJson(client, 200, out);

    serverSpan.end();

    // serverSpan ends on scope exit
  }
}

// ---------- Arduino setup/loop ----------
void setup() {
  Serial.begin(115200);
  delay(200);


  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);
  Serial.printf("Connecting to %s", SSID);
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi failed to connect. Continuing anyway.");
  }
  // 2) Sync NTP (configTime works on Pico W, ESP32 & ESP8266 in Arduino land)
  //    We're polling until we get something > Jan 1 2020 (1609459200).
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  DBG_PRINT("Waiting NTP sync");
  while (time(nullptr) < 1609459200UL) {
    DBG_PRINT('.');
    delay(500);
  }
  DBG_PRINTLN();

  // 3) (Optional) print the calendar time
  {
    time_t now = time(nullptr);
    struct tm tm;
    gmtime_r(&now, &tm);
    Serial.printf("NTP time: %s", asctime(&tm));
  }

    // Set resource attributes once (service/host/instance/etc.)
  auto &res = OTel::defaultResource();
  res.set("service.name",        "guidance-sytem");
  res.set("service.instance.id", "818b08");
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP8266)
  res.set("host.name", WiFi.getHostname());
#else
  res.set("host.name", "rp2040");
#endif
    OTel::Metrics::begin("otel-embedded", "0.1.0");
    OTel::Metrics::setDefaultMetricLabel("device.role", "webserver");
  //
  // Optional: default labels for all log lines from this process
  OTel::Logger::setDefaultLabel("device.role", "webserver");

  OTel::Logger::logInfo("Logger initialised");

  server.begin();
  Serial.println("HTTP server started on port 80.");
}

void loop() {
#if defined(ARDUINO_ARCH_RP2040)
  WiFiClient client = server.accept(); // non-deprecated on Pico W
#else
  WiFiClient client = server.available();
#endif
  if (!client) {
    delay(1);
    return;
  }

  String method, uri, body;
  std::map<String,String> headers;
  if (!readHttpRequest(client, method, uri, headers, body)) {
    OTel::Logger::logError("Unable to read request.");
    sendJson(client, 400, F("{\"error\":\"bad_request\"}"));
    client.stop();
    return;
  }

  // Route: only "/" for this demo
  if (uri == "/") {
    OTel::Logger::logInfo("Request received for root handler");
    Serial.println("Accepted Request");
    handleRequest(method, uri, headers, body, client);
  } else {
    OTel::Logger::logError("Route not found");
    sendJson(client, 404, F("{\"error\":\"not_found\"}"));
  }

  client.stop();
}


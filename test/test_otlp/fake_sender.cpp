#include "fake_sender.h"
#include "OtelSender.h"
#include <ArduinoJson.h>
#include <atomic>

// ── Captured state ────────────────────────────────────────────────────────────
namespace FakeSender {
  std::string          lastPath;
  std::string          lastJson;
  std::vector<uint8_t> lastProto;

  void reset() {
    lastPath.clear();
    lastJson.clear();
    lastProto.clear();
  }
}

// ── OTelSender static member definitions ─────────────────────────────────────
// These must live in exactly one translation unit; the real OtelSender.cpp is
// excluded from the native build, so we define them here instead.
OTelQueuedItem        OTelSender::q_[OTelSender::QCAP];
std::atomic<size_t>   OTelSender::head_{0};
std::atomic<size_t>   OTelSender::tail_{0};
std::atomic<uint32_t> OTelSender::drops_{0};
std::atomic<bool>     OTelSender::worker_started_{false};

// ── Public API stubs ──────────────────────────────────────────────────────────

void OTelSender::sendJson(const char* path, JsonDocument& doc) {
  FakeSender::lastPath = path;
  serializeJson(doc, FakeSender::lastJson);
}

void OTelSender::sendProto(const char* path, const uint8_t* buf, size_t len) {
  FakeSender::lastPath = path;
  FakeSender::lastProto.assign(buf, buf + len);
}

void     OTelSender::beginAsyncWorker() {}
uint32_t OTelSender::droppedCount()     { return 0; }
bool     OTelSender::queueIsHealthy()   { return true; }

// ── Private helper stubs ──────────────────────────────────────────────────────
bool   OTelSender::enqueue_(const char*, const char*, std::vector<uint8_t>&&) { return true; }
bool   OTelSender::dequeue_(OTelQueuedItem&)                    { return false; }
void   OTelSender::pumpOnce_()                                   {}
void   OTelSender::workerLoop_()                                 {}
void   OTelSender::launchWorkerOnce_()                           {}
String OTelSender::fullUrl_(const char* path)                    { return String(path); }

#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Test double for OTelSender.
// Call reset() in setUp(); then inspect lastPath / lastJson / lastProto
// after exercising library code to assert on the emitted payload.
namespace FakeSender {
  extern std::string        lastPath;
  extern std::string        lastJson;
  extern std::vector<uint8_t> lastProto;

  void reset();
}

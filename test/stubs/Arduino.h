#pragma once
// Minimal Arduino shim for native (Linux/macOS) unit tests.
// Provides just enough of the Arduino API surface that otel-embedded-cpp
// headers (and ArduinoJson's Arduino-mode polyfills) compile on a host
// toolchain without modification.

#ifndef ARDUINO
#define ARDUINO 100
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <cstdio>
#include <cstdarg>

// ── pgmspace (AVR flash memory — identity on native) ─────────────────────────
#define pgm_read_byte(addr)  (*((const uint8_t  *)(addr)))
#define pgm_read_word(addr)  (*((const uint16_t *)(addr)))
#define pgm_read_ptr(addr)   (*((const void* const*)(addr)))
#define PROGMEM
#define PGM_P                const char*
#define PSTR(s)              (s)

// ── Flash string helper ───────────────────────────────────────────────────────
// ArduinoJson checks for __FlashStringHelper in several headers.
// On native it is just an opaque type; cast via F().
struct __FlashStringHelper {};
#define F(x) (reinterpret_cast<const __FlashStringHelper*>(x))

// ── Print / Printable / Stream ────────────────────────────────────────────────
// ArduinoJson uses these as base-class constraints in its adapters.
class Print {
public:
  virtual size_t write(uint8_t)               { return 0; }
  virtual size_t write(const uint8_t*, size_t n) { return n; }
  size_t print(const char* s)   { return write((const uint8_t*)s, strlen(s)); }
  size_t println(const char* s = "") { return print(s) + write((uint8_t)'\n'); }
  virtual ~Print() = default;
};

class Printable {
public:
  virtual size_t printTo(Print&) const = 0;
  virtual ~Printable() = default;
};

class Stream : public Print {
public:
  virtual int    available()                         { return 0; }
  virtual int    read()                              { return -1; }
  virtual int    peek()                              { return -1; }
  virtual size_t readBytes(char*, size_t)            { return 0; }
  virtual size_t readBytes(uint8_t* b, size_t n)     { return readBytes((char*)b, n); }
};

// ── String ────────────────────────────────────────────────────────────────────
// std::string subclass that adds the Arduino-compatible constructors and
// helpers used throughout the library (c_str, length, indexOf, etc.).
class String : public std::string {
public:
  String() = default;
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  String(char c) : std::string(1, c) {}
  explicit String(int v)           : std::string(std::to_string(v)) {}
  explicit String(long v)          : std::string(std::to_string(v)) {}
  explicit String(unsigned int v)  : std::string(std::to_string(v)) {}
  explicit String(unsigned long v) : std::string(std::to_string(v)) {}
  explicit String(float v)         : std::string(std::to_string(v)) {}
  explicit String(double v)        : std::string(std::to_string(v)) {}

  unsigned int length() const { return (unsigned int)size(); }

  int indexOf(char c, unsigned int from = 0) const {
    auto p = find(c, from);
    return p == npos ? -1 : (int)p;
  }
  int indexOf(const String& s, unsigned int from = 0) const {
    auto p = find(s, from);
    return p == npos ? -1 : (int)p;
  }
  String substring(unsigned int from, unsigned int to = 0) const {
    return to ? String(substr(from, to - from)) : String(substr(from));
  }
  int   toInt()   const { if (empty()) return 0;   try { return std::stoi(*this); }  catch (...) { return 0; } }
  float toFloat() const { if (empty()) return 0.f; try { return std::stof(*this); } catch (...) { return 0.f; } }

  String& operator+=(const char*   s) { std::string::operator+=(s); return *this; }
  String& operator+=(const String& s) { std::string::operator+=(s); return *this; }
  String  operator+(const String& s) const { return String(std::string(*this) + std::string(s)); }
  String  operator+(const char*   s) const { return String(std::string(*this) + s); }
  bool    operator==(const char*   s) const { return compare(s) == 0; }
  bool    operator!=(const char*   s) const { return compare(s) != 0; }

  bool concat(const char* s)   { if (s) append(s); return true; }
  bool concat(const String& s) { append(s); return true; }
};

inline String operator+(const char* a, const String& b) {
  return String(std::string(a) + std::string(b));
}

// ── Serial stub ───────────────────────────────────────────────────────────────
struct HardwareSerial {
  void begin(unsigned long) {}
  template<typename T>        size_t print(T)        { return 0; }
  template<typename T>        size_t println(T)      { return 0; }
  template<typename T, int U> size_t print(T, int)   { return 0; }
  template<typename T, int U> size_t println(T, int) { return 0; }
  size_t println()                                    { return 0; }
  void   printf(const char*, ...)                     {}
};
static HardwareSerial Serial;

// ── Timing stubs ──────────────────────────────────────────────────────────────
static inline unsigned long millis()        { return 0; }
static inline unsigned long micros()        { return 0; }
static inline void delay(unsigned long)     {}

// ── PRNG ──────────────────────────────────────────────────────────────────────
static inline void randomSeed(unsigned long seed) { srand((unsigned int)seed); }
static inline long random(long hi)                { return (long)(rand() % hi); }
static inline long random(long lo, long hi)       { return lo + (long)(rand() % (hi - lo)); }

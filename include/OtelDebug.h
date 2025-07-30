#pragma once

// When you compile with -DDEBUG (or add DEBUG to your build flags),
// all the DBG_* macros map to Serial; otherwise they become no-ops.
#ifdef DEBUG
  #define DBG_PRINT(...)    Serial.print(__VA_ARGS__)
  #define DBG_PRINTLN(...)  Serial.println(__VA_ARGS__)
#else
  #define DBG_PRINT(...)    (void)0
  #define DBG_PRINTLN(...)  (void)0
#endif


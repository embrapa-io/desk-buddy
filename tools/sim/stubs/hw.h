// Tela, toque, SPI e NVS de mentira.
#pragma once
#include "Arduino.h"

#define TFT_BLACK 0
#define VSPI 3

extern uint16_t simFb[320 * 240];

struct TFT_eSPI {
  int wx = 0, wy = 0, ww = 0, wh = 0;
  void init() {}
  void setRotation(int) {}
  void fillScreen(int) {}
  void startWrite() {}
  void endWrite() {}
  void setAddrWindow(int x, int y, int w, int h) { wx = x; wy = y; ww = w; wh = h; }
  void pushColors(uint16_t* d, uint32_t n, bool) {
    for (uint32_t i = 0; i < n; i++) {
      int x = wx + (int)(i % ww), y = wy + (int)(i / ww);
      if (x >= 0 && x < 320 && y >= 0 && y < 240) simFb[y * 320 + x] = d[i];
    }
  }
};

struct SPIClass { SPIClass(int) {} void begin(int, int, int, int) {} };

struct TS_Point { int x, y, z; };
struct XPT2046_Touchscreen {
  XPT2046_Touchscreen(int, int) {}
  void begin(SPIClass&) {}
  void setRotation(int) {}
  bool touched();
  TS_Point getPoint();
};

struct Preferences {
  void begin(const char*, bool) {}
  void end() {}
  String getString(const char* k, const char* def) {
    if (!std::strcmp(k, "ssid")) return String("Rede-Exemplo-2G");
    if (!std::strcmp(k, "pass")) return String("senha-exemplo");
    return String(def);
  }
  uint32_t getUInt(const char*, uint32_t def) { return def; }
  uint8_t getUChar(const char*, uint8_t def) { return def; }
  bool getBool(const char*, bool def) { return def; }
  void putString(const char*, const String&) {}
  void putUInt(const char*, uint32_t) {}
  void putUChar(const char*, uint8_t) {}
  void putBool(const char*, bool) {}
};

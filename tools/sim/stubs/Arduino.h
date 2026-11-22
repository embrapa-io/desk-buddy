// Simulador do Desk Buddy: o mínimo do Arduino para compilar o src/main.cpp no computador.
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
unsigned long millis(void);
void delay(unsigned long ms);
#ifdef __cplusplus
}
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
using std::min;
using std::max;

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
long map(long x, long in_min, long in_max, long out_min, long out_max);
void pinMode(int pin, int mode);
void digitalWrite(int pin, int v);
int analogRead(int pin);
void ledcAttach(int pin, int freq, int bits);
void ledcWrite(int pin, int duty);

class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& c) : s(c) {}
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
  String operator+(const String& o) const { return String(s + o.s); }
  String operator+(const char* o) const { return String(s + o); }
  bool operator==(const char* o) const { return s == o; }
};

struct SerialStub {
  void begin(int) {}
  void println(const char* m) { std::printf("%8lu  %s\n", millis(), m); }
  void printf(const char* fmt, ...) {
    std::printf("%8lu  ", millis());
    va_list ap; va_start(ap, fmt); std::vprintf(fmt, ap); va_end(ap);
  }
};
extern SerialStub Serial;

struct EspStub { void restart(); };
extern EspStub ESP;
#endif

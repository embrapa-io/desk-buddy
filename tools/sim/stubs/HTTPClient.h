#pragma once
#include "Arduino.h"
#include <fstream>

struct WiFiClientSecure { void setInsecure() {} };

// O simulador escolhe o que o «servidor» devolve: código HTTP e arquivo JSON.
extern int simHttpCode;
extern std::string simHttpFile;

struct HTTPClient {
  std::ifstream in;
  void setTimeout(int) {}
  void setConnectTimeout(int) {}
  void useHTTP10(bool = true) {}
  bool begin(WiFiClientSecure&, const String&) { return true; }
  void collectHeaders(const char**, int) {}
  int GET() { if (simHttpCode == 200) in.open(simHttpFile); return simHttpCode; }
  String header(const char*) { return String("Sun, 22 Nov 2026 14:45:21 GMT"); }
  std::istream& getStream() { return in; }
  void end() {}
};

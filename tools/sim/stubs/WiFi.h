#pragma once
#include "Arduino.h"

enum wl_status_t { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
enum { WIFI_OFF = 0, WIFI_STA = 1 };
enum arduino_event_id_t { ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 5, ARDUINO_EVENT_MAX };
typedef int wifi_err_reason_t;
union arduino_event_info_t { struct { uint8_t reason; } wifi_sta_disconnected; };
typedef void (*WifiEventFn)(arduino_event_id_t, arduino_event_info_t);

struct IPAddress { String toString() const { return String("192.168.15.42"); } };

// O simulador controla o rádio por duas variáveis: há AP ao alcance? o core reconecta sozinho?
struct WiFiStub {
  bool connected = false;      // associado e com IP
  bool apInRange = true;       // o begin() só conecta se houver AP
  int begins = 0;
  WifiEventFn onDrop = nullptr;

  wl_status_t status() { return connected ? WL_CONNECTED : WL_DISCONNECTED; }
  void begin(const char* ssid, const char* pass) {
    begins++;
    connected = apInRange;
    std::printf("%8lu  (sim) WiFi.begin #%d -> %s\n", millis(), begins, connected ? "conectou" : "sem AP");
  }
  bool disconnect(bool off = false, bool erase = false, unsigned long t = 100) { connected = false; return true; }
  void mode(int) {}
  void persistent(bool) {}
  bool setSleep(bool) { return true; }
  bool setAutoReconnect(bool) { return true; }
  int onEvent(WifiEventFn fn, arduino_event_id_t) { onDrop = fn; return 0; }
  const char* disconnectReasonName(wifi_err_reason_t) { return "SIM"; }
  IPAddress localIP() { return IPAddress(); }
  int RSSI() { return -61; }
  // queda provocada pelo simulador (sem reconexão do core, que é o caso que travava)
  void drop(uint8_t reason) {
    connected = false;
    if (onDrop) { arduino_event_info_t i; i.wifi_sta_disconnected.reason = reason; onDrop(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, i); }
  }
};
extern WiFiStub WiFi;

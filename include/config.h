// ─────────────────────────────────────────────────────────────
//  Configuração do Desk Buddy — edite antes de flashear
// ─────────────────────────────────────────────────────────────
#pragma once

// ---- WiFi (rede 2.4 GHz WPA2) ----
// Credenciais reais ficam em include/secrets.h (gitignored). Sem ele, usa placeholders.
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif
// Default de distribuição: EM BRANCO. Sem secrets.h, o aparelho sobe sem rede
// e abre a tela de Configuração no boot para o usuário informar o Wi-Fi.
#ifndef WIFI_SSID
#  define WIFI_SSID      ""
#  define WIFI_PASSWORD  ""
#endif

// ---- Fonte de dados: Gatus (status.embrapa.io) ----
// pageSize=1 → traz só a medição mais recente por endpoint (~14 KB, parseável no ESP32)
#define GATUS_HOST  "status.embrapa.io"
#define GATUS_PATH  "/api/v1/endpoints/statuses?page=1&pageSize=1"

// Intervalo de atualização (ms). O Gatus mede a cada ~5 min; 60 s é folgado.
#define REFRESH_MS  60000UL

// ---- Calibração do touch (XPT2046) — AJUSTAR na unidade real ----
// Valores brutos típicos do CYD; refine olhando o Serial Monitor (imprime x/y crus ao tocar).
#define TS_MINX 200
#define TS_MAXX 3700
#define TS_MINY 240
#define TS_MAXY 3800

#define FW_VERSION "0.26.6-alpha.1"

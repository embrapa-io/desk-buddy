// ─────────────────────────────────────────────────────────────
//  Configuração do Desk Buddy — edite antes de flashear
// ─────────────────────────────────────────────────────────────
#pragma once

// ---- WiFi (rede 2.4 GHz WPA2 simples) ----
#define WIFI_SSID      "SUA_REDE_2G"
#define WIFI_PASSWORD  "SUA_SENHA"

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

#define FW_VERSION "0.1-mockup"

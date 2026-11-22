// ─────────────────────────────────────────────────────────────
//  Desk Buddy — Painel de status da plataforma Embrapa I/O
//  CYD ESP32-2432S028R · Arduino + TFT_eSPI + LVGL 8 + ArduinoJson 7
//  Início com 6 cards (5 grupos do Gatus + Configurar), uma listagem por grupo
//  e reconexão automática do Wi-Fi.
//  Fonte: https://status.embrapa.io/api/v1/endpoints/statuses?page=1&pageSize=1
// ─────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include <time.h>
#include <sys/time.h>
#include <Preferences.h>
#include "config.h"

// Fontes Montserrat com acentos PT-BR (geradas via lv_font_conv → src/ui_font_*.c)
LV_FONT_DECLARE(ui_font_12);
LV_FONT_DECLARE(ui_font_14);
LV_FONT_DECLARE(ui_font_16);

// Logos (bitmaps LVGL gerados em src/img_*.c)
LV_IMG_DECLARE(img_logo_header);   // embrapa.io branca (header)
LV_IMG_DECLARE(img_io_color);      // embrapa.io colorida (splash)
LV_IMG_DECLARE(img_embrapa_color); // Embrapa colorida (splash)

// ícones extras (FontAwesome) embutidos nas ui_font_*
#define SYM_CLOCK "\xEF\x80\x97"   // relógio (U+F017) — uptime
#define SYM_GLOBE "\xEF\x82\xAC"   // globo (U+F0AC) — IP/rede
#define SYM_CHIP  "\xEF\x8B\x9B"   // microchip (U+F2DB) — firmware

// ---------------- Display / Touch ----------------
#define SCR_W 320
#define SCR_H 240
// Touch XPT2046 em barramento VSPI próprio (pinos do CYD)
#define T_CLK 25
#define T_MOSI 32
#define T_MISO 39
#define T_CS 33
#define T_IRQ 36
// LED RGB on-board (active-LOW) e LDR
#define LED_R 4
#define LED_G 16
#define LED_B 17
#define PIN_LDR 34

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(T_CS, T_IRQ);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t lvbuf[SCR_W * 20];

// ---------------- Paleta ----------------
#define COL_BG     lv_color_hex(0x0E1116)
#define COL_CARD   lv_color_hex(0x161B22)
#define COL_PRESS  lv_color_hex(0x263041)
#define COL_BTN    lv_color_hex(0x1B2330)
#define COL_BORDER lv_color_hex(0x2D333B)
#define COL_TXT    lv_color_hex(0xE6EDF3)
#define COL_MUTED  lv_color_hex(0x8B949E)
#define COL_UP     lv_color_hex(0x3FB950)
#define COL_WARN   lv_color_hex(0xD29922)
#define COL_DOWN   lv_color_hex(0xF85149)

// ---------------- Modelo de dados ----------------
// Os rótulos são os nomes dos grupos no Gatus (status.embrapa.io): a correspondência é pelo nome.
enum Grp { G_CLUSTERS = 0, G_HOSTS, G_SERVICES, G_PANELS, G_GPUS, G_COUNT };
const char* GRP_LABEL[G_COUNT] = { "Clusters", "Hosts", "Serviços", "Painéis", "GPU Servers" };
struct Endpoint { char name[40]; uint8_t grp; bool up; uint16_t ms; };
#define MAX_EP 96
Endpoint eps[MAX_EP];
int epCount = 0;
int grpUp[G_COUNT], grpTot[G_COUNT], downTotal = 0;
bool dataFromGatus = false;          // já houve uma leitura válida
unsigned long lastOkRefresh = 0;     // millis() da última leitura válida
unsigned long lastFetch = 0;         // millis() da última tentativa (0 = nenhuma)
uint8_t fetchFails = 0;              // leituras seguidas sem sucesso
uint8_t netFails = 0;                // idem, só as que ficaram sem resposta nenhuma (rede)
bool timeSet = false;                // hora já ajustada (via header Date do Gatus)

// ---------------- Referências da UI ----------------
#define P_HOME  0
#define P_COUNT (1 + G_COUNT)        // início + uma listagem por grupo (a de g é a página 1 + g)
lv_obj_t* page[P_COUNT];
// início
lv_obj_t* ovVal[G_COUNT];
lv_obj_t* ovDot[G_COUNT];
lv_obj_t* ovStat[G_COUNT];
lv_obj_t* ovAll;                     // veredito geral, no header do início
lv_obj_t* ovNet;                     // estado do Wi-Fi, no card Configurar
// listagens
lv_obj_t* listTitle[G_COUNT];
lv_obj_t* listBox[G_COUNT];
// configuração (dados do sistema)
lv_obj_t* sysNet;
lv_obj_t* sysSig;
lv_obj_t* sysUp;
lv_obj_t* sysRefr;
lv_obj_t* clocks[P_COUNT]; int nClocks = 0;   // relógio + data por tela (no header)
lv_obj_t* splash = nullptr;   // tela de boot (logos)

// ---------------- Configuração persistente (NVS) ----------------
Preferences prefs;
struct Settings { String ssid, pass, tz; uint32_t refreshMs; uint8_t bright; bool autob; } cfg;
const char* TZ_STR[4] = { "<-04>4", "<-03>3", "<-02>2", "<-05>5" };
const char* TZ_LBL    = "Campo Grande (UTC-4)\nBrasília (UTC-3)\nNoronha (UTC-2)\nAcre (UTC-5)";
const uint32_t REFRESH_OPT[3] = { 30000UL, 60000UL, 300000UL };
const char* REFRESH_LBL = "30 s\n60 s\n5 min";

int tzIdx()      { for (int i = 0; i < 4; i++) if (cfg.tz == TZ_STR[i]) return i; return 0; }
int refreshIdx() { for (int i = 0; i < 3; i++) if (cfg.refreshMs == REFRESH_OPT[i]) return i; return 1; }

void loadCfg() {
  prefs.begin("deskbuddy", true);
  cfg.ssid      = prefs.getString("ssid", WIFI_SSID);
  cfg.pass      = prefs.getString("pass", WIFI_PASSWORD);
  cfg.tz        = prefs.getString("tz", "<-04>4");
  cfg.refreshMs = prefs.getUInt("refresh", REFRESH_MS);
  cfg.bright    = prefs.getUChar("bright", 80);
  cfg.autob     = prefs.getBool("autob", false);
  prefs.end();
}
void saveCfg() {
  prefs.begin("deskbuddy", false);
  prefs.putString("ssid", cfg.ssid); prefs.putString("pass", cfg.pass);
  prefs.putString("tz", cfg.tz);     prefs.putUInt("refresh", cfg.refreshMs);
  prefs.putUChar("bright", cfg.bright); prefs.putBool("autob", cfg.autob);
  prefs.end();
}
#define BL_PIN 21
void applyBrightness(uint8_t pct) { pct = constrain(pct, 5, 100); ledcWrite(BL_PIN, map(pct, 0, 100, 0, 255)); }

// refs da tela de configuração
lv_obj_t* cfgScreen = nullptr;
lv_obj_t* kb = nullptr;
lv_obj_t* taSsid; lv_obj_t* taPass; lv_obj_t* ddTz; lv_obj_t* ddRefresh; lv_obj_t* sldBright; lv_obj_t* swAuto;

// =================================================================
//  Wi-Fi: conexão e reconexão
//  O aparelho fica ligado por semanas. Se o Wi-Fi cai e o core não volta sozinho
//  (roteador reiniciado, IP perdido, senha recusada uma vez), o vigia refaz a
//  conexão do mesmo jeito que o «Salvar» da Configuração fazia à mão.
// =================================================================
#define WIFI_RETRY_MS       15000UL    // sem rede há 15 s: tenta de novo; depois a cada 30 s e 60 s
#define WIFI_RADIO_EVERY    4          // a cada 4 tentativas, desliga e religa o rádio
#define WIFI_REBOOT_MS      900000UL   // 15 min sem rede: reinicia o aparelho (último recurso)
#define NET_FAILS_RECONNECT 3          // 3 leituras seguidas sem resposta com o Wi-Fi «conectado»: refaz a conexão
#define FETCH_RETRY_MS      10000UL    // nova leitura depois de uma falha; da 4ª falha seguida em diante, 30 s

bool netUp = false;                    // Wi-Fi conectado, com IP
bool wifiDown = false;                 // o vigia já viu a queda
unsigned long wifiDownSince = 0;
unsigned long wifiLastTry = 0;
uint8_t wifiTries = 0;
uint16_t wifiDrops = 0;                // quedas desde o boot
volatile uint8_t wifiReason = 0;       // motivo da última desconexão (wifi_err_reason_t)

// Roda na tarefa de eventos do Wi-Fi: só guarda o motivo, nada de LVGL aqui.
static void onWifiDisconnected(arduino_event_id_t event, arduino_event_info_t info) {
  wifiReason = info.wifi_sta_disconnected.reason;
}

static void wifiConnect(bool resetRadio) {
  if (cfg.ssid.length() == 0) return;
  if (resetRadio) {
    WiFi.disconnect(true);             // desliga o rádio
    delay(200);
    WiFi.mode(WIFI_STA);
  } else {
    WiFi.disconnect();
  }
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  wifiLastTry = millis();
}

static void wifiWatchdog() {
  if (cfg.ssid.length() == 0) return;
  unsigned long now = millis();
  if (netUp) { wifiDown = false; wifiTries = 0; return; }
  if (!wifiDown) { wifiDown = true; wifiDownSince = now; wifiLastTry = now; return; }
  // não reinicia com a Configuração aberta: alguém pode estar digitando a senha
  if (now - wifiDownSince > WIFI_REBOOT_MS && lv_obj_has_flag(cfgScreen, LV_OBJ_FLAG_HIDDEN)) {
    Serial.println("[WiFi] 15 min sem rede: reiniciando o aparelho");
    delay(100);
    ESP.restart();
  }
  unsigned long wait = WIFI_RETRY_MS << (wifiTries < 2 ? wifiTries : 2);
  if (now - wifiLastTry < wait) return;
  wifiTries++;
  bool radio = (wifiTries % WIFI_RADIO_EVERY == 0);
  Serial.printf("[WiFi] sem rede há %lu s: tentativa %u%s\n", (now - wifiDownSince) / 1000, wifiTries,
    radio ? " (reiniciando o rádio)" : "");
  wifiConnect(radio);
}

// =================================================================
//  Drivers LVGL
// =================================================================
static void disp_flush(lv_disp_drv_t* d, const lv_area_t* area, lv_color_t* color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)&color_p->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(d);
}

static void touch_read(lv_indev_drv_t* drv, lv_indev_data_t* data) {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int x = map(p.x, TS_MINX, TS_MAXX, 0, SCR_W - 1);
    int y = map(p.y, TS_MINY, TS_MAXY, 0, SCR_H - 1);
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = constrain(x, 0, SCR_W - 1);
    data->point.y = constrain(y, 0, SCR_H - 1);
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// =================================================================
//  Helpers de estilo
// =================================================================
static void styleCard(lv_obj_t* o) {
  lv_obj_set_style_bg_color(o, COL_CARD, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, COL_BORDER, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_radius(o, 8, 0);
  lv_obj_set_style_pad_all(o, 8, 0);
}
static void flat(lv_obj_t* o) {
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
}
static lv_obj_t* dot(lv_obj_t* parent, lv_color_t c) {
  lv_obj_t* d = lv_obj_create(parent);
  lv_obj_set_size(d, 12, 12);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(d, c, 0);
  lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(d, 0, 0);
  lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);   // o toque vale para o card ou para a lista por trás
  return d;
}
// só redesenha o rótulo quando o texto muda (as rotinas de 1 s chamam sempre)
static void setText(lv_obj_t* l, const char* t) {
  if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}
static void setLed(bool r, bool g, bool b) {   // active-LOW
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}
// botão de fora a fora (rodapé das listagens)
static lv_obj_t* wideBtn(lv_obj_t* parent, const char* txt, lv_event_cb_t cb, void* user) {
  lv_obj_t* btn = lv_btn_create(parent);
  lv_obj_set_size(btn, SCR_W - 16, 34);
  lv_obj_set_style_bg_color(btn, COL_BTN, 0);
  lv_obj_set_style_bg_color(btn, COL_PRESS, LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, COL_BORDER, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
  lv_obj_t* bl = lv_label_create(btn);
  lv_label_set_text(bl, txt);
  lv_obj_set_style_text_font(bl, &ui_font_14, 0);
  lv_obj_set_style_text_color(bl, COL_TXT, 0);
  lv_obj_center(bl);
  return btn;
}

// =================================================================
//  Navegação
// =================================================================
static void showPage(int idx) {
  for (int i = 0; i < P_COUNT; i++) {
    if (i == idx) lv_obj_clear_flag(page[i], LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_add_flag(page[i], LV_OBJ_FLAG_HIDDEN);
  }
  if (idx > P_HOME) lv_obj_scroll_to_y(listBox[idx - 1], 0, LV_ANIM_OFF);   // a listagem abre sempre do topo
}
static void nav_cb(lv_event_t* e) {
  showPage((int)(intptr_t)lv_event_get_user_data(e));
}
static void openConfig();
static void openCfg_cb(lv_event_t* e) { openConfig(); }

// =================================================================
//  Construção da UI
// =================================================================
// Barra do header: o que vier à esquerda (logo ou título) + relógio/data à direita,
// centralizados verticalmente via flex.
static lv_obj_t* headerBar(lv_obj_t* parent) {
  lv_obj_t* hb = lv_obj_create(parent);
  lv_obj_remove_style_all(hb);
  lv_obj_set_size(hb, SCR_W - 16, 24);
  lv_obj_set_pos(hb, 8, 4);
  lv_obj_clear_flag(hb, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(hb, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hb, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return hb;
}
static void headerClock(lv_obj_t* hb) {
  lv_obj_t* clk = lv_label_create(hb);
  lv_obj_set_style_text_font(clk, &ui_font_12, 0);
  lv_obj_set_style_text_color(clk, COL_MUTED, 0);
  lv_label_set_text(clk, "--/--/--  --:--");
  if (nClocks < P_COUNT) clocks[nClocks++] = clk;
}

static lv_obj_t* makePage(lv_obj_t* scr) {
  lv_obj_t* p = lv_obj_create(scr);
  lv_obj_set_size(p, SCR_W, SCR_H);
  lv_obj_set_pos(p, 0, 0);
  flat(p);   // sem padding: margens controladas explicitamente por tela
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

// adiciona o rótulo do topo (centralizado) num card
static lv_obj_t* tileCap(lv_obj_t* tile, const char* txt) {
  lv_obj_t* cap = lv_label_create(tile);
  lv_obj_set_style_text_color(cap, COL_MUTED, 0);
  lv_obj_set_style_text_font(cap, &ui_font_12, 0);
  lv_label_set_text(cap, txt);
  return cap;
}

// Início: 6 cards clicáveis (3 x 2), um por grupo do Gatus + Configurar.
static void buildHome(lv_obj_t* scr) {
  lv_obj_t* p = makePage(scr);
  page[P_HOME] = p;

  lv_obj_t* hb = headerBar(p);
  lv_obj_t* logo = lv_img_create(hb);
  lv_img_set_src(logo, &img_logo_header);
  ovAll = lv_label_create(hb);               // veredito geral (antes era um card)
  lv_obj_set_style_text_font(ovAll, &ui_font_12, 0);
  lv_obj_set_style_text_color(ovAll, COL_MUTED, 0);
  lv_label_set_text(ovAll, "...");
  headerClock(hb);

  // grade 3x2 com margens simétricas: 8 px em volta E entre os cards
  lv_obj_t* grid = lv_obj_create(p);
  lv_obj_remove_style_all(grid);
  lv_obj_set_pos(grid, 0, 28);                 // logo abaixo do header
  lv_obj_set_size(grid, SCR_W, SCR_H - 28);    // 320 x 212
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(grid, 8, 0);
  lv_obj_set_style_pad_gap(grid, 8, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  int tileW = (SCR_W - 4 * 8) / 3;        // (320-32)/3 = 96
  int tileH = (SCR_H - 28 - 3 * 8) / 2;   // (212-24)/2 = 94

  for (int t = 0; t <= G_COUNT; t++) {
    lv_obj_t* tile = lv_obj_create(grid);
    lv_obj_set_size(tile, tileW, tileH);
    styleCard(tile);
    lv_obj_set_style_bg_color(tile, COL_PRESS, LV_STATE_PRESSED);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(tile, 6, 0);
    lv_obj_set_style_pad_hor(tile, 2, 0);

    if (t < G_COUNT) {
      lv_obj_add_event_cb(tile, nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)(1 + t));
      tileCap(tile, GRP_LABEL[t]);
      ovVal[t] = lv_label_create(tile);
      lv_obj_set_style_text_font(ovVal[t], &lv_font_montserrat_22, 0);
      lv_obj_set_style_text_color(ovVal[t], COL_TXT, 0);
      lv_label_set_text(ovVal[t], "-/-");
      // linha de status (bolinha + texto), centralizada
      lv_obj_t* strow = lv_obj_create(tile);
      lv_obj_remove_style_all(strow);
      lv_obj_clear_flag(strow, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_clear_flag(strow, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(strow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
      lv_obj_set_flex_flow(strow, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(strow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_column(strow, 5, 0);
      ovDot[t] = dot(strow, COL_MUTED);
      ovStat[t] = lv_label_create(strow);
      lv_obj_set_style_text_font(ovStat[t], &ui_font_12, 0);
      lv_obj_set_style_text_color(ovStat[t], COL_MUTED, 0);
      lv_label_set_text(ovStat[t], "...");
    } else {
      lv_obj_add_event_cb(tile, openCfg_cb, LV_EVENT_CLICKED, NULL);
      tileCap(tile, "Configurar");
      lv_obj_t* ic = lv_label_create(tile);
      lv_obj_set_style_text_font(ic, &lv_font_montserrat_22, 0);
      lv_obj_set_style_text_color(ic, COL_TXT, 0);
      lv_label_set_text(ic, LV_SYMBOL_SETTINGS);
      ovNet = lv_label_create(tile);
      lv_obj_set_style_text_font(ovNet, &ui_font_12, 0);
      lv_obj_set_style_text_color(ovNet, COL_MUTED, 0);
      lv_label_set_text(ovNet, "...");
    }
  }
}

// Listagem de um grupo: título + relógio, lista rolável e o botão «Início» de fora a fora.
static void buildListPage(lv_obj_t* scr, int gi) {
  lv_obj_t* p = makePage(scr);
  page[1 + gi] = p;

  lv_obj_t* hb = headerBar(p);
  listTitle[gi] = lv_label_create(hb);
  lv_obj_set_style_text_font(listTitle[gi], &ui_font_16, 0);
  lv_obj_set_style_text_color(listTitle[gi], COL_TXT, 0);
  lv_label_set_recolor(listTitle[gi], true);
  lv_label_set_text(listTitle[gi], GRP_LABEL[gi]);
  headerClock(hb);

  lv_obj_t* box = lv_obj_create(p);
  lv_obj_set_size(box, SCR_W - 16, SCR_H - 28 - 34 - 12);   // 304 x 166
  lv_obj_set_pos(box, 8, 28);
  flat(box);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(box, 2, 0);
  lv_obj_set_style_pad_right(box, 10, 0);   // a barra de rolagem não cobre a latência
  lv_obj_set_scroll_dir(box, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);
  listBox[gi] = box;

  lv_obj_t* back = wideBtn(p, LV_SYMBOL_HOME "  Início", nav_cb, (void*)(intptr_t)P_HOME);
  lv_obj_set_pos(back, 8, SCR_H - 34 - 6);
}

static void addRow(lv_obj_t* box, const char* name, bool up, uint16_t ms) {
  lv_obj_t* row = lv_obj_create(box);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, 26);
  flat(row);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 8, 0);

  dot(row, up ? COL_UP : COL_DOWN);

  lv_obj_t* nm = lv_label_create(row);
  lv_label_set_text(nm, name);
  lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(nm, COL_TXT, 0);
  lv_obj_set_style_text_font(nm, &ui_font_14, 0);
  lv_obj_set_flex_grow(nm, 1);

  lv_obj_t* st = lv_label_create(row);
  if (up) { char b[12]; snprintf(b, sizeof(b), "%u ms", ms); lv_label_set_text(st, b);
            lv_obj_set_style_text_color(st, COL_MUTED, 0); }
  else    { lv_label_set_text(st, "off"); lv_obj_set_style_text_color(st, COL_DOWN, 0); }
  lv_obj_set_style_text_font(st, &ui_font_12, 0);
}

static void addNote(lv_obj_t* box, const char* txt) {
  lv_obj_t* l = lv_label_create(box);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, &ui_font_14, 0);
  lv_obj_set_style_text_color(l, COL_MUTED, 0);
  lv_obj_set_style_pad_top(l, 6, 0);
}

// =================================================================
//  Tela de Configuração (teclado na tela + NVS) e dados do sistema
// =================================================================
static lv_obj_t* cfgRow(lv_obj_t* parent, const char* lbl) {
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_set_width(r, lv_pct(100));
  lv_obj_set_height(r, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, 8, 0);
  lv_obj_t* l = lv_label_create(r);
  lv_label_set_text(l, lbl);
  lv_obj_set_style_text_font(l, &ui_font_12, 0);
  lv_obj_set_style_text_color(l, COL_MUTED, 0);
  lv_obj_set_width(l, 56);
  return r;
}
// linha de dados do sistema: ícone (verde) + valor
static lv_obj_t* infoLine(lv_obj_t* parent, const char* sym, const char* txt) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_t* ic = lv_label_create(row);
  lv_label_set_text(ic, sym);
  lv_obj_set_style_text_font(ic, &ui_font_14, 0);
  lv_obj_set_style_text_color(ic, COL_UP, 0);
  lv_obj_set_width(ic, 18);
  lv_obj_t* l = lv_label_create(row);
  lv_obj_set_style_text_color(l, COL_MUTED, 0);
  lv_obj_set_style_text_font(l, &ui_font_14, 0);
  lv_label_set_text(l, txt);
  return l;
}

static void ta_event(lv_event_t* e) {
  lv_obj_t* ta = lv_event_get_target(e);
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(kb);
}
static void kb_event(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}
static void cancel_cb(lv_event_t* e) {
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(cfgScreen, LV_OBJ_FLAG_HIDDEN);
}
static void save_cb(lv_event_t* e) {
  cfg.ssid = lv_textarea_get_text(taSsid);
  cfg.pass = lv_textarea_get_text(taPass);
  cfg.tz = TZ_STR[lv_dropdown_get_selected(ddTz)];
  cfg.refreshMs = REFRESH_OPT[lv_dropdown_get_selected(ddRefresh)];
  cfg.bright = lv_slider_get_value(sldBright);
  cfg.autob = lv_obj_has_state(swAuto, LV_STATE_CHECKED);
  saveCfg();
  setenv("TZ", cfg.tz.c_str(), 1); tzset();
  if (!cfg.autob) applyBrightness(cfg.bright);
  wifiConnect(false);
  wifiDown = false; wifiTries = 0; netFails = 0;
  timeSet = false; lastFetch = 0;
  Serial.printf("[Config] salvo: ssid=%s tz=%s refresh=%lu bright=%u auto=%d\n",
    cfg.ssid.c_str(), cfg.tz.c_str(), (unsigned long)cfg.refreshMs, cfg.bright, cfg.autob);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(cfgScreen, LV_OBJ_FLAG_HIDDEN);
}
static void openConfig() {
  lv_textarea_set_text(taSsid, cfg.ssid.c_str());
  lv_textarea_set_text(taPass, cfg.pass.c_str());
  lv_dropdown_set_selected(ddTz, tzIdx());
  lv_dropdown_set_selected(ddRefresh, refreshIdx());
  lv_slider_set_value(sldBright, cfg.bright, LV_ANIM_OFF);
  if (cfg.autob) lv_obj_add_state(swAuto, LV_STATE_CHECKED);
  else           lv_obj_clear_state(swAuto, LV_STATE_CHECKED);
  lv_obj_clear_flag(cfgScreen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cfgScreen);
}

static void buildConfig(lv_obj_t* scr) {
  cfgScreen = lv_obj_create(scr);
  lv_obj_remove_style_all(cfgScreen);
  lv_obj_set_size(cfgScreen, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(cfgScreen, COL_BG, 0);
  lv_obj_set_style_bg_opa(cfgScreen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(cfgScreen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cfgScreen, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* tt = lv_label_create(cfgScreen);
  lv_label_set_text(tt, "Configurações");
  lv_obj_set_style_text_font(tt, &ui_font_16, 0);
  lv_obj_set_style_text_color(tt, COL_TXT, 0);
  lv_obj_align(tt, LV_ALIGN_TOP_LEFT, 10, 6);

  // estado da rede (IP ou «conectando»), à direita do título
  sysNet = lv_label_create(cfgScreen);
  lv_obj_set_style_text_font(sysNet, &ui_font_12, 0);
  lv_obj_set_style_text_color(sysNet, COL_MUTED, 0);
  lv_label_set_text(sysNet, "...");
  lv_obj_align(sysNet, LV_ALIGN_TOP_RIGHT, -10, 9);

  // divider abaixo do título
  lv_obj_t* dv = lv_obj_create(cfgScreen);
  lv_obj_remove_style_all(dv);
  lv_obj_set_size(dv, SCR_W - 20, 1);
  lv_obj_set_pos(dv, 10, 28);
  lv_obj_set_style_bg_color(dv, COL_BORDER, 0);
  lv_obj_set_style_bg_opa(dv, LV_OPA_COVER, 0);

  lv_obj_t* form = lv_obj_create(cfgScreen);
  lv_obj_remove_style_all(form);
  lv_obj_set_pos(form, 8, 34);
  lv_obj_set_size(form, SCR_W - 16, 142);
  lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(form, 5, 0);
  lv_obj_set_scroll_dir(form, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(form, LV_SCROLLBAR_MODE_AUTO);

  lv_obj_t* r1 = cfgRow(form, "Wi-Fi");
  taSsid = lv_textarea_create(r1);
  lv_textarea_set_one_line(taSsid, true);
  lv_textarea_set_placeholder_text(taSsid, "SSID");
  lv_obj_set_flex_grow(taSsid, 1);
  lv_obj_set_height(taSsid, 30);
  lv_obj_set_style_text_font(taSsid, &ui_font_14, 0);
  lv_obj_add_event_cb(taSsid, ta_event, LV_EVENT_CLICKED, NULL);

  lv_obj_t* r2 = cfgRow(form, "Senha");
  taPass = lv_textarea_create(r2);
  lv_textarea_set_one_line(taPass, true);
  lv_textarea_set_password_mode(taPass, true);
  lv_textarea_set_placeholder_text(taPass, "senha");
  lv_obj_set_flex_grow(taPass, 1);
  lv_obj_set_height(taPass, 30);
  lv_obj_set_style_text_font(taPass, &ui_font_14, 0);
  lv_obj_add_event_cb(taPass, ta_event, LV_EVENT_CLICKED, NULL);

  lv_obj_t* r3 = cfgRow(form, "Fuso");
  ddTz = lv_dropdown_create(r3);
  lv_dropdown_set_options(ddTz, TZ_LBL);
  lv_obj_set_flex_grow(ddTz, 1);
  lv_obj_set_style_text_font(ddTz, &ui_font_14, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(ddTz), &ui_font_14, 0);

  lv_obj_t* r4 = cfgRow(form, "Atualiz.");
  ddRefresh = lv_dropdown_create(r4);
  lv_dropdown_set_options(ddRefresh, REFRESH_LBL);
  lv_obj_set_flex_grow(ddRefresh, 1);
  lv_obj_set_style_text_font(ddRefresh, &ui_font_14, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(ddRefresh), &ui_font_14, 0);

  lv_obj_t* r5 = cfgRow(form, "Brilho");
  sldBright = lv_slider_create(r5);
  lv_slider_set_range(sldBright, 5, 100);
  lv_obj_set_flex_grow(sldBright, 1);

  lv_obj_t* r6 = cfgRow(form, "Auto");
  swAuto = lv_switch_create(r6);
  lv_obj_set_size(swAuto, 44, 24);

  // dados do sistema (antes ficavam na tela «Sistema»): no fim do formulário, rolando
  lv_obj_t* dv2 = lv_obj_create(form);
  lv_obj_remove_style_all(dv2);
  lv_obj_set_size(dv2, lv_pct(100), 1);
  lv_obj_set_style_bg_color(dv2, COL_BORDER, 0);
  lv_obj_set_style_bg_opa(dv2, LV_OPA_COVER, 0);
  sysSig  = infoLine(form, LV_SYMBOL_WIFI, "Sinal: ...");
  sysUp   = infoLine(form, SYM_CLOCK, "Ligado há ...");
  sysRefr = infoLine(form, LV_SYMBOL_REFRESH, "Atualizado: nunca");
  infoLine(form, SYM_CHIP, "Firmware: " FW_VERSION);
  infoLine(form, "©", "Embrapa Gado de Corte");

  // botões (fora do form, sempre no rodapé)
  lv_obj_t* cancel = lv_btn_create(cfgScreen);
  lv_obj_set_size(cancel, 120, 32);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 10, -6);
  lv_obj_set_style_bg_color(cancel, COL_CARD, 0);
  lv_obj_add_event_cb(cancel, cancel_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* cl = lv_label_create(cancel); lv_label_set_text(cl, "Cancelar");
  lv_obj_set_style_text_font(cl, &ui_font_14, 0); lv_obj_center(cl);

  lv_obj_t* save = lv_btn_create(cfgScreen);
  lv_obj_set_size(save, 120, 32);
  lv_obj_align(save, LV_ALIGN_BOTTOM_RIGHT, -10, -6);
  lv_obj_set_style_bg_color(save, COL_UP, 0);
  lv_obj_add_event_cb(save, save_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* sl = lv_label_create(save); lv_label_set_text(sl, "Salvar");
  lv_obj_set_style_text_font(sl, &ui_font_14, 0); lv_obj_center(sl);

  // teclado (oculto; aparece ao tocar num campo de texto)
  kb = lv_keyboard_create(cfgScreen);
  lv_obj_set_size(kb, SCR_W, 120);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, taSsid);
  lv_obj_add_event_cb(kb, kb_event, LV_EVENT_ALL, NULL);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

// =================================================================
//  Atualização da UI a partir dos dados
// =================================================================
// Dados velhos: sem leitura válida há 3 intervalos (no mínimo 3 min). A tela deixa de dizer «Healthy».
static bool dataStale() {
  if (!dataFromGatus) return false;
  unsigned long lim = cfg.refreshMs * 3UL;
  if (lim < 180000UL) lim = 180000UL;
  return millis() - lastOkRefresh > lim;
}
static void ageText(char* b, size_t n) {
  unsigned long m = (millis() - lastOkRefresh) / 60000UL;
  if (m < 1)       snprintf(b, n, "agora");
  else if (m < 60) snprintf(b, n, "há %lu min", m);
  else             snprintf(b, n, "há %lu h", m / 60);
}

// Cards do início, veredito geral e LED. Depende dos dados e do estado (conexão, dados velhos).
static void renderSummary() {
  bool stale = dataStale();
  char age[16]; ageText(age, sizeof(age));
  for (int g = 0; g < G_COUNT; g++) {
    lv_color_t c = COL_MUTED;
    if (!dataFromGatus) {
      setText(ovVal[g], "-/-"); setText(ovStat[g], "...");
    } else {
      char b[16]; snprintf(b, sizeof(b), "%d/%d", grpUp[g], grpTot[g]);
      setText(ovVal[g], b);
      int gdown = grpTot[g] - grpUp[g];
      if (stale)           setText(ovStat[g], age);
      else if (gdown == 0) { c = COL_UP; setText(ovStat[g], grpTot[g] > 0 ? "Healthy" : "vazio"); }
      else                 { c = COL_WARN; char sb[12]; snprintf(sb, sizeof(sb), "%d off", gdown); setText(ovStat[g], sb); }
      if (!stale && grpTot[g] == 0) c = COL_MUTED;
    }
    lv_obj_set_style_bg_color(ovDot[g], c, 0);
    lv_obj_set_style_text_color(ovStat[g], c, 0);
    // título da listagem: contagem, ou a idade dos dados quando estão velhos
    if (dataFromGatus) {
      char t[56];
      if (stale) snprintf(t, sizeof(t), "%s  #F85149 %s#", GRP_LABEL[g], age);
      else       snprintf(t, sizeof(t), "%s  #8B949E %d/%d#", GRP_LABEL[g], grpUp[g], grpTot[g]);
      setText(listTitle[g], t);
    }
  }
  // veredito geral (header do início) + LED RGB: verde = tudo no ar, vermelho = algo fora, azul = sem dados
  if (!dataFromGatus) {
    setText(ovAll, cfg.ssid.length() == 0 ? "sem Wi-Fi" : (netUp ? "lendo..." : "conectando..."));
    lv_obj_set_style_text_color(ovAll, COL_MUTED, 0);
    setLed(false, false, true);
  } else if (stale) {
    setText(ovAll, LV_SYMBOL_WARNING " Offline");
    lv_obj_set_style_text_color(ovAll, COL_DOWN, 0);
    setLed(false, false, true);
  } else if (downTotal == 0) {
    setText(ovAll, LV_SYMBOL_OK " Healthy");
    lv_obj_set_style_text_color(ovAll, COL_UP, 0);
    setLed(false, true, false);
  } else {
    char b[20]; snprintf(b, sizeof(b), LV_SYMBOL_WARNING " %d off", downTotal);
    setText(ovAll, b);
    lv_obj_set_style_text_color(ovAll, COL_WARN, 0);
    setLed(true, false, false);
  }
}
// assinatura do que o renderSummary mostra, para só redesenhar quando algo muda
static uint32_t summarySig() {
  bool stale = dataStale();
  uint32_t age = stale ? (millis() - lastOkRefresh) / 60000UL : 0;
  if (age >= 60) age = 60 + age / 60;
  return (dataFromGatus ? 1 : 0) | (stale ? 2 : 0) | (netUp ? 4 : 0) | (age << 8);
}

// Contagens por grupo + listagens (fora do ar primeiro). Só quando chegam dados novos.
static void renderData() {
  downTotal = 0;
  for (int g = 0; g < G_COUNT; g++) { grpUp[g] = 0; grpTot[g] = 0; }
  for (int i = 0; i < epCount; i++) {
    uint8_t g = eps[i].grp; if (g >= G_COUNT) continue;
    grpTot[g]++; if (eps[i].up) grpUp[g]++; else downTotal++;
  }
  for (int g = 0; g < G_COUNT; g++) {
    lv_obj_clean(listBox[g]);
    if (!dataFromGatus) {
      addNote(listBox[g], "Aguardando a primeira leitura...");
      continue;
    }
    if (grpTot[g] == 0) addNote(listBox[g], "Nenhum item neste grupo.");
    for (int pass = 0; pass < 2; pass++)
      for (int i = 0; i < epCount; i++)
        if (eps[i].grp == g && (pass == 0 ? !eps[i].up : eps[i].up))
          addRow(listBox[g], eps[i].name, eps[i].up, eps[i].ms);
  }
  renderSummary();
}

// Estado do Wi-Fi: card Configurar (início) e topo da Configuração.
static void renderNet() {
  if (netUp) {
    setText(ovNet, "Wi-Fi OK");
    lv_obj_set_style_text_color(ovNet, COL_UP, 0);
    String s = String(LV_SYMBOL_WIFI " ") + WiFi.localIP().toString();
    setText(sysNet, s.c_str());
    lv_obj_set_style_text_color(sysNet, COL_UP, 0);
  } else {
    bool none = (cfg.ssid.length() == 0);
    setText(ovNet, none ? "Sem Wi-Fi" : "Conectando");
    lv_obj_set_style_text_color(ovNet, COL_WARN, 0);
    setText(sysNet, none ? "Wi-Fi não configurado" : LV_SYMBOL_WIFI " conectando...");
    lv_obj_set_style_text_color(sysNet, COL_WARN, 0);
  }
  lv_obj_align(sysNet, LV_ALIGN_TOP_RIGHT, -10, 9);
}

// =================================================================
//  Dados: fetch do Gatus
// =================================================================
// Copia o nome sem cortar caractere UTF-8 no meio. A fonte só tem Latin-1:
// travessão e meia-risca (E2 80 94 / E2 80 93) viram hífen.
static void addEp(const char* n, uint8_t g, bool up, uint16_t ms) {
  if (epCount >= MAX_EP) return;
  char* d = eps[epCount].name;
  const size_t cap = sizeof(eps[0].name) - 1;
  size_t o = 0;
  const unsigned char* s = (const unsigned char*)n;
  while (*s) {
    if (s[0] == 0xE2 && s[1] == 0x80 && (s[2] == 0x93 || s[2] == 0x94)) {
      if (o + 1 > cap) break;
      d[o++] = '-'; s += 3; continue;
    }
    size_t len = (*s < 0x80) ? 1 : (*s >= 0xF0) ? 4 : (*s >= 0xE0) ? 3 : 2;
    if (o + len > cap) break;
    for (size_t k = 0; k < len && *s; k++) d[o++] = (char)*s++;
  }
  d[o] = 0;
  eps[epCount].grp = g; eps[epCount].up = up; eps[epCount].ms = ms; epCount++;
}

// Grupo do Gatus → card. Nome exato primeiro; se o grupo for renomeado, a inicial ainda acerta
// (como era antes); o que não casar fica em Serviços.
static uint8_t grpFrom(const char* g) {
  for (int i = 0; i < G_COUNT; i++) if (strcmp(g, GRP_LABEL[i]) == 0) return i;
  switch (g[0]) {
    case 'C': return G_CLUSTERS;
    case 'H': return G_HOSTS;
    case 'P': return G_PANELS;
    case 'G': return G_GPUS;
  }
  return G_SERVICES;
}

enum FetchResult { FETCH_NET_FAIL = -1, FETCH_BAD = 0, FETCH_OK = 1 };

// FETCH_NET_FAIL = sem resposta nenhuma (rede); FETCH_BAD = o servidor respondeu, mas não com os dados.
static FetchResult fetchGatus() {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient https;
  https.setConnectTimeout(8000);
  https.setTimeout(8000);
  https.useHTTP10(true);   // sem «chunked»: o ArduinoJson lê direto do stream
  String url = String("https://") + GATUS_HOST + GATUS_PATH;
  if (!https.begin(client, url)) return FETCH_NET_FAIL;
  static const char* hdrKeys[] = { "Date" };
  https.collectHeaders(hdrKeys, 1);
  int code = https.GET();
  Serial.printf("[Gatus] HTTP %d\n", code);
  FetchResult res = (code > 0) ? FETCH_BAD : FETCH_NET_FAIL;
  if (code == 200) {
    if (!timeSet) {   // ajusta o relógio pelo header Date (GMT) — sem SNTP/UDP
      String d = https.header("Date");
      struct tm tmd; memset(&tmd, 0, sizeof(tmd));
      if (d.length() && strptime(d.c_str(), "%a, %d %b %Y %H:%M:%S", &tmd)) {
        setenv("TZ", "UTC0", 1); tzset();
        time_t utc = mktime(&tmd);
        setenv("TZ", cfg.tz.c_str(), 1); tzset();   // volta ao fuso configurado
        struct timeval tv = { utc, 0 };
        settimeofday(&tv, nullptr);
        timeSet = true;
        Serial.printf("[Time] Date set: %s\n", d.c_str());
      }
    }
    JsonDocument filter;
    filter[0]["name"] = true;
    filter[0]["group"] = true;
    filter[0]["results"][0]["success"] = true;
    filter[0]["results"][0]["duration"] = true;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, https.getStream(), DeserializationOption::Filter(filter));
    JsonArray arr = doc.as<JsonArray>();
    if (!err && !arr.isNull() && arr.size() > 0) {   // resposta vazia ou de erro não apaga o que está na tela
      epCount = 0;
      for (JsonObject e : arr) {
        const char* nm = e["name"] | "?";
        uint8_t g = grpFrom(e["group"] | "");
        JsonArray rs = e["results"].as<JsonArray>();
        bool up = false; uint64_t dur = 0;   // nanossegundos
        if (!rs.isNull() && rs.size() > 0) { up = rs[0]["success"] | false; dur = rs[0]["duration"] | (uint64_t)0; }
        addEp(nm, g, up, (uint16_t)min<uint64_t>(dur / 1000000ULL, 65535));
      }
      dataFromGatus = true; res = FETCH_OK;
      Serial.printf("[Gatus] %d endpoints carregados\n", epCount);
    } else {
      Serial.printf("[Gatus] resposta inválida (%s)\n", err.c_str());
    }
  }
  https.end();
  return res;
}

// =================================================================
//  setup / loop
// =================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n[DeskBuddy] boot " FW_VERSION);
  loadCfg();
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  setLed(false, false, false);

  tft.init();
  tft.setRotation(1);              // landscape 320x240
  tft.fillScreen(TFT_BLACK);
  ledcAttach(BL_PIN, 5000, 8);     // backlight via PWM (controle de brilho)
  applyBrightness(cfg.bright);

  touchSPI.begin(T_CLK, T_MISO, T_MOSI, T_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, lvbuf, NULL, SCR_W * 20);
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCR_W; disp_drv.ver_res = SCR_H;
  disp_drv.flush_cb = disp_flush; disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER; indev_drv.read_cb = touch_read;
  lv_indev_drv_register(&indev_drv);

  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  buildHome(scr);
  for (int g = 0; g < G_COUNT; g++) buildListPage(scr, g);
  buildConfig(scr);

  renderData();
  renderNet();
  showPage(P_HOME);

  // splash de boot: logos coloridas (Embrapa + embrapa.io) sobre cinza bem claro
  splash = lv_obj_create(scr);
  lv_obj_remove_style_all(splash);
  lv_obj_set_size(splash, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(splash, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
  lv_obj_clear_flag(splash, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* sem = lv_img_create(splash);
  lv_img_set_src(sem, &img_embrapa_color);
  lv_obj_align(sem, LV_ALIGN_TOP_MID, 0, 16);            // margem do topo = margem de baixo do texto
  lv_obj_t* sio = lv_img_create(splash);
  lv_img_set_src(sio, &img_io_color);
  lv_obj_align(sio, LV_ALIGN_CENTER, 0, 0);              // centralizada verticalmente na tela
  lv_obj_t* sp = lv_spinner_create(splash, 1000, 60);   // indicador de progresso
  lv_obj_set_size(sp, 26, 26);
  lv_obj_align(sp, LV_ALIGN_BOTTOM_MID, 0, -56);
  lv_obj_set_style_arc_width(sp, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(sp, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(sp, lv_color_hex(0xCBD3DE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(sp, lv_color_hex(0x00773D), LV_PART_INDICATOR);
  lv_obj_t* scn = lv_label_create(splash);
  lv_label_set_text(scn, "Conectando ao Wi-Fi...");
  lv_obj_set_style_text_font(scn, &ui_font_12, 0);
  lv_obj_set_style_text_color(scn, lv_color_hex(0x5B6472), 0);
  lv_obj_align(scn, LV_ALIGN_BOTTOM_MID, 0, -32);
  lv_obj_t* scredit = lv_label_create(splash);
  lv_label_set_text(scredit, "Desenvolvido pela Embrapa Gado de Corte");
  lv_obj_set_style_text_font(scredit, &ui_font_12, 0);
  lv_obj_set_style_text_color(scredit, lv_color_hex(0x9AA4B2), 0);
  lv_obj_align(scredit, LV_ALIGN_BOTTOM_MID, 0, -12);

  // unidade sem Wi-Fi configurado (nova/distribuída) → abre a Configuração direto
  if (cfg.ssid.length() == 0) {
    if (splash) { lv_obj_del(splash); splash = nullptr; }
    openConfig();
  }

  setenv("TZ", cfg.tz.c_str(), 1); tzset();  // fuso configurável (NVS); hora vem do header Date do Gatus
  WiFi.persistent(false);          // as credenciais ficam na nossa NVS; o begin não regrava a flash
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);            // ligado na tomada: sem economia de energia no rádio (menos quedas)
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWifiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  wifiConnect(false);
}

void loop() {
  lv_timer_handler();

  // fecha o splash após a 1ª leitura OK do Gatus (ou no máx. 12 s)
  if (splash && millis() > 12000) { lv_obj_del(splash); splash = nullptr; }

  // estado do Wi-Fi + vigia da reconexão
  bool conn = (WiFi.status() == WL_CONNECTED);
  if (conn != netUp) {
    netUp = conn;
    if (conn) {
      Serial.printf("[WiFi] conectado %s (%d dBm)\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    } else {
      wifiDrops++;
      uint8_t r = wifiReason;
      Serial.printf("[WiFi] desconectado (motivo %u: %s)\n", r, WiFi.disconnectReasonName((wifi_err_reason_t)r));
    }
    renderNet();
  }
  wifiWatchdog();

  // rotinas de 1 s: brilho automático, relógio, dados do sistema e estado dos cards
  static unsigned long lastTick = 0;
  if (millis() - lastTick > 1000) {
    lastTick = millis();
    if (cfg.autob) {   // brilho automático pelo LDR
      int ldr = analogRead(PIN_LDR);
      applyBrightness(map(constrain(ldr, 0, 4095), 0, 4095, 20, 100));
    }
    unsigned long s = millis() / 1000;
    char u[48]; snprintf(u, sizeof(u), "Ligado há %luh %02lum %02lus", s/3600, (s/60)%60, s%60);
    setText(sysUp, u);
    if (netUp) snprintf(u, sizeof(u), "Sinal: %d dBm · quedas: %u", (int)WiFi.RSSI(), wifiDrops);
    else       snprintf(u, sizeof(u), "Sinal: sem rede · quedas: %u", wifiDrops);
    setText(sysSig, u);
    if (!dataFromGatus) snprintf(u, sizeof(u), "Atualizado: nunca");
    else { char a[16]; ageText(a, sizeof(a));
           if (fetchFails) snprintf(u, sizeof(u), "Atualizado: %s (%u falhas)", a, fetchFails);
           else            snprintf(u, sizeof(u), "Atualizado: %s", a); }
    setText(sysRefr, u);

    time_t nowt = time(nullptr);
    if (nowt > 1700000000) {   // hora já ajustada pelo header Date
      struct tm tmv; localtime_r(&nowt, &tmv);
      char cb[40]; snprintf(cb, sizeof(cb), "%d/%d/%02d  %02d:%02d",
        tmv.tm_mday, tmv.tm_mon + 1, (tmv.tm_year + 1900) % 100, tmv.tm_hour, tmv.tm_min);
      for (int i = 0; i < nClocks; i++) setText(clocks[i], cb);
    }

    static uint32_t lastSig = 0xFFFFFFFF;
    uint32_t sig = summarySig();
    if (sig != lastSig) { lastSig = sig; renderSummary(); }
  }

  // leitura periódica: a 1ª assim que conectar; depois a cada intervalo; se falhou, de novo em 10 s (30 s da 4ª em diante)
  unsigned long fetchIv = !fetchFails ? cfg.refreshMs : (fetchFails <= 3 ? FETCH_RETRY_MS : 3 * FETCH_RETRY_MS);
  if (conn && (lastFetch == 0 || millis() - lastFetch > fetchIv)) {
    lastFetch = millis(); if (lastFetch == 0) lastFetch = 1;
    FetchResult r = fetchGatus();
    if (r == FETCH_OK) {
      fetchFails = 0; netFails = 0;
      lastOkRefresh = millis();
      renderData();
      if (splash) { lv_obj_del(splash); splash = nullptr; }
    } else {
      if (fetchFails < 255) fetchFails++;
      if (r == FETCH_BAD) netFails = 0;   // o servidor respondeu: a rede está boa, o problema é do outro lado
      else if (++netFails % NET_FAILS_RECONNECT == 0) {
        bool radio = (netFails % (NET_FAILS_RECONNECT * WIFI_RADIO_EVERY) == 0);
        Serial.printf("[WiFi] %u leituras sem resposta com o Wi-Fi conectado: refazendo a conexão%s\n",
          netFails, radio ? " (reiniciando o rádio)" : "");
        wifiConnect(radio);
      }
    }
  }
  delay(5);
}

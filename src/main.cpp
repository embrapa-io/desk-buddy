// ─────────────────────────────────────────────────────────────
//  Desk Buddy — Painel de status da plataforma Embrapa I/O
//  CYD ESP32-2432S028R · Arduino + TFT_eSPI + LVGL 8.3 + ArduinoJson 7
//  v0 (mockup flasheável): menu lateral de 5 telas + dados ao vivo do Gatus.
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
#include "config.h"

// Fontes Montserrat com acentos PT-BR (geradas via lv_font_conv → src/ui_font_*.c)
LV_FONT_DECLARE(ui_font_12);
LV_FONT_DECLARE(ui_font_14);
LV_FONT_DECLARE(ui_font_16);

// Logos (bitmaps LVGL gerados em src/img_*.c)
LV_IMG_DECLARE(img_logo_header);   // embrapa.io branca (header)
LV_IMG_DECLARE(img_io_color);      // embrapa.io colorida (splash)
LV_IMG_DECLARE(img_embrapa_color); // Embrapa colorida (splash)

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
#define COL_BORDER lv_color_hex(0x2D333B)
#define COL_TXT    lv_color_hex(0xE6EDF3)
#define COL_MUTED  lv_color_hex(0x8B949E)
#define COL_UP     lv_color_hex(0x3FB950)
#define COL_WARN   lv_color_hex(0xD29922)
#define COL_DOWN   lv_color_hex(0xF85149)

// ---------------- Modelo de dados ----------------
enum Grp { G_CLUSTERS = 0, G_HOSTS = 1, G_SERVICOS = 2, G_COUNT = 3 };
const char* GRP_LABEL[G_COUNT] = { "Clusters", "Hosts", "Serviços" };
struct Endpoint { char name[40]; uint8_t grp; bool up; uint16_t ms; };
#define MAX_EP 64
Endpoint eps[MAX_EP];
int epCount = 0;
bool dataFromGatus = false;

// ---------------- Referências da UI ----------------
lv_obj_t* page[5];
lv_obj_t* navBtn[5];
lv_obj_t* navIcon[5];
lv_obj_t* navTxt[5];
int activePage = 0;
// overview
lv_obj_t* ovVal[G_COUNT];
lv_obj_t* ovDot[G_COUNT];
lv_obj_t* ovStat[G_COUNT];
lv_obj_t* ovAllIcon;
lv_obj_t* ovAllTxt;
// listas
lv_obj_t* listBox[G_COUNT];
// sistema
lv_obj_t* sysWifi;
lv_obj_t* sysIp;
lv_obj_t* sysUp;
lv_obj_t* sysRefr;
lv_obj_t* clocks[5]; int nClocks = 0;   // relógio + data por tela (no header)
lv_obj_t* splash = nullptr;   // tela de boot (logos)
bool timeSet = false;         // hora já ajustada (via header Date do Gatus)
unsigned long lastOkRefresh = 0;

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
  return d;
}

// =================================================================
//  Navegação
// =================================================================
static void showPage(int idx) {
  activePage = idx;
  for (int i = 0; i < 5; i++) {
    if (i == idx) lv_obj_clear_flag(page[i], LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_add_flag(page[i], LV_OBJ_FLAG_HIDDEN);
    bool act = (i == idx);
    lv_obj_set_style_bg_color(navBtn[i], act ? lv_color_hex(0x1B2330) : COL_CARD, 0);
    lv_color_t fg = act ? COL_UP : COL_MUTED;
    lv_obj_set_style_text_color(navIcon[i], fg, 0);
    lv_obj_set_style_text_color(navTxt[i], fg, 0);
  }
}
static void nav_cb(lv_event_t* e) {
  showPage((int)(intptr_t)lv_event_get_user_data(e));
}

// =================================================================
//  Construção da UI
// =================================================================
static const char* NAV_ICON[5] = { LV_SYMBOL_HOME, LV_SYMBOL_DRIVE, LV_SYMBOL_LIST, LV_SYMBOL_GPS, LV_SYMBOL_SETTINGS };
static const char* NAV_TXT[5]  = { "Geral", "Clusters", "Hosts", "Serviços", "Sistema" };

// Barra do header (apenas logo embrapa.io à esq. + relógio/data à dir.),
// ambos centralizados verticalmente via flex. Sem subtítulos.
static void header(lv_obj_t* parent) {
  lv_obj_t* hb = lv_obj_create(parent);
  lv_obj_remove_style_all(hb);
  lv_obj_set_size(hb, SCR_W - 64 - 16, 24);   // largura do conteúdo - 8px cada lado
  lv_obj_set_pos(hb, 8, 4);
  lv_obj_clear_flag(hb, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(hb, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hb, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* logo = lv_img_create(hb);
  lv_img_set_src(logo, &img_logo_header);

  lv_obj_t* clk = lv_label_create(hb);
  lv_obj_set_style_text_font(clk, &ui_font_12, 0);
  lv_obj_set_style_text_color(clk, COL_MUTED, 0);
  lv_label_set_text(clk, "--/--/--  --:--");
  if (nClocks < 5) clocks[nClocks++] = clk;
}

static void buildNav(lv_obj_t* scr) {
  lv_obj_t* nav = lv_obj_create(scr);
  lv_obj_set_size(nav, 64, SCR_H);
  lv_obj_set_pos(nav, 0, 0);
  flat(nav);
  lv_obj_set_style_bg_color(nav, COL_CARD, 0);
  lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
  lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  for (int i = 0; i < 5; i++) {
    lv_obj_t* b = lv_btn_create(nav);
    lv_obj_set_size(b, 58, 42);
    flat(b);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    lv_obj_t* ic = lv_label_create(b);
    lv_label_set_text(ic, NAV_ICON[i]);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_16, 0);
    lv_obj_t* tx = lv_label_create(b);
    lv_label_set_text(tx, NAV_TXT[i]);
    lv_obj_set_style_text_font(tx, &ui_font_12, 0);

    navBtn[i] = b; navIcon[i] = ic; navTxt[i] = tx;
  }
}

static lv_obj_t* makePage(lv_obj_t* scr) {
  lv_obj_t* p = lv_obj_create(scr);
  lv_obj_set_size(p, SCR_W - 64, SCR_H);
  lv_obj_set_pos(p, 64, 0);
  flat(p);   // sem padding: margens controladas explicitamente por tela
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

// adiciona o rótulo do topo (centralizado) num tile
static lv_obj_t* tileCap(lv_obj_t* tile, const char* txt) {
  lv_obj_t* cap = lv_label_create(tile);
  lv_obj_set_style_text_color(cap, COL_MUTED, 0);
  lv_obj_set_style_text_font(cap, &ui_font_12, 0);
  lv_label_set_text(cap, txt);
  return cap;
}

static void buildOverview(lv_obj_t* scr) {
  lv_obj_t* p = makePage(scr);
  page[0] = p;
  header(p);

  // grade 2x2 com margens simétricas: 8 px em volta E entre os tiles
  lv_obj_t* grid = lv_obj_create(p);
  lv_obj_remove_style_all(grid);
  lv_obj_set_pos(grid, 0, 28);                 // logo abaixo do header
  lv_obj_set_size(grid, SCR_W - 64, SCR_H - 28); // 256 x 212
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(grid, 8, 0);
  lv_obj_set_style_pad_gap(grid, 8, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  int tileW = (SCR_W - 64 - 3 * 8) / 2;   // (256-24)/2 = 116
  int tileH = (SCR_H - 28 - 3 * 8) / 2;   // (212-24)/2 = 94

  for (int t = 0; t < 4; t++) {
    lv_obj_t* tile = lv_obj_create(grid);
    lv_obj_set_size(tile, tileW, tileH);
    styleCard(tile);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(tile, 6, 0);

    if (t < G_COUNT) {
      tileCap(tile, GRP_LABEL[t]);
      ovVal[t] = lv_label_create(tile);
      lv_obj_set_style_text_font(ovVal[t], &lv_font_montserrat_22, 0);
      lv_obj_set_style_text_color(ovVal[t], COL_TXT, 0);
      lv_label_set_text(ovVal[t], "-/-");
      // linha de status (bolinha + texto), centralizada
      lv_obj_t* strow = lv_obj_create(tile);
      lv_obj_remove_style_all(strow);
      lv_obj_clear_flag(strow, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_size(strow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
      lv_obj_set_flex_flow(strow, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(strow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_column(strow, 5, 0);
      ovDot[t] = dot(strow, COL_MUTED);
      ovStat[t] = lv_label_create(strow);
      lv_obj_set_style_text_font(ovStat[t], &ui_font_12, 0);
      lv_label_set_text(ovStat[t], "...");
    } else {
      tileCap(tile, "Geral");
      ovAllIcon = lv_label_create(tile);
      lv_obj_set_style_text_font(ovAllIcon, &lv_font_montserrat_22, 0);
      lv_obj_set_style_text_color(ovAllIcon, COL_MUTED, 0);
      lv_label_set_text(ovAllIcon, LV_SYMBOL_REFRESH);
      ovAllTxt = lv_label_create(tile);
      lv_obj_set_style_text_font(ovAllTxt, &ui_font_12, 0);
      lv_label_set_text(ovAllTxt, "lendo...");
    }
  }
}

static lv_obj_t* buildListPage(lv_obj_t* scr, int gi, const char* title) {
  lv_obj_t* p = makePage(scr);
  header(p);
  lv_obj_t* box = lv_obj_create(p);
  lv_obj_set_size(box, SCR_W - 64 - 16, SCR_H - 28 - 8);   // 240 x 204
  lv_obj_set_pos(box, 8, 28);
  flat(box);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(box, 2, 0);
  lv_obj_set_scroll_dir(box, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);
  listBox[gi] = box;
  return p;
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

static void buildSystem(lv_obj_t* scr) {
  lv_obj_t* p = makePage(scr);
  page[4] = p;
  header(p);
  lv_obj_t* box = lv_obj_create(p);
  lv_obj_set_size(box, SCR_W - 64 - 16, SCR_H - 28 - 8);   // 240 x 204
  lv_obj_set_pos(box, 8, 28);
  flat(box);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(box, 6, 0);

  auto line = [&](const char* k) {
    lv_obj_t* l = lv_label_create(box);
    lv_obj_set_style_text_color(l, COL_TXT, 0);
    lv_obj_set_style_text_font(l, &ui_font_14, 0);
    lv_label_set_text(l, k);
    return l;
  };
  sysWifi = line("WiFi: ...");
  sysIp   = line("IP: ...");
  sysUp   = line("Uptime: ...");
  sysRefr = line("Atualizado: nunca");
  lv_obj_t* fw = line("Firmware: " FW_VERSION);
  lv_obj_set_style_text_color(fw, COL_MUTED, 0);
}

// =================================================================
//  Atualização da UI a partir dos dados
// =================================================================
static void refreshUI() {
  int up[G_COUNT] = {0,0,0}, tot[G_COUNT] = {0,0,0}, downTotal = 0;
  for (int i = 0; i < epCount; i++) {
    uint8_t g = eps[i].grp; if (g >= G_COUNT) continue;
    tot[g]++; if (eps[i].up) up[g]++; else downTotal++;
  }
  // tiles
  for (int g = 0; g < G_COUNT; g++) {
    char b[16]; snprintf(b, sizeof(b), "%d/%d", up[g], tot[g]);
    lv_label_set_text(ovVal[g], b);
    int gdown = tot[g] - up[g];
    bool allUp = (gdown == 0 && tot[g] > 0);
    lv_obj_set_style_bg_color(ovDot[g], allUp ? COL_UP : COL_WARN, 0);
    if (allUp) { lv_label_set_text(ovStat[g], "Healthy"); }
    else { char sb[12]; snprintf(sb, sizeof(sb), "%d off", gdown); lv_label_set_text(ovStat[g], sb); }
    lv_obj_set_style_text_color(ovStat[g], allUp ? COL_UP : COL_WARN, 0);
  }
  // geral
  if (downTotal == 0) {
    lv_label_set_text(ovAllIcon, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(ovAllIcon, COL_UP, 0);
    lv_label_set_text(ovAllTxt, "Healthy");
    lv_obj_set_style_text_color(ovAllTxt, COL_UP, 0);
  } else {
    char b[20]; snprintf(b, sizeof(b), "%d off", downTotal);
    lv_label_set_text(ovAllIcon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(ovAllIcon, COL_WARN, 0);
    lv_label_set_text(ovAllTxt, b);
    lv_obj_set_style_text_color(ovAllTxt, COL_WARN, 0);
  }
  // listas (fora do ar primeiro)
  for (int g = 0; g < G_COUNT; g++) {
    lv_obj_clean(listBox[g]);
    for (int pass = 0; pass < 2; pass++)
      for (int i = 0; i < epCount; i++)
        if (eps[i].grp == g && (pass == 0 ? !eps[i].up : eps[i].up))
          addRow(listBox[g], eps[i].name, eps[i].up, eps[i].ms);
  }
  // LED RGB (active-LOW): verde = tudo no ar, vermelho = algo fora
  digitalWrite(LED_R, downTotal == 0 ? HIGH : LOW);
  digitalWrite(LED_G, downTotal == 0 ? LOW : HIGH);
  digitalWrite(LED_B, HIGH);
}

// =================================================================
//  Dados: seed local + fetch do Gatus
// =================================================================
static void addEp(const char* n, uint8_t g, bool up, uint16_t ms) {
  if (epCount >= MAX_EP) return;
  strncpy(eps[epCount].name, n, 39); eps[epCount].name[39] = 0;
  eps[epCount].grp = g; eps[epCount].up = up; eps[epCount].ms = ms; epCount++;
}
static void seedData() {  // placeholder até o 1º fetch
  epCount = 0;
  const char* cl[] = {"agrodigital1","agrodigital2","cloud","cluster","storage","ucb"};
  for (auto c : cl) addEp(c, G_CLUSTERS, true, 3);
  const char* hs[] = {"api.embrapa.io","core.embrapa.io","git.embrapa.io","docs.embrapa.io"};
  for (auto h : hs) addEp(h, G_HOSTS, true, 20);
  const char* sv[] = {"API Gateway","Core API","Dashboard","GitLab","Grafana","Portal"};
  for (auto s : sv) addEp(s, G_SERVICOS, true, 40);
}

static uint8_t grpFrom(const char* g) {
  if (!g || !g[0]) return G_SERVICOS;
  switch (g[0]) { case 'C': return G_CLUSTERS; case 'H': return G_HOSTS; default: return G_SERVICOS; }
}

static bool fetchGatus() {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient https; https.setTimeout(8000);
  String url = String("https://") + GATUS_HOST + GATUS_PATH;
  if (!https.begin(client, url)) return false;
  static const char* hdrKeys[] = { "Date" };
  https.collectHeaders(hdrKeys, 1);
  int code = https.GET();
  Serial.printf("[Gatus] HTTP %d\n", code);
  bool ok = false;
  if (code == 200) {
    if (!timeSet) {   // ajusta o relógio pelo header Date (GMT) — sem SNTP/UDP
      String d = https.header("Date");
      struct tm tmd; memset(&tmd, 0, sizeof(tmd));
      if (d.length() && strptime(d.c_str(), "%a, %d %b %Y %H:%M:%S", &tmd)) {
        setenv("TZ", "UTC0", 1); tzset();
        time_t utc = mktime(&tmd);
        setenv("TZ", "<-04>4", 1); tzset();
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
    if (!err) {
      epCount = 0;
      for (JsonObject e : doc.as<JsonArray>()) {
        const char* nm = e["name"] | "?";
        uint8_t g = grpFrom(e["group"] | "");
        JsonArray rs = e["results"].as<JsonArray>();
        bool up = false; uint32_t dur = 0;
        if (!rs.isNull() && rs.size() > 0) { up = rs[0]["success"] | false; dur = rs[0]["duration"] | 0; }
        addEp(nm, g, up, (uint16_t)min<uint32_t>(dur / 1000000UL, 65535));
      }
      dataFromGatus = true; ok = true;
      Serial.printf("[Gatus] %d endpoints carregados\n", epCount);
    }
  }
  https.end();
  return ok;
}

// =================================================================
//  setup / loop
// =================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n[DeskBuddy] boot");
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);

  tft.init();
  tft.setRotation(1);              // landscape 320x240
  tft.fillScreen(TFT_BLACK);

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

  buildNav(scr);
  buildOverview(scr);
  page[1] = buildListPage(scr, G_CLUSTERS, "Clusters");
  page[2] = buildListPage(scr, G_HOSTS, "Hosts");
  page[3] = buildListPage(scr, G_SERVICOS, "Serviços");
  buildSystem(scr);

  seedData();
  refreshUI();
  showPage(0);

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
  lv_obj_align(sp, LV_ALIGN_BOTTOM_MID, 0, -44);
  lv_obj_set_style_arc_width(sp, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(sp, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(sp, lv_color_hex(0xCBD3DE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(sp, lv_color_hex(0x00773D), LV_PART_INDICATOR);
  lv_obj_t* scn = lv_label_create(splash);
  lv_label_set_text(scn, "Conectando ao Wi-Fi...");
  lv_obj_set_style_text_font(scn, &ui_font_12, 0);
  lv_obj_set_style_text_color(scn, lv_color_hex(0x5B6472), 0);
  lv_obj_align(scn, LV_ALIGN_BOTTOM_MID, 0, -16);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  setenv("TZ", "<-04>4", 1); tzset();  // America/Campo_Grande (UTC-4); hora vem do header Date do Gatus
}

unsigned long lastFetch = 0;
void loop() {
  lv_timer_handler();

  // fecha o splash após a 1ª leitura OK do Gatus (ou no máx. 12 s)
  if (splash && millis() > 12000) { lv_obj_del(splash); splash = nullptr; }

  // WiFi/IP info na tela Sistema
  static bool wasConn = false;
  bool conn = (WiFi.status() == WL_CONNECTED);
  if (conn != wasConn) {
    wasConn = conn;
    lv_label_set_text(sysWifi, conn ? "WiFi: " WIFI_SSID " (OK)" : "WiFi: conectando...");
    char ip[32]; snprintf(ip, sizeof(ip), "IP: %s", conn ? WiFi.localIP().toString().c_str() : "—");
    lv_label_set_text(sysIp, ip);
    Serial.printf("[WiFi] %s %s\n", conn ? "conectado" : "desconectado", conn ? WiFi.localIP().toString().c_str() : "");
  }

  // uptime
  static unsigned long lastUp = 0;
  if (millis() - lastUp > 1000) {
    lastUp = millis();
    unsigned long s = millis() / 1000;
    char u[40]; snprintf(u, sizeof(u), "Uptime: %luh %02lum %02lus", s/3600, (s/60)%60, s%60);
    lv_label_set_text(sysUp, u);
    time_t nowt = time(nullptr);
    static bool tprinted = false;
    if (nowt > 1700000000) {   // hora já sincronizada via NTP
      struct tm tmv; localtime_r(&nowt, &tmv);
      char cb[24]; snprintf(cb, sizeof(cb), "%d/%d/%02d  %02d:%02d",
        tmv.tm_mday, tmv.tm_mon + 1, (tmv.tm_year + 1900) % 100, tmv.tm_hour, tmv.tm_min);
      for (int i = 0; i < nClocks; i++) lv_label_set_text(clocks[i], cb);
      if (!tprinted) { tprinted = true; Serial.printf("[Hora] %s\n", cb); }
    } else {
      static unsigned long lastDbg = 0;
      if (millis() - lastDbg > 3000) { lastDbg = millis(); Serial.printf("[NTP] aguardando sync (epoch=%ld)\n", (long)nowt); }
    }
  }

  // fetch periódico (antes do 1º sucesso, tenta a cada 5 s; depois, REFRESH_MS)
  unsigned long fetchIv = dataFromGatus ? REFRESH_MS : 5000UL;
  if (conn && (lastFetch == 0 || millis() - lastFetch > fetchIv)) {
    lastFetch = millis();
    if (fetchGatus()) {
      refreshUI();
      lastOkRefresh = millis();
      lv_label_set_text(sysRefr, "Atualizado: agora");
      if (splash) { lv_obj_del(splash); splash = nullptr; }
    } else {
      lv_label_set_text(sysRefr, "Atualizado: falha na leitura");
    }
  }
  delay(5);
}

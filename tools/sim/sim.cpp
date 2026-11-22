// Simulador do Desk Buddy: roda o setup()/loop() do firmware com LVGL de verdade, relógio simulado,
// toques e quedas de Wi-Fi roteirizados, e grava as telas em BMP (o png.mjs converte).
// Uso: tools/sim/run.sh (Docker). As telas saem em tools/sim/out/.
#include "Arduino.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "hw.h"
#include "config.h"
#include <lvgl.h>

static unsigned long simNow = 0;
extern "C" unsigned long millis(void) { return simNow; }
extern "C" void delay(unsigned long ms) { simNow += ms; }

SerialStub Serial;
EspStub ESP;
WiFiStub WiFi;
int simHttpCode = 200;
std::string simHttpFile = "/sim/gatus.json";
uint16_t simFb[320 * 240];
static bool restarted = false;
void EspStub::restart() { std::printf("%8lu  (sim) ESP.restart()\n", millis()); restarted = true; }

long map(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }
void pinMode(int, int) {}
static int led[40];
void digitalWrite(int pin, int v) { if (pin < 40) led[pin] = v; }
int analogRead(int) { return 2000; }
void ledcAttach(int, int, int) {}
void ledcWrite(int, int) {}
static const char* ledName() {   // active-LOW: R=4 G=16 B=17
  bool r = !led[4], g = !led[16], b = !led[17];
  return r ? "vermelho" : g ? "verde" : b ? "azul" : "apagado";
}

static bool tPressed = false; static int tX = 0, tY = 0;
bool XPT2046_Touchscreen::touched() { return tPressed; }
TS_Point XPT2046_Touchscreen::getPoint() {
  TS_Point p;
  p.x = TS_MINX + (long)tX * (TS_MAXX - TS_MINX) / 319 + 3;
  p.y = TS_MINY + (long)tY * (TS_MAXY - TS_MINY) / 239 + 3;
  p.z = 1000;
  return p;
}

void setup();
void loop();

static void run(unsigned long ms) { unsigned long end = simNow + ms; while (simNow < end && !restarted) loop(); }
static void tap(int x, int y) { tX = x; tY = y; tPressed = true; run(120); tPressed = false; run(400); }
static void drag(int x, int y0, int y1) {
  tX = x; tY = y0; tPressed = true; run(60);
  for (int i = 1; i <= 10; i++) { tY = y0 + (y1 - y0) * i / 10; run(40); }
  tPressed = false; run(1200);
}
static void shot(const char* name) {
  lv_obj_invalidate(lv_scr_act()); run(100);
  char path[128]; snprintf(path, sizeof(path), "/sim/out/%s.bmp", name);
  FILE* f = fopen(path, "wb");
  const int W = 320, H = 240; uint32_t sz = 54 + W * H * 3;
  uint8_t h[54] = { 'B', 'M' };
  h[2] = sz; h[3] = sz >> 8; h[4] = sz >> 16; h[5] = sz >> 24; h[10] = 54; h[14] = 40;
  h[18] = W & 255; h[19] = W >> 8; h[22] = H & 255; h[23] = H >> 8; h[26] = 1; h[28] = 24;
  fwrite(h, 1, 54, f);
  for (int y = H - 1; y >= 0; y--)
    for (int x = 0; x < W; x++) {
      uint16_t c = simFb[y * W + x];
      uint8_t px[3] = { (uint8_t)((c & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c >> 11) * 255 / 31) };
      fwrite(px, 1, 3, f);
    }
  fclose(f);
  std::printf("%8lu  (sim) tela %s  [LED %s]\n", millis(), name, ledName());
}

int main() {
  setvbuf(stdout, nullptr, _IOLBF, 0);

  if (std::getenv("SIM_FIRST_BOOT")) {
    std::puts("== 0. primeiro boot (sem Wi-Fi configurado): abre a Configuração e não tenta conectar");
    setup();
    run(20000); shot("00-primeiro-boot");
    tap(70, 220); shot("00-primeiro-boot-cancelado");
    std::printf("%8lu  (sim) WiFi.begin=%d\n", millis(), WiFi.begins);
    return 0;
  }

  std::puts("== 1. boot sem AP ao alcance: o vigia tenta de novo (15 s, 30 s, 60 s, com rádio na 4ª)");
  WiFi.apInRange = false;
  setup();
  run(13000); shot("01-inicio-sem-rede");
  run(170000);
  std::puts("== 2. o AP volta: a próxima tentativa conecta e a leitura sai na hora");
  WiFi.apInRange = true;
  run(62000); shot("02-inicio");

  std::puts("== 3. cada card abre a sua listagem; «Início» volta");
  const int cx[3] = { 56, 160, 264 }, cy[2] = { 83, 185 };
  const char* names[5] = { "03-clusters", "04-hosts", "05-servicos", "06-paineis", "07-gpu-servers" };
  for (int i = 0; i < 5; i++) {
    tap(cx[i % 3], cy[i / 3]); shot(names[i]);
    if (i == 1) { drag(160, 170, 60); shot("04-hosts-rolado"); }
    tap(160, 216);
  }
  shot("08-de-volta-ao-inicio");

  std::puts("== 4. dois itens fora do ar");
  simHttpFile = "/sim/build/gatus-down.json";
  run(61000); shot("09-inicio-com-falhas");
  tap(cx[2], cy[0]); shot("10-servicos-com-falha"); tap(160, 216);

  std::puts("== 5. Configurar");
  tap(cx[2], cy[1]); shot("11-configurar");
  drag(160, 160, 40); drag(160, 160, 40); shot("12-configurar-sistema");
  tap(70, 220); shot("13-cancelar-volta-ao-inicio");

  std::puts("== 6. queda do Wi-Fi sem reconexão do core (o caso que pedia o «Salvar»)");
  simHttpFile = "/sim/gatus.json";
  run(61000);
  WiFi.drop(8);
  run(5000); shot("14-inicio-wifi-caiu");
  run(15000); shot("15-inicio-reconectou");

  std::puts("== 7. Wi-Fi «conectado» mas sem resposta: 3 leituras e refaz a conexão; dados velhos em 3 min");
  simHttpCode = -1;
  run(200000); shot("16-inicio-offline");
  tap(cx[0], cy[0]); shot("17-clusters-offline"); tap(160, 216);
  std::puts("== 8. o servidor responde 502 (deploy do Gatus): não mexe no Wi-Fi");
  simHttpCode = 502;
  int b = WiFi.begins; run(120000);
  std::printf("%8lu  (sim) WiFi.begin durante o 502: %d\n", millis(), WiFi.begins - b);
  simHttpCode = 200;
  run(15000); shot("18-inicio-recuperado");

  std::puts("== 9. 15 min sem rede: reinicia");
  WiFi.apInRange = false; WiFi.drop(201);
  run(1000000);
  std::printf("%8lu  (sim) fim; reiniciou=%d; WiFi.begin=%d\n", millis(), restarted, WiFi.begins);
  return 0;
}

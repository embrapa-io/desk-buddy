# Desk Buddy — Documentação técnica

Guia para quem quer **compilar do código-fonte, customizar ou fazer fork** do firmware.
Para apenas usar o dispositivo, veja o [README](../README.md).

## Stack
Arduino (ESP32 core 3.3) + **TFT_eSPI** (ILI9341) + **LVGL 8.4** + **XPT2046_Touchscreen** +
**ArduinoJson 7** + **Preferences** (NVS), gerenciado pelo **PlatformIO**.

- Partição **`huge_app.csv`** (3 MB) — LVGL + TLS não cabem na padrão (1.25 MB).
- **`LV_MEM_CUSTOM = 1`** — LVGL usa o heap do sistema (a pool fixa estourava com splash + imagens + listas).

## Estrutura do repositório
```
platformio.ini            build (pinos do CYD via build_flags; partição huge_app)
include/config.h          Gatus, intervalo, calibração do touch, FW_VERSION, default de Wi-Fi (vazio)
include/lv_conf.h         configuração do LVGL
include/secrets.h         Wi-Fi local — GITIGNORED (opcional; só p/ a build do desenvolvedor)
src/main.cpp              app (drivers, telas, Gatus, configuração)
src/ui_font_12/14/16.c    fontes Montserrat (acentos PT-BR + ícones FontAwesome)
src/img_*.c               logos em bitmap LVGL
tools/                    geradores de assets (gen_logos.mjs, png2lvgl.py, gen_fonts.sh)
tools/sim/                simulador: roda o firmware no computador e grava as telas (Docker)
tools/release.sh          gera o binário público do gravador web (sem o secrets.h)
assets/                   logos em SVG (fonte dos bitmaps)
docs/                     README assets, mockup das telas (mockups.html)
```

## Compilar do código-fonte
1. Instale o [PlatformIO](https://platformio.org/).
2. (Opcional) Crie `include/secrets.h` para a build já conectar numa rede:
   ```c
   #pragma once
   #define WIFI_SSID     "Minha-Rede-2G"
   #define WIFI_PASSWORD "minhasenha"
   ```
   Sem ele, o aparelho sobe **sem rede e abre a tela de Configuração** no boot.
3. Compile e grave:
   ```bash
   pio run -t upload          # upload_speed 460800
   pio device monitor         # 115200
   ```
   Gravar assim **mantém a configuração** do aparelho (Wi-Fi, fuso, brilho): só o firmware é regravado.

O `platformio.ini` tem dois ambientes: **`cyd`** (o padrão, da bancada, que embute o `secrets.h`) e
**`release`** (o público, que ignora o `secrets.h`).

## Publicar o binário do gravador web
O [buddy.embrapa.io](https://buddy.embrapa.io) grava o `docs/firmware/desk-buddy.bin`, na versão do
`docs/manifest.json`. Depois de validar o firmware na placa:

```bash
tools/release.sh           # compila o env release, confere e grava o binário + a versão do manifesto
git add docs && git commit && git push    # o GitHub Pages publica
```

- **Nunca copie o firmware do env `cyd` para `docs/firmware`:** ele leva embutidos o nome e a senha do Wi-Fi
  do `secrets.h`. O script compila sem o arquivo e recusa a publicação se achar algum valor dele no binário.
- O binário é uma imagem única, gravada a partir do endereço 0 (bootloader, partições e app). Ela cobre a
  área da NVS (`0x9000`), então **atualizar pelo gravador web zera a configuração**: o aparelho volta a
  pedir o Wi-Fi. Para manter a configuração, a gravação é pelo PlatformIO.

## Gravação — pontos de atenção (CYD/CH340)
- **Conecte o CYD direto numa porta USB do computador** — dock/hub costuma **não enumerar** o CH340.
- Use um **cabo USB-C de dados** (não "charge-only").
- **A porta não aparece?** Então o computador não está vendo a placa (não é driver: o macOS já traz o do
  CH340). Troque o cabo, mesmo que ele funcione com outro aparelho; se a placa tiver também a micro-USB,
  tente por ela; e, num cabo USB-C↔USB-C, tente um USB-A→USB-C (há placas sem os resistores de
  identificação no conector USB-C, que só enumeram assim).
- O conector USB-C do **case** só alimenta (2 fios): a gravação é sempre pela porta USB da própria placa.
- Porta serial: `/dev/cu.usbserial-*` (macOS) — chip **CH340** (VID:PID `1A86:7523`).
- **`upload_speed = 460800`** — 921600 é instável no CH340 ("chip stopped responding").
- **Não** precisa segurar BOOT (auto-reset por DTR/RTS).
- `TFT_INVERSION_ON` é obrigatório nesse painel (senão as cores saem em negativo).

## Alimentação (energia)
- O CYD recebe 5 V por **USB** ou pelo **pino VIN** (header **P1**: `VIN – TX – RX – GND`).
- **Não há entrada de energia dedicada** além disso. O conector JST **P4 é o alto-falante** (GPIO26 + GND) — **não** ligar energia ali.
- **Conector USB-C fêmea (2 fios) no case:** `vermelho (VBUS) → VIN`, `preto (GND) → GND`. **Confira a polaridade com multímetro.**
- Fêmea USB-C "de 2 pinos": se não tiver **resistores CC**, um carregador **USB-C PD** pode não energizar → use **cabo USB-A→USB-C** ou uma fonte 5 V simples.

## Configuração em runtime (NVS)
A tela **Configurar** (teclado na tela) grava em NVS (namespace `deskbuddy`):
`ssid`, `pass`, `tz` (POSIX, ex. `<-04>4`), `refresh` (ms), `bright` (0-100), `autob`.
Os defaults vêm de `secrets.h`/`config.h` quando a NVS está vazia.

## Grupos do Gatus
Cada grupo de [status.embrapa.io](https://status.embrapa.io) tem um card no início e uma listagem. A
correspondência é pelo **nome do grupo** (`GRP_LABEL` em `main.cpp`: Clusters, Hosts, Serviços, Painéis,
GPU Servers); se um grupo for renomeado, vale a inicial, e o que não casar cai em Serviços. Grupo novo no
Gatus pede uma entrada no `enum Grp` e no `GRP_LABEL` (a grade do início comporta 5 grupos + Configurar).

## Wi-Fi: reconexão
O aparelho fica ligado por semanas, e o core do Arduino nem sempre volta sozinho depois de uma queda. O
vigia (`wifiWatchdog`, no `loop()`) cuida disso:

| Situação | O que faz |
|---|---|
| Sem conexão há 15 s | `WiFi.disconnect()` + `WiFi.begin()`; repete em 30 s e depois a cada 60 s |
| A cada 4 tentativas | desliga e religa o rádio antes de tentar |
| 15 min sem conexão | `ESP.restart()` (não reinicia com a Configuração aberta) |
| Wi-Fi «conectado», mas 3 leituras seguidas sem resposta | refaz a conexão (a leitura repete em 10 s; da 4ª falha em diante, em 30 s) |
| O servidor responde com erro (ex.: 502 num deploy) | só repete a leitura; o Wi-Fi não é tocado |

A economia de energia do rádio fica desligada (`WiFi.setSleep(false)`): o aparelho vive na tomada. O motivo
de cada desconexão sai no monitor serial (`[WiFi] desconectado (motivo N: …)`), e a Configuração mostra o
sinal (dBm) e o número de quedas desde que ligou.

**Dados velhos:** sem leitura válida há 3 intervalos (no mínimo 3 min), os cards trocam o `Healthy` pela
idade dos dados, o cabeçalho mostra `Offline` e o LED fica azul.

## Simulador
`tools/sim/run.sh` compila o `src/main.cpp` com o **LVGL de verdade** e stubs do Arduino (Wi-Fi, HTTP, tela,
toque, NVS), roda o roteiro de `tools/sim/sim.cpp` com relógio simulado (toques, queda do Wi-Fi, servidor fora)
e grava as telas em `tools/sim/out/*.png`. Só precisa de Docker e de um `pio run` anterior, que baixa as
bibliotecas. Não substitui o teste na placa: o rádio e o toque são de mentira.

## Decisões técnicas (e porquês)
| Tema | Decisão | Motivo |
|---|---|---|
| Hora | **header HTTP `Date`** da resposta do Gatus (não NTP) | `configTzTime` no `loop()` dá assert de UDP/LWIP no core 3.x |
| Acentos + ícones | `lv_font_conv`: Montserrat + Latin-1 + FontAwesome → `ui_font_*` | a fonte embutida do LVGL só tem ASCII |
| Logos | SVG → PNG (`@resvg/resvg-js`) → bitmap LVGL `TRUE_COLOR_ALPHA` | LVGL não renderiza SVG |
| Memória | `LV_MEM_CUSTOM = 1` (heap) | pool fixa do LVGL estourava (`LoadProhibited`) |
| Dados | Gatus `?page=1&pageSize=1` (~19 KB) + filtro do ArduinoJson, em HTTP/1.0 (sem «chunked»); resposta vazia ou de erro não apaga a tela | payload completo (~541 KB) é inviável |

## Regenerar assets
```bash
node tools/gen_logos.mjs && python3 tools/png2lvgl.py   # logos SVG -> bitmap LVGL (requer @resvg/resvg-js e Pillow)
bash tools/gen_fonts.sh                                  # fontes (acentos + ícones; requer um `pio run` antes p/ baixar as TTFs)
```

## Customizações comuns
- **Outra fonte de dados:** o app espera o formato da API do [Gatus](https://github.com/TwiN/gatus). Ajuste `GATUS_HOST`/`GATUS_PATH` em `config.h` e o parser em `fetchGatus()` (`main.cpp`).
- **Cores/tema:** macros `COL_*` no topo de `main.cpp`.
- **Telas:** `buildHome` / `buildListPage` / `buildConfig` em `main.cpp`.
- **Fusos disponíveis:** array `TZ_STR` / `TZ_LBL` em `main.cpp`.

## Mockup das telas
`docs/mockups.html` — protótipo HTML (320×240, escala 2×) da primeira versão, com menu lateral. O layout
atual (início com 6 cards) é conferido no simulador.

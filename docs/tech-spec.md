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

## Gravação — pontos de atenção (CYD/CH340)
- **Conecte o CYD direto numa porta USB do computador** — dock/hub costuma **não enumerar** o CH340.
- Use um **cabo USB-C de dados** (não "charge-only").
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
A tela **Sistema → Configurar** (teclado na tela) grava em NVS (namespace `deskbuddy`):
`ssid`, `pass`, `tz` (POSIX, ex. `<-04>4`), `refresh` (ms), `bright` (0-100), `autob`.
Os defaults vêm de `secrets.h`/`config.h` quando a NVS está vazia.

## Decisões técnicas (e porquês)
| Tema | Decisão | Motivo |
|---|---|---|
| Hora | **header HTTP `Date`** da resposta do Gatus (não NTP) | `configTzTime` no `loop()` dá assert de UDP/LWIP no core 3.x |
| Acentos + ícones | `lv_font_conv`: Montserrat + Latin-1 + FontAwesome → `ui_font_*` | a fonte embutida do LVGL só tem ASCII |
| Logos | SVG → PNG (`@resvg/resvg-js`) → bitmap LVGL `TRUE_COLOR_ALPHA` | LVGL não renderiza SVG |
| Memória | `LV_MEM_CUSTOM = 1` (heap) | pool fixa do LVGL estourava (`LoadProhibited`) |
| Dados | Gatus `?page=1&pageSize=1` (~14 KB) + filtro do ArduinoJson; retry 5 s até o 1º sucesso | payload completo (~541 KB) é inviável |

## Regenerar assets
```bash
node tools/gen_logos.mjs && python3 tools/png2lvgl.py   # logos SVG -> bitmap LVGL (requer @resvg/resvg-js e Pillow)
bash tools/gen_fonts.sh                                  # fontes (acentos + ícones; requer um `pio run` antes p/ baixar as TTFs)
```

## Customizações comuns
- **Outra fonte de dados:** o app espera o formato da API do [Gatus](https://github.com/TwiN/gatus). Ajuste `GATUS_HOST`/`GATUS_PATH` em `config.h` e o parser em `fetchGatus()` (`main.cpp`).
- **Cores/tema:** macros `COL_*` no topo de `main.cpp`.
- **Telas:** `buildOverview` / `buildListPage` / `buildSystem` / `buildConfig` em `main.cpp`.
- **Fusos disponíveis:** array `TZ_STR` / `TZ_LBL` em `main.cpp`.

## Mockup das telas
`docs/mockups.html` — protótipo HTML (320×240, escala 2×) usado para validar o layout antes de flashear.

# Desk Buddy — Painel de status Embrapa I/O (CYD/ESP32)

Painel de mesa que mostra a saúde da plataforma **Embrapa I/O** (clusters, hosts e serviços)
lendo a API do **Gatus** em [status.embrapa.io](https://status.embrapa.io), num **Cheap Yellow Display
(ESP32-2432S028R)**. Navegação por toque com **menu lateral de 5 telas**.

> **Status: v0 (mockup flasheável).** As telas, a navegação por toque e a leitura ao vivo do Gatus
> estão implementadas. É um ponto de partida para **validar em hardware** (compilar, flashear,
> e principalmente **calibrar o touch**). Documentação completa no Obsidian:
> `Projetos/Embrapa IO/Desk Buddy/`.

## Stack
Arduino + **TFT_eSPI** + **LVGL 8.3** + **XPT2046_Touchscreen** + **ArduinoJson 7** (via PlatformIO).

## Estrutura
```
platformio.ini        Config do build (pinos do CYD via build_flags — não precisa editar a TFT_eSPI)
include/config.h      WiFi, URL do Gatus, intervalo, calibração do touch  ← EDITAR
include/lv_conf.h     Configuração do LVGL
src/main.cpp          Drivers + UI (menu lateral, 5 telas) + fetch do Gatus
mockups/              Mockups visuais 320×240 (SVG + index.html) para revisar o layout
```

## Como compilar e flashear
1. Instale o [PlatformIO](https://platformio.org/) (extensão do VS Code ou CLI `pio`).
2. Edite **`include/config.h`**: `WIFI_SSID`, `WIFI_PASSWORD` (rede **2,4 GHz** WPA2).
3. Conecte o CYD por USB. No macOS pode ser necessário o **driver CH340**.
4. Compile e grave:
   ```bash
   pio run -t upload
   pio device monitor      # 115200 — mostra logs e x/y crus do touch
   ```

## Calibração do touch
Os valores `TS_MINX/MAXX/MINY/MAXY` em `config.h` são aproximados. Ao tocar nos cantos da tela,
observe os valores crus no Serial Monitor e ajuste até o toque casar com os botões.

## As 5 telas
1. **Geral** — tiles Clusters / Hosts / Serviços (up/total) + veredito global.
2. **Clusters** (6) · 3. **Hosts** (12) · 4. **Serviços** (25) — listas com status + latência (fora do ar no topo).
5. **Sistema** — WiFi/IP, uptime, última atualização, versão.

O **LED RGB** da placa funciona como farol: 🟢 tudo no ar · 🔴 algo fora.

## Dados (Gatus)
`GET https://status.embrapa.io/api/v1/endpoints/statuses?page=1&pageSize=1` → ~14 KB,
parseado com **filtro do ArduinoJson** (só `name`, `group`, `success`, `duration`). Refresh a cada 60 s.

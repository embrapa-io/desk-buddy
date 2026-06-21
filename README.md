# Desk Buddy — Painel de status Embrapa I/O (CYD/ESP32)

Dispositivo de mesa que mostra a **saúde da plataforma Embrapa I/O** (clusters, hosts e serviços)
lendo a API do **Gatus** em [status.embrapa.io](https://status.embrapa.io), num **Cheap Yellow Display
(ESP32-2432S028R)**. Navegação por toque, splash com a marca e **configuração on-screen**.

> **Status:** v1 funcional no hardware (`0.26.6-alpha.1`).

## Recursos
- **Dashboard ao vivo** do Gatus: Visão Geral (tiles), Clusters, Hosts, Serviços — status + latência (`Healthy` / `N off`).
- **Header** com logo `embrapa.io` + relógio/data (hora obtida do header HTTP `Date` do Gatus, fuso configurável).
- **Splash** no boot com as logos Embrapa / embrapa.io.
- **Tela Sistema → Configurar** (teclado na tela): Wi-Fi, fuso, intervalo e brilho — persistidos em **NVS**.
- **Brilho** por PWM + automático via LDR. **LED RGB** como farol de status.

## Stack
Arduino (ESP32 core 3.3) + **TFT_eSPI** + **LVGL 8.4** + **XPT2046_Touchscreen** + **ArduinoJson 7** + **Preferences** (PlatformIO).

## Estrutura
```
platformio.ini        build (pinos do CYD via build_flags; partição huge_app)
include/config.h      Gatus, intervalo, calibração do touch, versão
include/lv_conf.h     configuração do LVGL (heap do sistema)
include/secrets.h     Wi-Fi local — GITIGNORED (opcional; default é em branco)
src/main.cpp          app (drivers, telas, Gatus, configuração)
src/ui_font_*.c       fontes Montserrat (acentos PT-BR + ícones)
src/img_*.c           logos em bitmap LVGL
tools/                geradores de assets (logos, fontes)
mockups/index.html    mockup visual das telas
```

## Compilar e gravar
1. Instale o [PlatformIO](https://platformio.org/).
2. (Opcional) Crie `include/secrets.h` com `WIFI_SSID`/`WIFI_PASSWORD` para a build conectar direto.
   Sem ele, o aparelho sobe **sem rede e abre a tela de Configuração** para você informar o Wi-Fi pelo toque.
3. Conecte o CYD **direto numa porta USB do computador** (dock/hub pode não enumerar o CH340), com um **cabo USB-C de dados**.
4. Grave:
   ```bash
   pio run -t upload          # upload_speed 460800 (CH340)
   pio device monitor         # 115200
   ```
   Não precisa segurar BOOT (auto-reset).

## Regenerar assets (opcional)
```bash
node tools/gen_logos.mjs && python3 tools/png2lvgl.py   # logos SVG -> bitmap LVGL
bash tools/gen_fonts.sh                                  # fontes (acentos + ícones)
```

## Dados (Gatus)
`GET /api/v1/endpoints/statuses?page=1&pageSize=1` (~14 KB) → filtro do ArduinoJson (só `name`, `group`, `success`, `duration`). Refresh configurável.

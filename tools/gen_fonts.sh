#!/usr/bin/env bash
# Gera as fontes Montserrat com acentos PT-BR (Latin-1) + símbolos LVGL (FontAwesome)
# + ícones extras: relógio (F017), globo (F0AC), microchip (F2DB).
# Requer as TTFs/woff que vêm no pacote do LVGL (após um `pio run`).
set -e
cd "$(dirname "$0")/.."
TTF=".pio/libdeps/cyd/lvgl/scripts/built_in_font/Montserrat-Medium.ttf"
FA=".pio/libdeps/cyd/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"

# conjunto padrão de símbolos do LVGL + extras (61463=relógio, 61612=globo, 62171=chip, 61530=info)
SYMS="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650,61463,61612,62171,61530"

for S in 12 14 16; do
  npx -y lv_font_conv --bpp 4 --size "$S" --no-compress --format lvgl --lv-include lvgl.h \
    --font "$TTF" -r 0x20-0x7F -r 0xA0-0xFF \
    --font "$FA" -r "$SYMS" \
    -o "src/ui_font_$S.c"
  echo "gerado src/ui_font_$S.c"
done

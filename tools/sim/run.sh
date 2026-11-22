#!/bin/sh
# Simulador do Desk Buddy: roda o firmware (src/main.cpp) no computador, com o LVGL de verdade, e grava as telas
# em tools/sim/out/*.png. Serve para conferir o layout e a reconexão do Wi-Fi sem a placa. Só precisa de Docker
# e de um `pio run` anterior (que baixa as bibliotecas para .pio/libdeps).
#
#   tools/sim/run.sh          compila e roda o roteiro (tools/sim/sim.cpp)
#   tools/sim/run.sh clean    apaga a compilação e as telas
set -e
cd "$(dirname "$0")/../.."
if [ "${1:-}" = clean ]; then rm -rf tools/sim/build tools/sim/out; exit 0; fi
docker run --rm -v "$PWD:/src:ro" -v "$PWD/tools/sim:/sim" node:22 sh /sim/build.sh

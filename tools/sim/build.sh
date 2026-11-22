#!/bin/sh
# Roda DENTRO do contêiner (veja run.sh): compila o LVGL de verdade + src/main.cpp + stubs e executa o roteiro.
set -e
L=/src/.pio/libdeps/cyd
[ -d "$L/lvgl/src" ] || { echo "Faltam as bibliotecas: rode 'pio run' uma vez antes (baixa o LVGL e o ArduinoJson)."; exit 1; }
INC="-I/sim/stubs -I/src/include -I$L/lvgl -I$L/lvgl/src -I$L/ArduinoJson/src -DLV_CONF_INCLUDE_SIMPLE -DPUBLIC_BUILD=1"
mkdir -p /sim/build/lv /sim/out
if [ ! -f /sim/build/liblvgl.a ]; then
  find "$L/lvgl/src" -name '*.c' | xargs -P 8 -I{} sh -c 'gcc -O1 -w '"$INC"' -c "$1" -o /sim/build/lv/$(echo "$1" | md5sum | cut -c1-12).o' _ {}
  ar rcs /sim/build/liblvgl.a /sim/build/lv/*.o
fi
for f in /src/src/*.c; do gcc -O1 -w $INC -c "$f" -o "/sim/build/$(basename "$f").o"; done
g++ -std=gnu++17 -O1 -Wall -Wno-unused $INC -c /src/src/main.cpp -o /sim/build/main.o
g++ -std=gnu++17 -O1 -Wall $INC -c /sim/sim.cpp -o /sim/build/sim.o
g++ -o /sim/build/sim /sim/build/main.o /sim/build/sim.o /sim/build/*.c.o /sim/build/liblvgl.a -lm
# variante da resposta do Gatus com dois itens fora do ar
node -e 'const fs=require("fs");const d=JSON.parse(fs.readFileSync("/sim/gatus.json","utf8"));for(const e of d)if(e.name==="storage.agro.rocks"||e.name==="Sentry")e.results[0].success=false;fs.writeFileSync("/sim/build/gatus-down.json",JSON.stringify(d));'
rm -f /sim/out/*.bmp /sim/out/*.png
/sim/build/sim
SIM_FIRST_BOOT=1 /sim/build/sim
node /sim/png.mjs /sim/out

#!/bin/bash
# Gera o binário público do gravador web (buddy.embrapa.io) e atualiza a versão do manifesto.
#
# Quando usar: depois de validar o firmware na placa, para publicar a versão do include/config.h (FW_VERSION).
# O binário sai do env «release» do PlatformIO, que ignora o include/secrets.h. NUNCA copie o firmware do env
# «cyd» para docs/firmware: ele leva embutidos o nome e a senha do Wi-Fi da bancada.
#
#   tools/release.sh          compila, confere e grava docs/firmware/desk-buddy.bin + docs/manifest.json
#
# Não faz commit nem push: confira o `git diff --stat` e publique.
set -euo pipefail
cd "$(dirname "$0")/.."

[ -f platformio.ini ] && [ -f include/config.h ] || { echo "Rode a partir do repositório do Desk Buddy." >&2; exit 1; }
command -v pio > /dev/null || { echo "PlatformIO (pio) não encontrado." >&2; exit 1; }

version=$(sed -n 's/^#define FW_VERSION "\(.*\)"$/\1/p' include/config.h)
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[a-z]+\.[0-9]+)?$ ]] || { echo "FW_VERSION inválida em include/config.h: '$version'" >&2; exit 1; }

echo "Compilando o env release (versão $version)..."
pio run -e release > /dev/null
bin=.pio/build/release/firmware.factory.bin
[ -s "$bin" ] || { echo "Não achei $bin." >&2; exit 1; }

# Confere que nenhum valor do secrets.h foi parar no binário (os valores não aparecem na tela).
python3 -I - "$bin" "$version" <<'PY'
import re, sys, os
data = open(sys.argv[1], 'rb').read()
if sys.argv[2].encode() not in data:
    sys.exit('A versão %s não está no binário: build desatualizada?' % sys.argv[2])
if os.path.exists('include/secrets.h'):
    src = open('include/secrets.h', encoding='utf-8').read()
    for name, value in re.findall(r'#define\s+(\w+)\s+"([^"]+)"', src):
        if value.encode() in data:
            sys.exit('O binário contém o valor de %s (include/secrets.h). Nada foi publicado.' % name)
PY

cp "$bin" docs/firmware/desk-buddy.bin
python3 -I - "$version" <<'PY'
import re, sys
path = 'docs/manifest.json'
text = open(path, encoding='utf-8').read()
new, n = re.subn(r'("version":\s*")[^"]*(")', lambda m: m.group(1) + sys.argv[1] + m.group(2), text, count=1)
if n != 1:
    sys.exit('Não achei o campo "version" em docs/manifest.json.')
open(path, 'w', encoding='utf-8').write(new)
PY

echo "Pronto: docs/firmware/desk-buddy.bin ($(wc -c < docs/firmware/desk-buddy.bin | tr -d ' ') bytes) e docs/manifest.json na versão $version."
echo "Sem Wi-Fi embutido. Falta o commit e o push para o GitHub Pages publicar."

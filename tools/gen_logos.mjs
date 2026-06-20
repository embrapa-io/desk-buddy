// Rasteriza os SVGs dos logos para PNG (com transparência) em tamanhos-alvo.
// Uso: node tools/gen_logos.mjs   (requer @resvg/resvg-js)
import { Resvg } from '@resvg/resvg-js';
import { readFileSync, writeFileSync } from 'node:fs';

const jobs = [
  ['assets/embrapa.io-white.svg', 'tools/logo_header.png', 107], // header (branca, fundo escuro)
  ['assets/embrapa.io.svg',       'tools/logo_io.png',     216], // splash (colorida)
  ['assets/embrapa.svg',          'tools/logo_embrapa.png', 121], // splash (colorida)
];

for (const [src, out, w] of jobs) {
  const svg = readFileSync(src);
  const r = new Resvg(svg, { fitTo: { mode: 'width', value: w } }); // fundo transparente (default)
  const png = r.render().asPng();
  writeFileSync(out, png);
  console.log('render:', out, 'w=' + w);
}

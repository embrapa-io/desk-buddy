// Converte as telas BMP (320x240, 24 bits) do simulador em PNG ampliado 2x, sem dependências.
import fs from 'fs';
import zlib from 'zlib';

const dir = process.argv[2];
const W = 320, H = 240, Z = 2;
const table = Array.from({ length: 256 }, (_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
const crc = (b) => { let c = 0xFFFFFFFF; for (const x of b) c = table[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
const chunk = (type, data) => {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type), data]);
  const sum = Buffer.alloc(4); sum.writeUInt32BE(crc(body));
  return Buffer.concat([len, body, sum]);
};

for (const f of fs.readdirSync(dir).filter((n) => n.endsWith('.bmp')).sort()) {
  const bmp = fs.readFileSync(`${dir}/${f}`);
  const raw = Buffer.alloc((W * Z * 3 + 1) * H * Z);
  for (let y = 0; y < H * Z; y++) {
    const row = y * (W * Z * 3 + 1);
    raw[row] = 0;   // filtro «None»
    for (let x = 0; x < W * Z; x++) {
      const src = 54 + ((H - 1 - (y >> 1)) * W + (x >> 1)) * 3;   // BMP: de baixo para cima, BGR
      raw[row + 1 + x * 3] = bmp[src + 2]; raw[row + 2 + x * 3] = bmp[src + 1]; raw[row + 3 + x * 3] = bmp[src];
    }
  }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(W * Z, 0); ihdr.writeUInt32BE(H * Z, 4); ihdr[8] = 8; ihdr[9] = 2;
  fs.writeFileSync(`${dir}/${f.replace('.bmp', '.png')}`, Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0)),
  ]));
  fs.unlinkSync(`${dir}/${f}`);
}

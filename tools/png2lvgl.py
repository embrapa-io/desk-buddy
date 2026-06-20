# Converte PNG RGBA -> imagem LVGL (TRUE_COLOR_ALPHA, 16-bit) em arquivo .c
# Uso: python3 tools/png2lvgl.py   (requer Pillow)
from PIL import Image

JOBS = [
    ("tools/logo_header.png",  "img_logo_header",   "src/img_logo_header.c"),
    ("tools/logo_io.png",      "img_io_color",      "src/img_io_color.c"),
    ("tools/logo_embrapa.png", "img_embrapa_color", "src/img_embrapa_color.c"),
]

for png, name, out in JOBS:
    im = Image.open(png).convert("RGBA")
    w, h = im.size
    px = im.load()
    data = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)  # RGB565
            data += bytes([c & 0xFF, (c >> 8) & 0xFF, a])       # LE color + alpha
    with open(out, "w") as f:
        f.write('#include "lvgl.h"\n\n')
        f.write("#ifndef LV_ATTRIBUTE_MEM_ALIGN\n#define LV_ATTRIBUTE_MEM_ALIGN\n#endif\n\n")
        f.write(f"const LV_ATTRIBUTE_MEM_ALIGN uint8_t {name}_map[] = {{\n")
        f.write(",".join(str(v) for v in data))
        f.write("\n};\n\n")
        f.write(f"const lv_img_dsc_t {name} = {{\n")
        f.write("  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,\n")
        f.write("  .header.always_zero = 0,\n  .header.reserved = 0,\n")
        f.write(f"  .header.w = {w},\n  .header.h = {h},\n")
        f.write(f"  .data_size = {w * h * 3},\n  .data = {name}_map,\n}};\n")
    print(f"wrote {out}  ({w}x{h}, {len(data)} bytes)")

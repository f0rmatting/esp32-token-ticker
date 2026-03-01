import struct
from PIL import Image

# LVGL 9 constants
LV_IMAGE_HEADER_MAGIC = 0x19
LV_COLOR_FORMAT_ARGB8888 = 0x10

OUTPUT_C = '/Users/bruce/work/moodland/crypto_monitor/main/crypto_logos.c'
OUTPUT_H = '/Users/bruce/work/moodland/crypto_monitor/main/crypto_logos.h'

SPDX = (
    '/*\n'
    ' * SPDX-FileCopyrightText: 2025-2026 Bruce\n'
    ' * SPDX-License-Identifier: CC-BY-NC-4.0\n'
    ' */\n'
)

logos = [
    ('bitcoin.png',   'img_bitcoin_logo',   36),
    ('ethereum.png',  'img_ethereum_logo',  36),
    ('paxg.png',      'img_paxg_logo',      36),
    ('chainbase.png', 'img_chainbase_logo', 36),
    ('sui.png',       'img_sui_logo',       36),
    ('doge.png',      'img_doge_logo',      36),
    ('solana.png',    'img_solana_logo',    36),
    ('tron.png',      'img_tron_logo',      36),
    ('usdc.png',      'img_usdc_logo',      36),
    ('usdt.png',      'img_usdt_logo',      36),
]

# ── Generate .c file ──────────────────────────────────────────
combined = []
combined.append(SPDX)
combined.append('// Auto-generated crypto token logos for LVGL 9')
combined.append('#include "lvgl.h"')
combined.append('')

for png, var, sz in logos:
    img = Image.open(png).convert("RGBA").resize((sz, sz), Image.LANCZOS)
    w, h = img.size
    stride = w * 4

    pixels = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b, a = img.getpixel((x, y))
            pixels.extend([b, g, r, a])

    combined.append(f'// {png} -> {w}x{h} ARGB8888')
    combined.append(f'static const uint8_t {var}_data[] = {{')
    for y in range(h):
        row_start = y * stride
        row_bytes = pixels[row_start:row_start + stride]
        hex_str = ', '.join(f'0x{b:02x}' for b in row_bytes)
        combined.append(f'    {hex_str},')
    combined.append(f'}};')
    combined.append(f'')
    combined.append(f'const lv_image_dsc_t {var} = {{')
    combined.append(f'    .header = {{')
    combined.append(f'        .magic = LV_IMAGE_HEADER_MAGIC,')
    combined.append(f'        .cf = LV_COLOR_FORMAT_ARGB8888,')
    combined.append(f'        .w = {w},')
    combined.append(f'        .h = {h},')
    combined.append(f'        .stride = {stride},')
    combined.append(f'    }},')
    combined.append(f'    .data_size = sizeof({var}_data),')
    combined.append(f'    .data = {var}_data,')
    combined.append(f'}};')
    combined.append(f'')

output_c = '\n'.join(combined) + '\n'
with open(OUTPUT_C, 'w') as f:
    f.write(output_c)

# ── Generate .h file ──────────────────────────────────────────
h_lines = []
h_lines.append(SPDX)
h_lines.append('#pragma once')
h_lines.append('')
h_lines.append('#include "lvgl.h"')
h_lines.append('')
for _, var, _ in logos:
    h_lines.append(f'extern const lv_image_dsc_t {var};')
h_lines.append('')

output_h = '\n'.join(h_lines) + '\n'
with open(OUTPUT_H, 'w') as f:
    f.write(output_h)

print(f'Generated {OUTPUT_C} ({len(output_c)} bytes)')
print(f'Generated {OUTPUT_H} ({len(output_h)} bytes)')
for png, var, sz in logos:
    print(f'  {var}: {sz}x{sz} ARGB8888 = {sz*sz*4} bytes')

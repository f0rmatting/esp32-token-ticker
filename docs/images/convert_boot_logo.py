#!/usr/bin/env python3
"""Convert layout_lean_version.png to LVGL 9 RGB565 C array for boot splash."""

import os
import struct
from PIL import Image

# LVGL 9 constants
LV_IMAGE_HEADER_MAGIC = 0x19
LV_COLOR_FORMAT_RGB565 = 0x12

TARGET_W = 320
TARGET_H = 172

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
INPUT_PNG = os.path.join(SCRIPT_DIR, "layout_lean_version.png")
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "..", "..", "main")
OUTPUT_C = os.path.join(OUTPUT_DIR, "boot_logo.c")
OUTPUT_H = os.path.join(OUTPUT_DIR, "boot_logo.h")


def rgb888_to_rgb565(r, g, b):
    """Convert 8-bit RGB to 16-bit RGB565."""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def main():
    img = Image.open(INPUT_PNG).convert("RGB").resize(
        (TARGET_W, TARGET_H), Image.LANCZOS
    )
    w, h = img.size
    stride = w * 2  # RGB565: 2 bytes per pixel

    # Build pixel data (little-endian RGB565)
    pixels = bytearray()
    for y in range(h):
        for x in range(w):
            r, g, b = img.getpixel((x, y))
            val = rgb888_to_rgb565(r, g, b)
            pixels.extend(struct.pack("<H", val))

    # Generate .c file
    lines = []
    lines.append("// Auto-generated boot logo, {}x{} RGB565".format(w, h))
    lines.append("// Do not edit — regenerate with docs/images/convert_boot_logo.py")
    lines.append('#include "boot_logo.h"')
    lines.append("")
    lines.append("static const uint8_t boot_logo_data[] = {")
    for y in range(h):
        row_start = y * stride
        row_bytes = pixels[row_start : row_start + stride]
        hex_str = ", ".join("0x{:02x}".format(b) for b in row_bytes)
        lines.append("    {},".format(hex_str))
    lines.append("};")
    lines.append("")
    lines.append("const lv_image_dsc_t boot_logo_dsc = {")
    lines.append("    .header = {")
    lines.append("        .magic = LV_IMAGE_HEADER_MAGIC,")
    lines.append("        .cf = LV_COLOR_FORMAT_RGB565,")
    lines.append("        .w = {},".format(w))
    lines.append("        .h = {},".format(h))
    lines.append("        .stride = {},".format(stride))
    lines.append("    },")
    lines.append("    .data_size = sizeof(boot_logo_data),")
    lines.append("    .data = boot_logo_data,")
    lines.append("};")
    lines.append("")

    with open(OUTPUT_C, "w") as f:
        f.write("\n".join(lines))
    print("Generated {} ({} bytes, {}x{} RGB565)".format(OUTPUT_C, len(pixels), w, h))

    # Generate .h file
    header = []
    header.append("#pragma once")
    header.append('#include "lvgl.h"')
    header.append("")
    header.append("extern const lv_image_dsc_t boot_logo_dsc;")
    header.append("")

    with open(OUTPUT_H, "w") as f:
        f.write("\n".join(header))
    print("Generated {}".format(OUTPUT_H))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Generate the Sparkle Bot UI assets for the LCDWIKI ES3C28P board.

Writes ../sparkle_assets.c and ../sparkle_assets.h:
  * LVGL 9 bitmap fonts (4 bpp, Vietnamese coverage) rendered with FreeType
    from Be Vietnam Pro and Baloo 2 (both SIL OFL 1.1, Google Fonts).
  * A8 glow / sparkle masks. Blur and glow are baked here so the device only
    blends a pre-computed alpha mask instead of running LVGL shadow blur.

Usage (needs `pip install freetype-py pillow numpy`):
  gen_sparkle_assets.py --bvp-regular BeVietnamPro-Regular.ttf \
      --bvp-semibold BeVietnamPro-SemiBold.ttf --baloo Baloo2[wght].ttf

Fonts: https://github.com/google/fonts/tree/main/ofl/bevietnampro
       https://github.com/google/fonts/tree/main/ofl/baloo2
"""

import argparse
import math
import os

import freetype
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_C = os.path.join(HERE, "..", "sparkle_assets.c")
OUT_H = os.path.join(HERE, "..", "sparkle_assets.h")

# Vietnamese text plus the typography used by the design
VIETNAMESE = (
    list(range(0x20, 0x7F))
    + list(range(0xA0, 0x100))
    + [0x102, 0x103, 0x110, 0x111, 0x128, 0x129, 0x168, 0x169, 0x1A0, 0x1A1, 0x1AF, 0x1B0]
    + list(range(0x1EA0, 0x1EFA))
    + [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026]
)
CLOCK = [ord(c) for c in "0123456789:-"]


# --------------------------------------------------------------------------- fonts

def render_font(path, px, codepoints, weight=None):
    face = freetype.Face(path)
    if weight is not None:
        face.set_var_design_coords((weight,))
    face.set_pixel_sizes(0, px)
    ascender = math.ceil(face.size.ascender / 64)
    descender = math.floor(face.size.descender / 64)  # negative
    glyphs = []
    for cp in sorted(set(codepoints)):
        if face.get_char_index(cp) == 0:
            continue
        face.load_char(cp, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT)
        g = face.glyph
        bmp = g.bitmap
        w, h = bmp.width, bmp.rows
        if w and h:
            pixels = np.array(bmp.buffer, dtype=np.uint8).reshape(h, bmp.pitch)[:, :w]
        else:
            pixels = np.zeros((0, 0), dtype=np.uint8)
        glyphs.append({
            "cp": cp,
            "adv": int(round(g.advance.x / 64 * 16)),
            "w": w,
            "h": h,
            "ofs_x": g.bitmap_left,
            "ofs_y": g.bitmap_top - h,
            "px": pixels,
        })
    # Fit the line box to the glyphs we actually ship (stacked Vietnamese
    # diacritics included) instead of the font's roomier global metrics
    inked = [g for g in glyphs if g["h"]]
    ascender = max(ascender if weight is None else 0, max(g["ofs_y"] + g["h"] for g in inked))
    descender = min(g["ofs_y"] for g in inked)
    return glyphs, ascender - descender, -descender


def pack_4bpp(px):
    vals = [(int(v) * 15 + 127) // 255 for v in px.flatten()]
    if len(vals) % 2:
        vals.append(0)
    return [(vals[i] << 4) | vals[i + 1] for i in range(0, len(vals), 2)]


def cmap_ranges(cps):
    ranges = []
    start = prev = cps[0]
    for cp in cps[1:]:
        if cp != prev + 1:
            ranges.append((start, prev))
            start = cp
        prev = cp
    ranges.append((start, prev))
    return ranges


def emit_font(name, glyphs, line_height, base_line):
    out = [f"/* {name}: {len(glyphs)} glyphs, 4 bpp */"]
    bitmap, dsc = [], ["    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}"]
    for g in glyphs:
        index = len(bitmap)
        if g["w"] and g["h"]:
            bitmap.extend(pack_4bpp(g["px"]))
        dsc.append(
            f"    {{.bitmap_index = {index}, .adv_w = {g['adv']}, .box_w = {g['w']}, "
            f".box_h = {g['h']}, .ofs_x = {g['ofs_x']}, .ofs_y = {g['ofs_y']}}}")
    out.append(f"static LV_ATTRIBUTE_LARGE_CONST const uint8_t {name}_bitmap[] = {{")
    for i in range(0, max(len(bitmap), 1), 16):
        out.append("    " + ", ".join(f"0x{b:02x}" for b in (bitmap[i:i + 16] or [0])) + ",")
    out.append("};")
    out.append(f"static const lv_font_fmt_txt_glyph_dsc_t {name}_glyph_dsc[] = {{")
    out.append(",\n".join(dsc))
    out.append("};")

    cps = [g["cp"] for g in glyphs]
    out.append(f"static const lv_font_fmt_txt_cmap_t {name}_cmaps[] = {{")
    glyph_id = 1
    ranges = cmap_ranges(cps)
    for first, last in ranges:
        length = last - first + 1
        out.append(
            f"    {{.range_start = {first}, .range_length = {length}, .glyph_id_start = {glyph_id}, "
            ".unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, "
            ".type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},")
        glyph_id += length
    out.append("};")
    out.append(f"static const lv_font_fmt_txt_dsc_t {name}_dsc = {{")
    out.append(f"    .glyph_bitmap = {name}_bitmap,")
    out.append(f"    .glyph_dsc = {name}_glyph_dsc,")
    out.append(f"    .cmaps = {name}_cmaps,")
    out.append("    .kern_dsc = NULL,")
    out.append("    .kern_scale = 0,")
    out.append(f"    .cmap_num = {len(ranges)},")
    out.append("    .bpp = 4,")
    out.append("    .kern_classes = 0,")
    out.append("    .bitmap_format = LV_FONT_FMT_TXT_PLAIN,")
    out.append("};")
    # Not const: the UI sets .fallback at runtime for characters outside this set
    out.append(f"lv_font_t {name} = {{")
    out.append("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    out.append("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    out.append(f"    .line_height = {line_height},")
    out.append(f"    .base_line = {base_line},")
    out.append("    .subpx = LV_FONT_SUBPX_NONE,")
    out.append("    .underline_position = -1,")
    out.append("    .underline_thickness = 1,")
    out.append(f"    .dsc = &{name}_dsc,")
    out.append("    .fallback = NULL,")
    out.append("    .user_data = NULL,")
    out.append("};")
    return "\n".join(out), len(bitmap)


# --------------------------------------------------------------------------- masks

def glow_ellipse(w, h, blur, strength):
    """Soft halo around a w x h ellipse (CSS box-shadow: 0 0 <blur>px)."""
    pad = blur + 2
    img = Image.new("L", (w + 2 * pad, h + 2 * pad), 0)
    ImageDraw.Draw(img).ellipse([pad, pad, pad + w - 1, pad + h - 1], fill=255)
    img = img.filter(ImageFilter.GaussianBlur(blur / 2))
    return (np.asarray(img, dtype=np.float32) * strength).clip(0, 255).astype(np.uint8)


def radial_glow(size, strength):
    """Radial gradient that fades to zero at 70 % of the radius."""
    c = (size - 1) / 2
    y, x = np.mgrid[0:size, 0:size]
    r = np.sqrt((x - c) ** 2 + (y - c) ** 2) / (size / 2)
    a = np.clip(1 - r / 0.7, 0, 1) ** 1.6
    return (a * 255 * strength).astype(np.uint8)


def sparkle(size, blur=0):
    """The design's 4-point star (SVG path M12 0C13 8 16 11 24 12 ...)."""
    scale = 8
    big = size * scale
    pad = blur * scale
    img = Image.new("L", (big + 2 * pad, big + 2 * pad), 0)
    d = ImageDraw.Draw(img)

    def bez(p0, p1, p2, p3, n=24):
        return [((1 - t) ** 3 * p0[0] + 3 * (1 - t) ** 2 * t * p1[0] + 3 * (1 - t) * t * t * p2[0]
                 + t ** 3 * p3[0],
                 (1 - t) ** 3 * p0[1] + 3 * (1 - t) ** 2 * t * p1[1] + 3 * (1 - t) * t * t * p2[1]
                 + t ** 3 * p3[1]) for t in (i / n for i in range(n + 1))]

    pts = (bez((12, 0), (13, 8), (16, 11), (24, 12)) + bez((24, 12), (16, 13), (13, 16), (12, 24))
           + bez((12, 24), (11, 16), (8, 13), (0, 12)) + bez((0, 12), (8, 11), (11, 8), (12, 0)))
    k = big / 24
    d.polygon([(pad + x * k, pad + y * k) for x, y in pts], fill=255)
    if blur:
        halo = img.filter(ImageFilter.GaussianBlur(blur * scale / 2))
        img = Image.fromarray(np.maximum(np.asarray(img), np.asarray(halo) * 0.9).astype(np.uint8))
    img = img.resize(((size + 2 * blur), (size + 2 * blur)), Image.LANCZOS)
    return np.asarray(img, dtype=np.uint8)


def dashed_ring(diameter, dash_deg=6, gap_deg=6):
    """1 px dashed circle (CSS border: 1px dashed)."""
    scale = 4
    size = (diameter + 2) * scale
    img = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(img)
    box = [scale, scale, size - scale - 1, size - scale - 1]
    start = 0
    while start < 360:
        d.arc(box, start, start + dash_deg, fill=255, width=scale)
        start += dash_deg + gap_deg
    img = img.resize((diameter + 2, diameter + 2), Image.LANCZOS)
    return np.asarray(img, dtype=np.uint8)


def emit_mask(name, a):
    h, w = a.shape
    out = [f"static const uint8_t {name}_map[] = {{"]
    flat = a.flatten()
    for i in range(0, len(flat), 24):
        out.append("    " + ",".join(str(int(v)) for v in flat[i:i + 24]) + ",")
    out.append("};")
    out.append(f"const lv_image_dsc_t {name} = {{")
    out.append("    .header = {")
    out.append("        .magic = LV_IMAGE_HEADER_MAGIC,")
    out.append("        .cf = LV_COLOR_FORMAT_A8,")
    out.append("        .flags = 0,")
    out.append(f"        .w = {w},")
    out.append(f"        .h = {h},")
    out.append(f"        .stride = {w},")
    out.append("    },")
    out.append(f"    .data_size = sizeof({name}_map),")
    out.append(f"    .data = {name}_map,")
    out.append("};")
    return "\n".join(out), w * h


# --------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bvp-regular", required=True)
    ap.add_argument("--bvp-semibold", required=True)
    ap.add_argument("--baloo", required=True, help="Baloo2[wght].ttf variable font")
    args = ap.parse_args()

    fonts = [
        ("sparkle_font_text_12", args.bvp_regular, 12, VIETNAMESE, None),
        ("sparkle_font_text_10", args.bvp_semibold, 10, VIETNAMESE, None),
        ("sparkle_font_title_12", args.baloo, 12, VIETNAMESE, 700),
        ("sparkle_font_title_20", args.baloo, 20, VIETNAMESE, 800),
        ("sparkle_font_clock_50", args.baloo, 50, CLOCK, 800),
    ]
    masks = [
        # Face halos (box-shadow 0 0 22px) per screen size
        ("sparkle_glow_face_l", glow_ellipse(122, 114, 22, 0.5)),
        ("sparkle_glow_face_m", glow_ellipse(106, 100, 22, 0.55)),
        ("sparkle_glow_face_s", glow_ellipse(98, 92, 22, 0.6)),
        ("sparkle_glow_face_xs", glow_ellipse(76, 72, 18, 0.5)),
        # Eye / pupil halos (box-shadow 0 0 12px / 8px)
        ("sparkle_glow_eye_tall", glow_ellipse(19, 24, 12, 1.0)),
        ("sparkle_glow_eye_round", glow_ellipse(22, 22, 12, 1.0)),
        ("sparkle_glow_pupil", glow_ellipse(9, 9, 8, 1.0)),
        ("sparkle_glow_disc", glow_ellipse(92, 92, 20, 0.45)),
        # Idle background bloom (radial-gradient, 240 px)
        ("sparkle_glow_bloom", radial_glow(240, 0.35)),
        # Sparkles: plain and with drop-shadow glow
        ("sparkle_ring_dashed", dashed_ring(168)),
        ("sparkle_star_16", sparkle(16)),
        ("sparkle_star_glow_16", sparkle(16, 4)),
    ]

    c = ["/* Generated by tools/gen_sparkle_assets.py - do not edit by hand.",
         " * Glyphs from Be Vietnam Pro and Baloo 2, SIL Open Font License 1.1. */",
         "#include \"sparkle_assets.h\"", ""]
    h = ["/* Generated by tools/gen_sparkle_assets.py - do not edit by hand. */",
         "#ifndef SPARKLE_ASSETS_H", "#define SPARKLE_ASSETS_H", "",
         "#include <lvgl.h>", "", "#ifdef __cplusplus", "extern \"C\" {", "#endif", ""]
    total = 0
    for name, path, px, cps, weight in fonts:
        glyphs, lh, bl = render_font(path, px, cps, weight)
        src, size = emit_font(name, glyphs, lh, bl)
        c += [src, ""]
        h.append(f"extern lv_font_t {name};")
        total += size
        print(f"{name}: {len(glyphs)} glyphs, line {lh}px, {size} B")
    for name, a in masks:
        src, size = emit_mask(name, a)
        c += [src, ""]
        h.append(f"extern const lv_image_dsc_t {name};")
        total += size
        print(f"{name}: {a.shape[1]}x{a.shape[0]}, {size} B")
    h += ["", "#ifdef __cplusplus", "}", "#endif", "", "#endif  // SPARKLE_ASSETS_H", ""]
    with open(OUT_C, "w", encoding="utf-8") as f:
        f.write("\n".join(c))
    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("\n".join(h))
    print(f"total {total / 1024:.1f} KiB")


if __name__ == "__main__":
    main()

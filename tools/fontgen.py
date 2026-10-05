"""Bitmap font atlases (OFL fonts: Fredoka for UI, Cinzel for titles).

Glyphs are rendered white with a dark outline baked in. The atlas is IA8
(intensity = fill vs outline, alpha = coverage) so vertex colour tints the
fill while the outline stays dark.
"""
import os
import struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from meshbuild import fnv1a

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
CHARS = [chr(c) for c in range(32, 127)]


def make_font(path, size, variation, outline, atlas_w, atlas_h, chars=CHARS, shadow=1):
    font = ImageFont.truetype(path, size)
    if variation:
        try:
            font.set_variation_by_name(variation)
        except Exception:
            pass
    ascent, descent = font.getmetrics()
    pad = outline + shadow + 1
    glyphs = []
    for ch in chars:
        bbox = font.getbbox(ch)
        adv = font.getlength(ch)
        w = max(1, bbox[2] - bbox[0]) + pad * 2
        h = max(1, bbox[3] - bbox[1]) + pad * 2
        if ch == ' ':
            w, h = 1, 1
        img = Image.new('L', (w, h), 0)
        d = ImageDraw.Draw(img)
        if ch != ' ':
            d.text((pad - bbox[0], pad - bbox[1]), ch, font=font, fill=255)
        glyphs.append((ch, img, bbox, adv))
    # pack rows
    glyphs_sorted = sorted(glyphs, key=lambda g: -g[1].size[1])
    x = y = 0
    rowh = 0
    placed = {}
    for ch, img, bbox, adv in glyphs_sorted:
        w, h = img.size
        if x + w > atlas_w:
            x = 0
            y += rowh + 1
            rowh = 0
        if y + h > atlas_h:
            raise ValueError('atlas too small for %s %d' % (path, size))
        placed[ch] = (x, y)
        x += w + 1
        rowh = max(rowh, h)
    fill = np.zeros((atlas_h, atlas_w), np.float64)
    for ch, img, bbox, adv in glyphs:
        px, py = placed[ch]
        w, h = img.size
        fill[py:py + h, px:px + w] = np.asarray(img) / 255.0
    # outline = dilated fill, plus a soft drop shadow
    fimg = Image.fromarray((fill * 255).astype(np.uint8))
    dil = fimg
    for _ in range(outline):
        dil = dil.filter(ImageFilter.MaxFilter(3))
    dil = np.asarray(dil) / 255.0
    if shadow:
        sh = np.roll(np.roll(dil, shadow, 0), shadow, 1) * 0.6
    else:
        sh = np.zeros_like(dil)
    alpha = np.clip(np.maximum(dil, sh), 0, 1)
    inten = np.where(alpha > 0, fill / np.maximum(alpha, 1e-6), 0)
    inten = np.clip(inten * 1.0, 0, 1)
    # intensity: 255 inside the glyph fill, ~45 for the outline
    I = 45 + inten * 210
    A = alpha * 255
    out = np.zeros((atlas_h, atlas_w, 4), np.uint8)
    out[..., 0] = out[..., 1] = out[..., 2] = np.clip(I, 0, 255).astype(np.uint8)
    out[..., 3] = np.clip(A, 0, 255).astype(np.uint8)
    # glyph table
    table = []
    for ch, img, bbox, adv in glyphs:
        px, py = placed[ch]
        w, h = img.size
        xoff = bbox[0] - pad
        yoff = bbox[1] - pad
        if ch == ' ':
            w = h = 0
        table.append((ord(ch), px, py, w, h, xoff, yoff, int(round(adv))))
    return out, table, ascent + descent, ascent


def font_asset(tex_name, atlas, table, line_h, ascent):
    h, w = atlas.shape[:2]
    data = struct.pack('>4sHHHhHHI', b'FNT1', w, h, line_h, ascent, len(table), 0, fnv1a(tex_name))
    for (code, x, y, gw, gh, xo, yo, adv) in table:
        data += struct.pack('>HHHBBbbB3x', code, x, y, gw, gh, xo, yo, adv)
    return data


def build_all(b, stages=None):
    if stages and 'font' not in stages:
        return
    fred = os.path.join(ROOT, 'assets', 'fonts', 'Fredoka.ttf')
    cinzel = os.path.join(ROOT, 'assets', 'fonts', 'Cinzel.ttf')
    title_chars = [chr(c) for c in range(ord('A'), ord('Z') + 1)] + list(" 0123456789-.,!?'&:")
    specs = [
        ('font/ui', fred, 19, b'SemiBold', 2, 256, 256, 'IA8', CHARS),
        ('font/small', fred, 15, b'Medium', 1, 256, 128, 'IA8', CHARS),
        ('font/big', fred, 34, b'Bold', 3, 512, 256, 'IA4', CHARS),
        ('font/title', cinzel, 60, b'Bold', 4, 512, 512, 'IA4', title_chars),
    ]
    for name, path, size, var, outline, aw, ah, fmt, chars in specs:
        atlas, table, lh, asc = make_font(path, size, var, outline, aw, ah, chars)
        tname = 'tx/' + name.replace('/', '_')
        b.add_texture(tname, atlas, fmt)
        b.pak.add(name, 'FONT', font_asset(tname, atlas, table, lh, asc))
        b.report.append(('font', name, len(table)))

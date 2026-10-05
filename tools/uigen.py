"""UI textures: panel 9-slice, GameCube button glyphs, bars, minimap."""
import math
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))


def rounded_panel(size=64, radius=18, border=3):
    """White rounded rect (alpha = shape) with a faint top-lit gradient."""
    s = 4
    big = Image.new('L', (size * s, size * s), 0)
    d = ImageDraw.Draw(big)
    d.rounded_rectangle([0, 0, size * s - 1, size * s - 1], radius * s, fill=255)
    a = np.asarray(big.resize((size, size), Image.LANCZOS)).astype(np.float64)
    y = np.linspace(0, 1, size)[:, None]
    I = 255 * (1.0 - 0.10 * y) * np.ones((size, size))
    out = np.zeros((size, size, 4), np.uint8)
    out[..., 0] = out[..., 1] = out[..., 2] = np.clip(I, 0, 255).astype(np.uint8)
    out[..., 3] = a.astype(np.uint8)
    return out


def circle(size, r_frac=0.48, soft=1.5):
    y, x = np.mgrid[0:size, 0:size]
    d = np.sqrt((x - size / 2 + 0.5) ** 2 + (y - size / 2 + 0.5) ** 2)
    a = np.clip((size * r_frac - d) / soft + 0.5, 0, 1)
    return a


def button_glyphs():
    """GameCube controller buttons, 32x32 cells: A B X Y Z L R START Dpad Cstick Stick."""
    cell = 32
    names = ['A', 'B', 'X', 'Y', 'Z', 'L', 'R', 'START', 'DPAD', 'C', 'STICK']
    W = cell * 8
    H = cell * 2
    atlas = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    font = ImageFont.truetype(os.path.join(ROOT, 'assets', 'fonts', 'Fredoka.ttf'), 17)
    try:
        font.set_variation_by_name(b'Bold')
    except Exception:
        pass
    small = ImageFont.truetype(os.path.join(ROOT, 'assets', 'fonts', 'Fredoka.ttf'), 11)
    try:
        small.set_variation_by_name(b'Bold')
    except Exception:
        pass
    cols = {'A': (40, 190, 120), 'B': (220, 60, 60), 'X': (200, 200, 205), 'Y': (200, 200, 205), 'Z': (120, 80, 200),
            'L': (170, 170, 178), 'R': (170, 170, 178), 'START': (180, 180, 186), 'DPAD': (90, 90, 96), 'C': (230, 200, 40), 'STICK': (190, 190, 196)}
    for i, n in enumerate(names):
        ox, oy = (i % 8) * cell, (i // 8) * cell
        s = 4
        im = Image.new('RGBA', (cell * s, cell * s), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        c = cols[n]
        dark = tuple(int(v * 0.55) for v in c)
        if n in ('A', 'B', 'C', 'STICK'):
            r = 13 if n == 'A' else (10 if n == 'B' else 11)
            d.ellipse([(16 - r) * s, (16 - r) * s, (16 + r) * s, (16 + r) * s], fill=dark + (255,))
            d.ellipse([(16 - r + 2) * s, (16 - r + 2) * s, (16 + r - 2) * s, (16 + r - 2) * s], fill=c + (255,))
        elif n in ('X', 'Y'):
            d.rounded_rectangle([6 * s, 9 * s, 26 * s, 23 * s], 7 * s, fill=dark + (255,))
            d.rounded_rectangle([8 * s, 11 * s, 24 * s, 21 * s], 5 * s, fill=c + (255,))
        elif n == 'Z':
            d.rounded_rectangle([4 * s, 10 * s, 28 * s, 22 * s], 5 * s, fill=dark + (255,))
            d.rounded_rectangle([6 * s, 12 * s, 26 * s, 20 * s], 3 * s, fill=c + (255,))
        elif n in ('L', 'R'):
            d.rounded_rectangle([4 * s, 8 * s, 28 * s, 24 * s], 8 * s, fill=dark + (255,))
            d.rounded_rectangle([6 * s, 10 * s, 26 * s, 22 * s], 6 * s, fill=c + (255,))
        elif n == 'START':
            d.rounded_rectangle([3 * s, 10 * s, 29 * s, 22 * s], 6 * s, fill=dark + (255,))
            d.rounded_rectangle([5 * s, 12 * s, 27 * s, 20 * s], 4 * s, fill=c + (255,))
        elif n == 'DPAD':
            for rect in ([12, 3, 20, 29], [3, 12, 29, 20]):
                d.rectangle([v * s for v in rect], fill=(40, 40, 44, 255))
            for rect in ([13, 4, 19, 28], [4, 13, 28, 19]):
                d.rectangle([v * s for v in rect], fill=c + (255,))
        im = im.resize((cell, cell), Image.LANCZOS)
        dd = ImageDraw.Draw(im)
        label = {'START': 'ST', 'DPAD': '', 'STICK': '', 'C': 'C'}.get(n, n)
        f = small if len(label) > 1 else font
        if label:
            bb = dd.textbbox((0, 0), label, font=f)
            tx = 16 - (bb[2] - bb[0]) / 2 - bb[0]
            ty = 16 - (bb[3] - bb[1]) / 2 - bb[1]
            txtcol = (255, 255, 255, 255) if n in ('A', 'B', 'Z') else (40, 40, 48, 255)
            dd.text((tx, ty), label, font=f, fill=txtcol)
        atlas.paste(im, (ox, oy), im)
    return np.asarray(atlas).copy()


def bar_texture(w=64, h=16):
    """Glossy bar fill: intensity gradient (top highlight)."""
    y = np.linspace(0, 1, h)[:, None]
    I = 0.78 + 0.30 * np.exp(-((y - 0.28) / 0.18) ** 2) - 0.22 * y
    I = np.clip(I, 0, 1) * np.ones((h, w))
    out = np.zeros((h, w, 4), np.uint8)
    out[..., 0] = out[..., 1] = out[..., 2] = (I * 255).astype(np.uint8)
    out[..., 3] = 255
    return out


def ring_target(size=64):
    y, x = np.mgrid[0:size, 0:size]
    r = np.sqrt((x - size / 2 + 0.5) ** 2 + (y - size / 2 + 0.5) ** 2) / (size / 2)
    a = np.clip(1 - np.abs(r - 0.82) / 0.1, 0, 1)
    # four notches
    ang = np.arctan2(y - size / 2, x - size / 2)
    notch = (np.abs(np.sin(ang * 2)) < 0.25) & (r > 0.6) & (r < 0.98)
    a = np.maximum(a, notch * 0.9)
    out = np.zeros((size, size, 4), np.uint8)
    out[..., :3] = 255
    out[..., 3] = (np.clip(a, 0, 1) * 255).astype(np.uint8)
    return out


def aoe_disc(size=64):
    y, x = np.mgrid[0:size, 0:size]
    r = np.sqrt((x - size / 2 + 0.5) ** 2 + (y - size / 2 + 0.5) ** 2) / (size / 2)
    edge = np.clip(1 - np.abs(r - 0.94) / 0.06, 0, 1)
    fill = np.clip((1 - r) * 4, 0, 1) * (0.35 + 0.25 * r)
    a = np.maximum(edge, fill)
    out = np.zeros((size, size, 4), np.uint8)
    out[..., :3] = 255
    out[..., 3] = (np.clip(a, 0, 1) * 255).astype(np.uint8)
    return out


def build_all(b, stages=None):
    if stages and 'ui' not in stages:
        return
    b.add_texture('tx/ui_panel', rounded_panel(64, 16, 3), 'IA8')
    b.add_texture('tx/ui_panel_sharp', rounded_panel(32, 6, 2), 'IA8')
    b.add_texture('tx/ui_buttons', button_glyphs(), 'RGB5A3')
    b.add_texture('tx/ui_bar', bar_texture(), 'I8', wrap=(1, 0))
    m = np.zeros((64, 64, 4), np.uint8)
    m[..., :3] = 255
    m[..., 3] = (circle(64, 0.49, 1.2) * 255).astype(np.uint8)
    b.add_texture('tx/ui_circle', m, 'IA8')
    b.add_texture('tx/ui_target', ring_target(64), 'IA8')
    b.add_texture('tx/aoe', aoe_disc(64), 'IA8')
    # minimap from the world paint (generated earlier in the same run)
    try:
        import worldgen
        w = getattr(worldgen, 'LAST_WORLD', None)
        if w is not None:
            img = np.concatenate([w.C_minimap, np.full(w.C.shape[:2] + (1,), 255.0)], axis=-1)
            # darken water and emphasise roads a little
            im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), 'RGBA').resize((128, 128), Image.LANCZOS)
            b.add_texture('tx/minimap', np.asarray(im).copy(), 'RGB565')
    except Exception as e:
        print('  minimap failed', e)

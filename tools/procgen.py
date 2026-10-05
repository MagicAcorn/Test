"""Procedural models and textures that fill gaps in the KayKit packs:
broadleaf trees, bushes, ore nodes, herbs, slimes, crafting stations, tools,
item props, grass/flower cards and effect sprites. All use vertex colours
(no textures) except the alpha-tested cards, so they share the cel shading.
"""
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

from meshbuild import Part, BF_ALPHATEST, BF_DOUBLESIDED, BF_UNLIT, BF_FOLIAGE
from meshkit import Mesh, merge, icosphere, cylinder, box, cone, quad_cross

# --------------------------------------------------------------- palettes
LEAF = {
    'oak': ((38, 92, 40), (120, 178, 62)),
    'birch': ((70, 120, 40), (170, 205, 90)),
    'autumn': ((150, 62, 28), (245, 168, 58)),
    'blossom': ((190, 100, 140), (255, 205, 220)),
    'willow': ((52, 110, 70), (140, 196, 110)),
    'dark': ((24, 64, 44), (70, 130, 70)),
    'maple': ((150, 34, 26), (240, 96, 52)),
    'poplar': ((44, 104, 46), (150, 196, 80)),
    'stone': ((40, 86, 50), (110, 160, 80)),
}
BARK = {
    'oak': ((70, 44, 30), (122, 82, 52)),
    'birch': ((150, 146, 136), (236, 232, 222)),
    'autumn': ((74, 46, 32), (126, 86, 54)),
    'blossom': ((70, 44, 36), (118, 78, 60)),
    'willow': ((66, 50, 36), (112, 88, 60)),
    'dark': ((44, 32, 26), (88, 66, 48)),
    'maple': ((66, 42, 32), (120, 80, 56)),
    'poplar': ((96, 84, 70), (170, 156, 136)),
    'stone': ((90, 60, 44), (150, 104, 74)),
}

ORE = {
    'copper': ((176, 92, 40), (240, 150, 80)),
    'tin': ((120, 132, 150), (200, 214, 226)),
    'iron': ((92, 52, 40), (160, 96, 72)),
    'silver': ((170, 176, 190), (245, 248, 255)),
    'gold': ((190, 140, 30), (255, 220, 90)),
    'crystal': ((90, 70, 200), (190, 170, 255)),
    'coal': ((30, 30, 34), (70, 70, 78)),
    'stone': ((110, 108, 104), (170, 168, 160)),
}


def rng_for(name):
    import zlib
    return np.random.default_rng(zlib.crc32(name.encode()))


# ---------------------------------------------------------------- trees
def tree_broadleaf(seed, kind='oak', height=4.2, spread=1.0):
    rng = np.random.default_rng(seed)
    parts = []
    lean = rng.uniform(-0.06, 0.06, size=2)
    trunk = cylinder(0.72 * spread, 0.42 * spread, height, segs=7, cap_top=False)
    # gentle lean / bend proportional to height
    t = trunk.pos[:, 1] / height
    trunk.pos[:, 0] += lean[0] * t * t * height
    trunk.pos[:, 2] += lean[1] * t * t * height
    trunk.gradient_y(BARK[kind][0], BARK[kind][1])
    if kind == 'birch':
        # dark bark stripes
        stripes = (np.sin(trunk.pos[:, 1] * 5.0 + rng.uniform(0, 3)) > 0.75)
        trunk.clr[stripes, :3] *= 0.35
    parts.append(trunk.faceted())
    # flared roots
    for k in range(4):
        a = k * math.pi / 2 + rng.uniform(-0.3, 0.3)
        root = cone(0.28 * spread, 0.9, segs=5)
        root.rotate_z(1.05).rotate_y(a).translate(math.cos(a) * 0.35, 0.05, -math.sin(a) * 0.35)
        root.gradient_y(BARK[kind][0], BARK[kind][1])
        parts.append(root.faceted())
    # branches
    top = np.array([lean[0] * height, height, lean[1] * height])
    for k in range(3):
        a = rng.uniform(0, 2 * math.pi)
        br = cylinder(0.16 * spread, 0.08 * spread, 1.6, segs=5, cap_top=False)
        br.rotate_z(0.9).rotate_y(a).translate(*(top * np.array([1, 0.78, 1])))
        br.gradient_y(BARK[kind][0], BARK[kind][1])
        parts.append(br.faceted())
    # canopy puffs
    dark, light = LEAF[kind]
    n = rng.integers(5, 8)
    centers = [top + np.array([0, 1.0, 0])]
    for k in range(n - 1):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(0.9, 1.6) * spread
        centers.append(top + np.array([math.cos(a) * r, rng.uniform(0.0, 1.5), math.sin(a) * r]))
    puffs = []
    ymin = top[1] - 1.6
    ymax = top[1] + 3.4
    for c in centers:
        s = rng.uniform(1.25, 1.75) * spread
        p = icosphere(1).scale(s, s * 0.82, s).displace(rng, 0.18 * s, 2.0).translate(*c)
        puffs.append(p)
    canopy = merge(puffs)
    canopy.gradient_y(dark, light, ymin, ymax, power=0.8)
    canopy.jitter_color(rng, 0.06)
    parts.append(canopy)
    return [m.to_part(BF_FOLIAGE if m is canopy else 0) for m in parts]


def _trunk(rng, kind, height, r0, r1, segs=7, lean=0.07):
    tr = cylinder(r0, r1, height, segs=segs, cap_top=False)
    ln = rng.uniform(-lean, lean, size=2)
    t = tr.pos[:, 1] / height
    tr.pos[:, 0] += ln[0] * t * t * height
    tr.pos[:, 2] += ln[1] * t * t * height
    tr.gradient_y(BARK[kind][0], BARK[kind][1])
    return tr, np.array([ln[0] * height, height, ln[1] * height])


def _puffs(rng, centers, sizes, kind, squash=0.82, subdiv=1, rough=0.18, ymin=None, ymax=None):
    puffs = []
    for c, s in zip(centers, sizes):
        puffs.append(icosphere(subdiv).scale(s, s * squash, s).displace(rng, rough * s, 2.0).translate(*c))
    m = merge(puffs)
    ys = np.array([c[1] for c in centers])
    lo = ys.min() - max(sizes) if ymin is None else ymin
    hi = ys.max() + max(sizes) if ymax is None else ymax
    m.gradient_y(LEAF[kind][0], LEAF[kind][1], lo, hi, power=0.8)
    m.jitter_color(rng, 0.06)
    return m


def tree_poplar(seed, height=8.0):
    """Tall columnar tree: a narrow stack of leafy lobes."""
    rng = np.random.default_rng(seed)
    trunk, top = _trunk(rng, 'poplar', height * 0.55, 0.42, 0.22, segs=6, lean=0.03)
    centers, sizes = [], []
    n = 5
    for i in range(n):
        f = i / (n - 1)
        y = height * (0.35 + 0.65 * f)
        s = (1.25 - 0.55 * f) * rng.uniform(0.9, 1.1)
        centers.append(np.array([rng.uniform(-0.25, 0.25), y, rng.uniform(-0.25, 0.25)]))
        sizes.append(s)
    canopy = _puffs(rng, centers, sizes, 'poplar', squash=1.25)
    return [trunk.faceted().to_part(), canopy.to_part(BF_FOLIAGE)]


def tree_umbrella(seed, height=6.0):
    """Stone-pine silhouette: bare leaning trunk with a flat, wide crown."""
    rng = np.random.default_rng(seed)
    trunk, top = _trunk(rng, 'stone', height, 0.55, 0.3, segs=7, lean=0.16)
    parts = [trunk.faceted()]
    for k in range(3):
        a = k * 2.1 + rng.uniform(-0.3, 0.3)
        br = cylinder(0.2, 0.1, 2.0, segs=5, cap_top=False).rotate_z(1.1).rotate_y(a).translate(*(top * np.array([1, 0.85, 1])))
        br.gradient_y(BARK['stone'][0], BARK['stone'][1])
        parts.append(br.faceted())
    centers, sizes = [], []
    for k in range(7):
        a = k * 0.9 + rng.uniform(0, 0.5)
        r = 0 if k == 0 else rng.uniform(1.4, 2.4)
        centers.append(top + np.array([math.cos(a) * r, rng.uniform(0.2, 0.7), math.sin(a) * r]))
        sizes.append(rng.uniform(1.3, 1.7))
    canopy = _puffs(rng, centers, sizes, 'stone', squash=0.45)
    return [m.faceted().to_part() for m in parts] + [canopy.to_part(BF_FOLIAGE)]


def tree_birch_slender(seed, height=6.5):
    """Tall white birch with a light, airy crown of small clusters."""
    rng = np.random.default_rng(seed)
    trunk, top = _trunk(rng, 'birch', height, 0.3, 0.16, segs=6, lean=0.09)
    stripes = (np.sin(trunk.pos[:, 1] * 6.0 + rng.uniform(0, 3)) > 0.7)
    trunk.clr[stripes, :3] *= 0.3
    centers, sizes = [], []
    for k in range(8):
        f = rng.uniform(0.45, 1.05)
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(0.3, 1.3) * (1.2 - f * 0.6)
        centers.append(np.array([top[0] * f + math.cos(a) * r, height * f + 0.4, top[2] * f + math.sin(a) * r]))
        sizes.append(rng.uniform(0.6, 0.95))
    canopy = _puffs(rng, centers, sizes, 'birch', squash=0.9)
    return [trunk.faceted().to_part(), canopy.to_part(BF_FOLIAGE)]


def tree_willow_curtain(seed, height=4.8):
    """Weeping willow: a dome with long hanging leaf curtains."""
    rng = np.random.default_rng(seed)
    trunk, top = _trunk(rng, 'willow', height, 0.75, 0.45, segs=7, lean=0.1)
    dome = _puffs(rng, [top + np.array([0, 0.6, 0])] + [top + np.array([math.cos(a) * 1.6, 0.2, math.sin(a) * 1.6]) for a in np.linspace(0, 6.28, 5, endpoint=False)],
                  [2.0] + [1.5] * 5, 'willow', squash=0.6)
    strands = []
    for k in range(14):
        a = k * 2 * math.pi / 14 + rng.uniform(-0.15, 0.15)
        r = rng.uniform(2.1, 2.9)
        L = rng.uniform(2.4, 3.6)
        st = cone(0.42, L, segs=4).scale(1, -1, 1).translate(math.cos(a) * r, top[1] + 0.6, math.sin(a) * r)
        st.gradient_y(LEAF['willow'][0], LEAF['willow'][1])
        strands.append(st)
    hang = merge(strands)
    return [trunk.faceted().to_part(), dome.to_part(BF_FOLIAGE), hang.faceted().to_part(BF_FOLIAGE | BF_DOUBLESIDED)]


def tree_maple(seed, height=5.0):
    """Broad red maple: wide rounded crown, short sturdy trunk."""
    rng = np.random.default_rng(seed)
    trunk, top = _trunk(rng, 'maple', height, 0.6, 0.38, segs=7, lean=0.06)
    centers, sizes = [top + np.array([0, 0.9, 0])], [1.9]
    for k in range(6):
        a = k * 1.05 + rng.uniform(-0.2, 0.2)
        centers.append(top + np.array([math.cos(a) * 1.9, rng.uniform(-0.3, 0.8), math.sin(a) * 1.9]))
        sizes.append(rng.uniform(1.25, 1.55))
    canopy = _puffs(rng, centers, sizes, 'maple', squash=0.78)
    return [trunk.faceted().to_part(), canopy.to_part(BF_FOLIAGE)]


def tree_old_oak(seed, height=5.2):
    """Wide old oak: thick trunk, big spreading limbs, sprawling crown."""
    rng = np.random.default_rng(seed)
    trunk, top = _trunk(rng, 'oak', height, 1.0, 0.6, segs=8, lean=0.05)
    parts = [trunk.faceted()]
    centers, sizes = [top + np.array([0, 1.0, 0])], [2.0]
    for k in range(5):
        a = k * 1.256 + rng.uniform(-0.2, 0.2)
        br = cylinder(0.36, 0.16, 3.2, segs=6, cap_top=False).rotate_z(1.2).rotate_y(-a).translate(*(top * np.array([1, 0.72, 1])))
        br.gradient_y(BARK['oak'][0], BARK['oak'][1])
        parts.append(br.faceted())
        centers.append(top + np.array([math.cos(a) * 3.1, rng.uniform(-0.6, 0.4), math.sin(a) * 3.1]))
        sizes.append(rng.uniform(1.5, 1.9))
    canopy = _puffs(rng, centers, sizes, 'oak', squash=0.7)
    return [m.faceted().to_part() for m in parts] + [canopy.to_part(BF_FOLIAGE)]


def tree_pine(seed, kind='dark', height=7.0):
    rng = np.random.default_rng(seed)
    parts = []
    trunk = cylinder(0.42, 0.18, height * 0.55, segs=6, cap_top=False)
    trunk.gradient_y(BARK['dark'][0], BARK['oak'][1])
    parts.append(trunk.faceted())
    tiers = 4
    dark, light = LEAF[kind]
    for i in range(tiers):
        f = i / (tiers - 1)
        r = (2.3 - 1.5 * f) * rng.uniform(0.9, 1.1)
        h = 2.6 - 0.8 * f
        y = height * 0.18 + f * height * 0.62
        c = cone(r, h, segs=8, y0=y)
        c.displace(rng, 0.12, 2.0)
        c.rotate_y(rng.uniform(0, 1))
        c.gradient_y(dark, light, height * 0.15, height + 0.8, 0.9)
        parts.append(c.faceted())
    return [m.to_part(BF_FOLIAGE if k > 0 else 0) for k, m in enumerate(parts)]


def bush(seed, kind='oak', size=1.0, berries=None):
    rng = np.random.default_rng(seed)
    dark, light = LEAF[kind]
    puffs = []
    for k in range(rng.integers(3, 5)):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(0.2, 0.7) * size
        s = rng.uniform(0.6, 0.85) * size
        puffs.append(icosphere(1).scale(s, s * 0.8, s).displace(rng, 0.12 * s, 2.5).translate(math.cos(a) * r, s * 0.55, math.sin(a) * r))
    m = merge(puffs)
    m.gradient_y(dark, light, 0, 1.4 * size)
    m.jitter_color(rng, 0.05)
    parts = [m.to_part(BF_FOLIAGE)]
    if berries:
        bs = []
        for k in range(9):
            v = rng.normal(size=3)
            v[1] = abs(v[1]) * 0.6 + 0.3
            v = v / np.linalg.norm(v)
            b = icosphere(0).scale(0.09 * size).translate(v[0] * 0.75 * size, 0.45 * size + v[1] * 0.7 * size, v[2] * 0.75 * size)
            b.color(berries)
            bs.append(b)
        parts.append(merge(bs).to_part())
    return parts


def rock(seed, size=1.0, kind='stone', squash=0.7):
    rng = np.random.default_rng(seed)
    m = icosphere(1).scale(size, size * squash, size * rng.uniform(0.8, 1.1)).displace(rng, 0.22 * size, 1.4)
    m.pos[:, 1] = np.maximum(m.pos[:, 1], -0.1 * size)
    m.translate(0, 0.25 * size * squash, 0)
    lo, hi = ORE['stone']
    m.gradient_y(lo, hi)
    m.jitter_color(rng, 0.08)
    return m.faceted()


def ore_node(kind, seed=1):
    rng = np.random.default_rng(seed)
    base = rock(seed, 1.25, 'stone', 0.8)
    parts = [base]
    lo, hi = ORE[kind]
    if kind in ('crystal',):
        for k in range(6):
            a = rng.uniform(0, 2 * math.pi)
            tilt = rng.uniform(0.15, 0.6)
            h = rng.uniform(0.9, 1.7)
            c = merge([cylinder(0.18, 0.18, h * 0.7, segs=6, cap_top=False), cone(0.18, h * 0.3, segs=6, y0=h * 0.7)])
            c.rotate_z(tilt).rotate_y(a).translate(math.cos(a) * 0.5, 0.6, math.sin(a) * 0.5)
            c.gradient_y(lo, hi)
            parts.append(c.faceted())
    else:
        for k in range(rng.integers(6, 10)):
            v = rng.normal(size=3)
            v[1] = abs(v[1]) * 0.8 + 0.2
            v /= np.linalg.norm(v)
            s = rng.uniform(0.16, 0.3)
            n = icosphere(0).scale(s, s * 0.7, s).translate(v[0] * 1.0, 0.4 + v[1] * 0.75, v[2] * 1.0)
            n.gradient_y(lo, hi)
            parts.append(n.faceted())
    return [m.to_part() for m in parts]


def herb(kind, seed=1):
    rng = np.random.default_rng(seed)
    flowers = {'mint': None, 'lavender': (150, 110, 220), 'sunpetal': (250, 210, 60), 'emberroot': (230, 80, 50),
               'moonbloom': (170, 210, 255), 'sage': None}
    leaf_dark, leaf_light = ((50, 110, 50), (130, 200, 90)) if kind != 'sage' else ((90, 120, 90), (170, 200, 160))
    parts = []
    leaves = []
    for k in range(7):
        a = k * 2 * math.pi / 7 + rng.uniform(-0.2, 0.2)
        l = icosphere(0).scale(0.12, 0.05, 0.42)
        l.translate(0, 0.0, 0.32).rotate_x(-0.5 - rng.uniform(0, 0.4)).rotate_y(a).translate(0, 0.12, 0)
        leaves.append(l)
    lv = merge(leaves)
    lv.gradient_y(leaf_dark, leaf_light, 0, 0.5)
    parts.append(lv.faceted())
    fc = flowers.get(kind)
    if fc:
        heads = []
        stems = []
        for k in range(5):
            a = rng.uniform(0, 2 * math.pi)
            r = rng.uniform(0.05, 0.25)
            h = rng.uniform(0.5, 0.8)
            st = cylinder(0.025, 0.02, h, segs=4, cap_top=False).translate(math.cos(a) * r, 0, math.sin(a) * r)
            st.color((70, 130, 60))
            stems.append(st)
            hd = icosphere(0).scale(0.09, 0.14 if kind == 'lavender' else 0.08, 0.09).translate(math.cos(a) * r, h, math.sin(a) * r)
            hd.gradient_y(tuple(int(c * 0.7) for c in fc), fc)
            heads.append(hd)
        parts.append(merge(stems).faceted())
        parts.append(merge(heads).faceted())
    return [m.to_part() for m in parts]


def mushroom(cap=(200, 50, 40), seed=1, glow=False):
    rng = np.random.default_rng(seed)
    parts = []
    for k in range(3):
        a = rng.uniform(0, 2 * math.pi)
        r = 0 if k == 0 else rng.uniform(0.25, 0.4)
        s = 1.0 if k == 0 else rng.uniform(0.5, 0.7)
        stem = cylinder(0.08 * s, 0.06 * s, 0.35 * s, segs=6, cap_top=False).translate(math.cos(a) * r, 0, math.sin(a) * r)
        stem.color((236, 226, 200))
        top = icosphere(1).scale(0.26 * s, 0.16 * s, 0.26 * s)
        top.pos[:, 1] = np.maximum(top.pos[:, 1], -0.02)
        top.translate(math.cos(a) * r, 0.35 * s, math.sin(a) * r)
        top.gradient_y(tuple(int(c * 0.6) for c in cap), cap)
        # white spots
        spots = rng.uniform(size=len(top.pos)) > 0.82
        top.clr[spots, :3] = 245
        parts += [stem.faceted(), top.faceted()]
    return [m.to_part(BF_UNLIT if glow and i % 2 == 1 else 0) for i, m in enumerate(parts)]


def slime(color, seed=1):
    rng = np.random.default_rng(seed)
    body = icosphere(2).scale(0.9, 0.72, 0.9)
    body.pos[:, 1] = np.maximum(body.pos[:, 1], -0.45)
    body.translate(0, 0.45, 0)
    lo = tuple(int(c * 0.55) for c in color)
    hi = tuple(min(255, int(c * 1.15 + 30)) for c in color)
    body.gradient_y(lo, hi, 0, 1.1)
    parts = [body.to_part()]
    for side in (-1, 1):
        eye = icosphere(1).scale(0.13, 0.17, 0.08).translate(0.27 * side, 0.72, 0.8)
        eye.color((20, 20, 30))
        hl = icosphere(0).scale(0.045).translate(0.27 * side + 0.04, 0.78, 0.87)
        hl.color((255, 255, 255))
        parts.append(eye.to_part(BF_UNLIT))
        parts.append(hl.to_part(BF_UNLIT))
    return parts


# ------------------------------------------------------------- stations
def stone_ring(radius, count, size, rng, y=0.0):
    stones = []
    for k in range(count):
        a = 2 * math.pi * k / count
        s = size * rng.uniform(0.8, 1.2)
        st = icosphere(0).scale(s, s * 0.7, s).displace(rng, 0.1 * s, 2).translate(math.cos(a) * radius, y + s * 0.4, math.sin(a) * radius)
        st.gradient_y((96, 92, 88), (178, 172, 160))
        stones.append(st)
    return merge(stones).faceted()


def great_hearth():
    rng = np.random.default_rng(7)
    parts = []
    base = cylinder(3.4, 3.1, 0.5, segs=12, cap_top=True).gradient_y((110, 104, 96), (170, 162, 148))
    parts.append(base.faceted())
    step = cylinder(2.6, 2.4, 0.5, segs=12, cap_top=True, y0=0.5).gradient_y((120, 112, 104), (186, 178, 162))
    parts.append(step.faceted())
    bowl = cylinder(1.2, 1.8, 0.9, segs=10, cap_top=False, cap_bottom=True, y0=1.6)
    bowl.gradient_y((60, 50, 46), (130, 110, 90))
    parts.append(bowl.faceted())
    stand = cylinder(0.5, 0.7, 0.7, segs=8, cap_top=False, y0=1.0).gradient_y((70, 60, 54), (110, 96, 84))
    parts.append(stand.faceted())
    parts.append(stone_ring(2.9, 14, 0.42, rng, 0.45))
    logs = []
    for k in range(5):
        lg = cylinder(0.16, 0.16, 1.9, segs=6, cap_top=True, cap_bottom=True).rotate_z(1.2).rotate_y(k * 1.256).translate(0, 2.0, 0)
        lg.gradient_y((70, 44, 28), (126, 84, 50))
        logs.append(lg)
    parts.append(merge(logs).faceted())
    # four iron posts with rings
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        post = cylinder(0.14, 0.11, 2.6, segs=6).translate(math.cos(a) * 2.3, 0.5, math.sin(a) * 2.3)
        post.gradient_y((50, 46, 50), (110, 106, 112))
        parts.append(post.faceted())
    return [m.to_part() for m in parts]


def bridge(hl, hw, arch, seed=5):
    """Arched plank bridge spanning x in [-hl, hl] (deck top follows the same
    curve the engine walks on: y = arch * (1 - (x / hl)^2)), with railings,
    side beams, river piers and stone abutments that sink into both banks."""
    rng = np.random.default_rng(seed)
    deck = lambda x: arch * (1.0 - (x / hl) ** 2)
    slope = lambda x: math.atan(-2.0 * arch * x / (hl * hl))
    wood, stone = [], []
    n = int(2 * hl / 0.72)
    for i in range(n):
        x = -hl + (i + 0.5) * (2 * hl / n)
        pl = box(2 * hl / n - 0.06, 0.24, 2 * hw, y0=-0.24).rotate_z(slope(x)).translate(x, deck(x), 0)
        c = np.array([150, 104, 64]) * rng.uniform(0.85, 1.12)
        pl.color(tuple(np.clip(c, 0, 255).astype(int)))
        wood.append(pl)
    # side beams under the deck edge
    for zs in (-1, 1):
        segs = 10
        for i in range(segs):
            x0, x1 = -hl + i * 2 * hl / segs, -hl + (i + 1) * 2 * hl / segs
            xm = (x0 + x1) / 2
            bm = box(x1 - x0 + 0.1, 0.42, 0.36, y0=-0.62).rotate_z(slope(xm)).translate(xm, deck(xm), zs * (hw - 0.1))
            bm.color((96, 64, 40))
            wood.append(bm)
    # railings: posts and a top rail
    posts = int(2 * hl / 2.4) + 1
    for zs in (-1, 1):
        z = zs * (hw - 0.16)
        xs = [-hl + 0.3 + k * (2 * hl - 0.6) / (posts - 1) for k in range(posts)]
        for x in xs:
            p_ = box(0.24, 1.15, 0.24).translate(x, deck(x) - 0.05, z)
            p_.color((110, 74, 46))
            wood.append(p_)
        for a, b in zip(xs[:-1], xs[1:]):
            xm = (a + b) / 2
            r = box(b - a + 0.2, 0.14, 0.16).rotate_z(slope(xm)).translate(xm, deck(xm) + 0.95, z)
            r.color((168, 120, 74))
            wood.append(r)
            r2 = box(b - a + 0.2, 0.1, 0.1).rotate_z(slope(xm)).translate(xm, deck(xm) + 0.5, z)
            r2.color((140, 98, 60))
            wood.append(r2)
    # piers in the river
    for xs_ in (-0.42, 0.42):
        x = xs_ * hl
        for zs in (-1, 1):
            pr = cylinder(0.42, 0.5, deck(x) + 4.0, segs=7, y0=-4.0).translate(x, 0, zs * (hw - 0.45))
            pr.color((88, 62, 42))
            wood.append(pr)
    # stone abutments: blocks that bury into the banks under each end
    for sgn in (-1, 1):
        for k in range(3):
            w = 2.0 + k * 0.6
            blk = box(w, 3.4 - k * 0.5, 2 * hw + 1.0 - k * 0.3, y0=-3.4 - k * 0.2).translate(sgn * (hl + w / 2 - 0.4 - k * 0.2), 0, 0)
            blk.displace(rng, 0.08, 3.0)
            g_ = rng.integers(118, 150)
            blk.color((g_, g_ - 4, g_ - 12))
            stone.append(blk)
        cap = box(1.2, 0.3, 2 * hw + 1.2, y0=-0.05).translate(sgn * (hl + 0.2), 0, 0)
        cap.color((168, 160, 146))
        stone.append(cap)
    return [merge(wood).to_part(), merge(stone).to_part()]


def campfire(seed=3):
    rng = np.random.default_rng(seed)
    parts = [stone_ring(0.75, 9, 0.22, rng)]
    logs = []
    for k in range(4):
        lg = cylinder(0.09, 0.08, 1.1, segs=5, cap_top=True, cap_bottom=True).rotate_z(1.1).rotate_y(k * 1.57).translate(0, 0.25, 0)
        lg.gradient_y((60, 36, 22), (120, 80, 48))
        logs.append(lg)
    parts.append(merge(logs).faceted())
    ash = cylinder(0.55, 0.5, 0.06, segs=8).color((50, 46, 44))
    parts.append(ash.faceted())
    return [m.to_part() for m in parts]


def anvil():
    parts = []
    stump = cylinder(0.42, 0.48, 0.7, segs=8).gradient_y((80, 52, 34), (136, 94, 60))
    parts.append(stump.faceted())
    body = box(0.9, 0.3, 0.38, 0.7).gradient_y((40, 40, 46), (110, 112, 120))
    waist = box(0.4, 0.22, 0.3, 1.0).gradient_y((50, 50, 56), (100, 100, 108))
    top = box(1.2, 0.22, 0.46, 1.2).gradient_y((70, 72, 80), (150, 154, 164))
    horn = cone(0.2, 0.55, segs=6).rotate_z(-math.pi / 2).translate(0.6, 1.31, 0).gradient_y((80, 80, 90), (150, 150, 160))
    for m in (body, waist, top, horn.faceted()):
        parts.append(m)
    return [m.to_part() for m in parts]


def forge():
    rng = np.random.default_rng(11)
    parts = []
    base = box(2.4, 1.0, 1.6).gradient_y((90, 84, 80), (150, 140, 128))
    parts.append(base)
    rim = box(2.0, 0.15, 1.2, 1.0).color((60, 40, 30))
    parts.append(rim)
    coals = []
    for k in range(18):
        c = icosphere(0).scale(0.14).translate(rng.uniform(-0.8, 0.8), 1.12, rng.uniform(-0.45, 0.45))
        c.color((255, 120, 40) if k % 3 else (90, 40, 30))
        coals.append(c)
    parts.append(merge(coals).faceted())
    chim = cylinder(0.45, 0.35, 2.2, segs=6, y0=1.0).translate(0, 0, -0.55).gradient_y((100, 92, 86), (160, 150, 138))
    parts.append(chim.faceted())
    out = [m.to_part() for m in parts]
    out[2].flags = BF_UNLIT
    return out


def cooking_pot():
    rng = np.random.default_rng(5)
    parts = [stone_ring(0.7, 8, 0.2, rng)]
    pot = cylinder(0.45, 0.55, 0.6, segs=10, cap_top=False, cap_bottom=True, y0=0.35).gradient_y((30, 30, 34), (90, 90, 96))
    stew = cylinder(0.5, 0.5, 0.02, segs=10, y0=0.88).color((170, 110, 50))
    tri = []
    for k in range(3):
        a = k * 2.094
        leg = cylinder(0.04, 0.04, 1.8, segs=4).rotate_x(0.35).rotate_y(a).translate(math.cos(a) * 0.75, 0, math.sin(a) * 0.75)
        leg.color((80, 56, 38))
        tri.append(leg)
    parts += [pot.faceted(), stew.faceted(), merge(tri).faceted()]
    return [m.to_part() for m in parts]


def workbench():
    parts = []
    top = box(2.0, 0.16, 0.9, 0.9).gradient_y((120, 80, 48), (176, 124, 76))
    parts.append(top)
    for sx in (-0.85, 0.85):
        for sz in (-0.35, 0.35):
            leg = box(0.14, 0.9, 0.14).translate(sx, 0, sz).color((96, 64, 40))
            parts.append(leg)
    saw = box(0.6, 0.02, 0.18, 1.07).translate(-0.4, 0, 0.1).color((180, 186, 196))
    plank = box(0.9, 0.08, 0.3, 1.06).translate(0.4, 0, -0.1).gradient_y((170, 120, 70), (220, 170, 110))
    parts += [saw, plank]
    return [m.to_part() for m in parts]


def alchemy_table():
    rng = np.random.default_rng(9)
    parts = workbench()[:5]
    extra = []
    cols = [(120, 220, 120), (220, 90, 120), (110, 160, 240), (240, 200, 90)]
    for k, c in enumerate(cols):
        x = -0.7 + k * 0.45
        flask = icosphere(1).scale(0.14, 0.16, 0.14).translate(x, 1.2, 0.1)
        flask.gradient_y(tuple(int(v * 0.6) for v in c), c)
        neck = cylinder(0.05, 0.04, 0.18, segs=6, y0=1.3).translate(x, 0, 0.1).color((200, 220, 230))
        extra += [flask.faceted(), neck.faceted()]
    return parts + [m.to_part() for m in extra]


def notice_board():
    parts = []
    for sx in (-0.9, 0.9):
        parts.append(box(0.16, 2.4, 0.16).translate(sx, 0, 0).gradient_y((80, 52, 34), (130, 90, 56)))
    parts.append(box(2.0, 1.1, 0.12, 1.1).gradient_y((130, 90, 56), (176, 128, 80)))
    roof = box(2.4, 0.12, 0.5, 2.35).color((150, 60, 40))
    parts.append(roof)
    papers = []
    rng = np.random.default_rng(2)
    for k in range(5):
        p = box(0.32, 0.4, 0.02, 1.2 + rng.uniform(0, 0.5)).translate(-0.7 + k * 0.35, 0, 0.08).color((240, 232, 210))
        papers.append(p)
    parts.append(merge(papers))
    return [m.to_part() for m in parts]


def signpost():
    parts = [box(0.14, 2.0, 0.14).gradient_y((80, 52, 34), (130, 90, 56))]
    for k, (y, a) in enumerate([(1.7, 0.3), (1.35, -0.6)]):
        s = box(1.0, 0.24, 0.06, y).translate(0.45, 0, 0).rotate_y(a).gradient_y((150, 104, 64), (196, 146, 92))
        parts.append(s)
    return [m.to_part() for m in parts]


def fishing_spot():
    # little bobbing reeds + lilypads ring; the ripples are drawn at runtime
    rng = np.random.default_rng(4)
    reeds = []
    for k in range(5):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(1.2, 1.8)
        rd = cylinder(0.03, 0.02, rng.uniform(0.8, 1.4), segs=4, cap_top=False).translate(math.cos(a) * r, -0.3, math.sin(a) * r)
        rd.gradient_y((50, 90, 40), (150, 190, 90))
        reeds.append(rd)
    return [merge(reeds).faceted().to_part()]


# ------------------------------------------------------------------ tools
def tool(kind):
    """Held tools in hand-slot space: handle along +Y, grip at the origin."""
    handle = cylinder(0.045, 0.04, 1.15, segs=6, cap_top=True, cap_bottom=True, y0=-0.25).gradient_y((90, 60, 36), (150, 104, 64))
    parts = [handle.faceted()]
    if kind == 'pickaxe':
        head = merge([cone(0.07, 0.45, segs=5).rotate_z(-math.pi / 2).translate(0.05, 0.82, 0),
                      cone(0.07, 0.45, segs=5).rotate_z(math.pi / 2).translate(-0.05, 0.82, 0)])
        head.gradient_y((90, 92, 100), (190, 194, 204))
        parts.append(head.faceted())
    elif kind == 'hammer':
        head = box(0.36, 0.22, 0.22, 0.75).gradient_y((70, 70, 78), (160, 162, 170))
        parts.append(head)
    elif kind == 'sickle':
        blade = []
        for k in range(7):
            a = 0.15 + k * 0.38
            seg = box(0.06, 0.16, 0.035).rotate_z(-a).translate(math.sin(a) * 0.32, 0.62 + (1 - math.cos(a)) * -0.32 + 0.32, 0)
            blade.append(seg)
        bl = merge(blade).gradient_y((150, 156, 168), (225, 230, 240))
        parts.append(bl)
    elif kind == 'rod':
        handle.pos[:, 1] *= 1.9
        rod = cylinder(0.025, 0.012, 1.6, segs=5, y0=1.2).gradient_y((150, 110, 60), (210, 170, 110))
        reel = cylinder(0.08, 0.08, 0.08, segs=8, cap_top=True, cap_bottom=True, y0=0.2).rotate_z(math.pi / 2).translate(0.04, 0, 0).color((140, 140, 150))
        parts = [handle.faceted(), rod.faceted(), reel.faceted()]
    elif kind == 'torch':
        head = cylinder(0.08, 0.11, 0.2, segs=6, y0=0.85).color((60, 40, 30))
        parts.append(head.faceted())
    return [m.to_part() for m in parts]


# ------------------------------------------------------------- item props
def item_log(kind='oak'):
    lg = cylinder(0.22, 0.22, 0.9, segs=7, cap_top=True, cap_bottom=True, y0=-0.45).rotate_z(math.pi / 2)
    lg.gradient_y(BARK.get(kind, BARK['oak'])[0], BARK.get(kind, BARK['oak'])[1])
    # light rings on caps
    caps = np.abs(lg.pos[:, 0]) > 0.44
    lg.clr[caps, :3] = (225, 190, 140)
    return [lg.faceted().to_part()]


def item_ore(kind):
    rng = np.random.default_rng(len(kind))
    m = icosphere(0).scale(0.32, 0.26, 0.3).displace(rng, 0.06, 2.5)
    m.gradient_y(*ORE[kind])
    return [m.faceted().to_part()]


def item_ingot(kind):
    m = box(0.62, 0.18, 0.28).translate(0, -0.09, 0)
    m.pos[m.pos[:, 1] > 0, 0] *= 0.8
    m.pos[m.pos[:, 1] > 0, 2] *= 0.75
    lo, hi = ORE[kind]
    m.gradient_y(lo, hi)
    return [m.to_part()]


def item_fish(body=(110, 150, 170), belly=(230, 230, 210), seed=1):
    m = icosphere(1).scale(0.45, 0.2, 0.12)
    m.gradient_y(belly, body)
    tail = cone(0.16, 0.24, segs=4).rotate_z(math.pi / 2).translate(-0.62, 0, 0).scale(1, 1, 0.3)
    tail.color(body)
    eye = icosphere(0).scale(0.035).translate(0.3, 0.05, 0.1).color((10, 10, 10))
    return [m.faceted().to_part(), tail.faceted().to_part(), eye.to_part(BF_UNLIT)]


def item_plank():
    m = box(0.9, 0.08, 0.26).translate(0, -0.04, 0).gradient_y((170, 120, 70), (220, 170, 110))
    return [m.to_part()]


def item_herb_bundle(color):
    rng = np.random.default_rng(3)
    leaves = []
    for k in range(6):
        l = icosphere(0).scale(0.08, 0.04, 0.3).translate(0, 0, 0.2).rotate_x(-0.9).rotate_y(k * 1.05)
        leaves.append(l)
    lv = merge(leaves).gradient_y((60, 120, 50), (150, 210, 100))
    tie = cylinder(0.06, 0.06, 0.06, segs=6).translate(0, -0.05, 0).color((200, 160, 90))
    out = [lv.faceted().to_part(), tie.faceted().to_part()]
    if color:
        fl = icosphere(0).scale(0.08).translate(0, 0.32, 0).color(color)
        out.append(fl.faceted().to_part())
    return out


def item_potion(color):
    flask = icosphere(1).scale(0.22, 0.24, 0.22)
    flask.gradient_y(tuple(int(c * 0.55) for c in color), tuple(min(255, int(c * 1.1 + 20)) for c in color))
    neck = cylinder(0.07, 0.06, 0.18, segs=6, y0=0.18).color((210, 225, 235))
    cork = cylinder(0.075, 0.075, 0.08, segs=6, y0=0.35).color((150, 100, 60))
    return [flask.faceted().to_part(), neck.faceted().to_part(), cork.faceted().to_part()]


def item_bobber():
    float_ = icosphere(1).scale(0.11, 0.13, 0.11)
    float_.gradient_y((245, 242, 232), (225, 50, 40))
    stick = cylinder(0.018, 0.012, 0.16, segs=5, y0=0.1).color((60, 50, 40))
    return [float_.to_part(), stick.faceted().to_part()]


def item_bread():
    m = icosphere(1).scale(0.36, 0.2, 0.24).translate(0, 0.0, 0)
    m.gradient_y((170, 100, 40), (230, 170, 90))
    return [m.faceted().to_part()]


def item_gem(color):
    m = merge([cone(0.2, 0.18, segs=6), cone(0.2, 0.3, segs=6).rotate_x(math.pi)])
    m.gradient_y(tuple(int(c * 0.5) for c in color), color)
    return [m.faceted().to_part()]


def item_ember():
    m = icosphere(1).scale(0.22).displace(np.random.default_rng(2), 0.04, 3)
    m.gradient_y((200, 60, 20), (255, 220, 120))
    return [m.faceted().to_part(BF_UNLIT)]


def item_cloth(color):
    m = box(0.6, 0.12, 0.5).translate(0, -0.06, 0)
    m.gradient_y(tuple(int(c * 0.6) for c in color), color)
    return [m.to_part()]


def item_scroll():
    roll = cylinder(0.09, 0.09, 0.6, segs=8, cap_top=True, cap_bottom=True, y0=-0.3).rotate_z(math.pi / 2).color((236, 222, 188))
    band = cylinder(0.1, 0.1, 0.06, segs=8, y0=-0.03).rotate_z(math.pi / 2).color((180, 50, 40))
    return [roll.faceted().to_part(), band.faceted().to_part()]


# --------------------------------------------------------------- textures
def tex_grass_card(seed=1):
    """Alpha cutout grass tuft, painterly (RGBA 64x64)."""
    rng = np.random.default_rng(seed)
    W = 64
    img = Image.new('RGBA', (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for k in range(26):
        x0 = rng.uniform(6, W - 6)
        h = rng.uniform(0.45, 0.98) * W
        bend = rng.uniform(-14, 14)
        w = rng.uniform(2.2, 4.2)
        g = rng.uniform(0.75, 1.15)
        col = (int(90 * g), int(170 * g), int(60 * g), 255)
        tip = (x0 + bend, W - h)
        d.polygon([(x0 - w, W), (x0 + w, W), tip], fill=col)
    arr = np.asarray(img).astype(np.float64)
    # vertical gradient: darker at the base
    y = np.linspace(0, 1, W)[:, None]
    arr[..., :3] *= (0.55 + 0.6 * (1 - y))[..., None]
    arr[..., :3] = np.clip(arr[..., :3], 0, 255)
    return arr.astype(np.uint8)


def tex_flower_card(color, seed=2):
    rng = np.random.default_rng(seed)
    W = 64
    img = Image.new('RGBA', (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for k in range(5):
        x = rng.uniform(10, W - 10)
        top = rng.uniform(8, 30)
        d.line([(x, W), (x + rng.uniform(-4, 4), top)], fill=(70, 140, 60, 255), width=2)
        r = rng.uniform(4, 6.5)
        for p in range(5):
            a = p * 2 * math.pi / 5
            cx, cy = x + math.cos(a) * r * 0.8, top + math.sin(a) * r * 0.8
            d.ellipse([cx - r * 0.6, cy - r * 0.6, cx + r * 0.6, cy + r * 0.6], fill=tuple(color) + (255,))
        d.ellipse([x - 2, top - 2, x + 2, top + 2], fill=(250, 220, 90, 255))
    return np.asarray(img).copy()


def tex_soft_dot(size=32, power=2.0):
    y, x = np.mgrid[0:size, 0:size]
    r = np.sqrt((x - size / 2 + 0.5) ** 2 + (y - size / 2 + 0.5) ** 2) / (size / 2)
    a = np.clip(1 - r, 0, 1) ** power
    out = np.zeros((size, size, 4), np.uint8)
    out[..., :3] = 255
    out[..., 3] = (a * 255).astype(np.uint8)
    return out


def tex_noise_detail(size=64, seed=5):
    """Grey detail texture (mean ~128) used to give terrain a painted grain."""
    rng = np.random.default_rng(seed)
    acc = np.zeros((size, size))
    for octave, amp in ((4, 0.5), (8, 0.3), (16, 0.2), (32, 0.12)):
        n = rng.uniform(-1, 1, size=(octave, octave))
        im = Image.fromarray(((n + 1) * 127.5).astype(np.uint8)).resize((size, size), Image.BICUBIC)
        # make it tile by blending wrapped copy
        a = np.asarray(im).astype(np.float64) / 127.5 - 1
        acc += a * amp
    acc = (acc - acc.mean()) / (acc.std() + 1e-6)
    # tileable: blend with rolled versions
    acc = (acc + np.roll(acc, size // 2, 0) + np.roll(acc, size // 2, 1) + np.roll(np.roll(acc, size // 2, 0), size // 2, 1)) / 2
    v = np.clip(128 + acc * 18, 0, 255).astype(np.uint8)
    out = np.zeros((size, size, 4), np.uint8)
    out[..., 0] = out[..., 1] = out[..., 2] = v
    out[..., 3] = 255
    return out


def tex_water(size=64, seed=8):
    """Tileable caustic-ish wave pattern (intensity)."""
    y, x = np.mgrid[0:size, 0:size] / size * 2 * math.pi
    rng = np.random.default_rng(seed)
    v = np.zeros((size, size))
    for k in range(5):
        fx, fy = rng.integers(1, 4, size=2)
        ph = rng.uniform(0, 6.28)
        v += np.sin(x * fx + y * fy + ph)
    v = np.abs(v) / 5.0
    v = 1 - v
    v = v ** 3
    out = np.zeros((size, size, 4), np.uint8)
    g = np.clip(v * 255, 0, 255).astype(np.uint8)
    out[..., 0] = out[..., 1] = out[..., 2] = g
    out[..., 3] = g
    return out


def tex_ring(size=64, width=0.12):
    y, x = np.mgrid[0:size, 0:size]
    r = np.sqrt((x - size / 2 + 0.5) ** 2 + (y - size / 2 + 0.5) ** 2) / (size / 2)
    a = np.clip(1 - np.abs(r - 0.8) / width, 0, 1)
    out = np.zeros((size, size, 4), np.uint8)
    out[..., :3] = 255
    out[..., 3] = (a * 255).astype(np.uint8)
    return out


def tex_flame(size=32):
    y, x = np.mgrid[0:size, 0:size] / (size - 1)
    cx = 0.5
    w = 0.42 * (1 - y) ** 0.6 * (y ** 0.35 + 0.05)
    d = np.abs(x - cx) / np.maximum(w, 1e-3)
    a = np.clip(1 - d, 0, 1) ** 0.8 * np.clip(y * 3, 0, 1)
    out = np.zeros((size, size, 4), np.uint8)
    out[..., :3] = 255
    out[..., 3] = (a * 255).astype(np.uint8)
    return out[::-1].copy()


def tex_leaf(size=16):
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([2, 4, size - 2, size - 4], fill=(255, 255, 255, 255))
    return np.asarray(img).copy()


def tex_spark(size=32):
    y, x = np.mgrid[0:size, 0:size]
    cx = cy = size / 2 - 0.5
    dx, dy = np.abs(x - cx) / (size / 2), np.abs(y - cy) / (size / 2)
    a = np.clip(1 - (dx * 6 * dy + np.minimum(dx, dy) * 3 + np.sqrt(dx * dx + dy * dy) * 0.6), 0, 1)
    out = np.zeros((size, size, 4), np.uint8)
    out[..., :3] = 255
    out[..., 3] = (np.clip(a, 0, 1) ** 1.5 * 255).astype(np.uint8)
    return out


def tex_shadow(size=32):
    y, x = np.mgrid[0:size, 0:size]
    r = np.sqrt((x - size / 2 + 0.5) ** 2 + (y - size / 2 + 0.5) ** 2) / (size / 2)
    a = np.clip(1 - r, 0, 1) ** 0.7
    out = np.zeros((size, size, 4), np.uint8)
    out[..., 3] = (a * 200).astype(np.uint8)
    return out


def tex_cloud_puff(size=64, seed=3):
    rng = np.random.default_rng(seed)
    acc = np.zeros((size, size))
    y, x = np.mgrid[0:size, 0:size] / size
    for k in range(9):
        cx, cy = rng.uniform(0.25, 0.75), rng.uniform(0.35, 0.65)
        r = rng.uniform(0.12, 0.25)
        acc = np.maximum(acc, np.clip(1 - np.sqrt((x - cx) ** 2 + ((y - cy) * 1.4) ** 2) / r, 0, 1))
    a = np.clip(acc * 1.6, 0, 1)
    out = np.zeros((size, size, 4), np.uint8)
    shade = np.clip(1.0 - (y - 0.3) * 0.5, 0.75, 1.0)
    out[..., 0] = (255 * shade).astype(np.uint8)
    out[..., 1] = (255 * shade).astype(np.uint8)
    out[..., 2] = (255 * np.clip(shade + 0.05, 0, 1)).astype(np.uint8)
    out[..., 3] = (a * 255).astype(np.uint8)
    return out


# ------------------------------------------------------------------ build
TREES = {
    'pr/oak_a': ('oak', 101, 4.4, 1.0), 'pr/oak_b': ('oak', 102, 4.0, 1.1), 'pr/oak_c': ('oak', 103, 4.8, 0.95),
    'pr/birch_a': ('birch', 111, 5.0, 0.8), 'pr/birch_b': ('birch', 112, 4.6, 0.85),
    'pr/autumn_a': ('autumn', 121, 4.3, 1.0), 'pr/autumn_b': ('autumn', 122, 4.0, 1.05),
    'pr/blossom_a': ('blossom', 131, 3.8, 0.95), 'pr/willow_a': ('willow', 141, 4.4, 1.15),
    'pr/darkoak_a': ('dark', 151, 4.8, 1.05),
}


def build_all(b, stages=None):
    if stages and 'proc' not in stages:
        return
    add = b.add_model
    for name, (kind, seed, h, s) in TREES.items():
        add(name, tree_broadleaf(seed, kind, h, s))
    for i in range(2):
        add('pr/poplar_%s' % 'ab'[i], tree_poplar(501 + i, [8.0, 9.5][i]))
        add('pr/umbrella_%s' % 'ab'[i], tree_umbrella(511 + i, [6.0, 7.0][i]))
        add('pr/birchs_%s' % 'ab'[i], tree_birch_slender(521 + i, [6.5, 7.5][i]))
        add('pr/willowc_%s' % 'ab'[i], tree_willow_curtain(531 + i, [4.8, 5.4][i]))
        add('pr/maple_%s' % 'ab'[i], tree_maple(541 + i, [5.0, 5.6][i]))
        add('pr/oldoak_%s' % 'ab'[i], tree_old_oak(551 + i, [5.2, 5.8][i]))
    add('pr/pine_a', tree_pine(201, 'dark', 7.5))
    add('pr/pine_b', tree_pine(202, 'dark', 6.0))
    add('pr/pine_c', tree_pine(203, 'oak', 8.5))
    add('pr/bush_a', bush(301, 'oak', 1.0))
    add('pr/bush_b', bush(302, 'dark', 1.2))
    add('pr/bush_berry', bush(303, 'oak', 1.0, berries=(200, 40, 60)))
    add('pr/bush_autumn', bush(304, 'autumn', 0.9))
    for i in range(4):
        add('pr/rock_%d' % i, [rock(400 + i, [1.0, 1.6, 2.4, 3.4][i]).to_part()])
    for kind in ('copper', 'tin', 'iron', 'silver', 'gold', 'crystal', 'coal'):
        add('pr/ore_' + kind, ore_node(kind, int(rng_for(kind).integers(0, 65535))))
    for kind in ('mint', 'lavender', 'sunpetal', 'emberroot', 'moonbloom', 'sage'):
        add('pr/herb_' + kind, herb(kind, len(kind)))
    add('pr/mushroom_red', mushroom((200, 50, 40), 1))
    add('pr/mushroom_glow', mushroom((80, 170, 255), 2, glow=True))
    for name, col in (('green', (110, 200, 90)), ('blue', (90, 150, 240)), ('red', (230, 90, 80)), ('gold', (240, 190, 60)), ('purple', (170, 100, 220))):
        add('pr/slime_' + name, slime(col))
    add('pr/hearth', great_hearth())
    add('pr/campfire', campfire())
    add('pr/anvil', anvil())
    add('pr/forge', forge())
    add('pr/cookpot', cooking_pot())
    add('pr/workbench', workbench())
    add('pr/alchemy', alchemy_table())
    add('pr/noticeboard', notice_board())
    add('pr/signpost', signpost())
    add('pr/reeds', fishing_spot())
    for t in ('pickaxe', 'hammer', 'sickle', 'rod', 'torch'):
        add('itm/' + t, tool(t))
    # item props (used for icons and drops)
    for k in ('oak', 'birch', 'autumn', 'dark', 'willow'):
        add('ip/log_' + k, item_log(k))
    for k in ('copper', 'tin', 'iron', 'silver', 'gold', 'coal', 'stone', 'crystal'):
        add('ip/ore_' + k, item_ore(k))
    for k in ('copper', 'tin', 'iron', 'silver', 'gold'):
        add('ip/ingot_' + k, item_ingot(k))
    add('ip/ingot_bronze', item_ingot('copper'))
    add('ip/fish_trout', item_fish((110, 150, 120), (230, 220, 200)))
    add('ip/fish_perch', item_fish((150, 160, 70), (240, 230, 190)))
    add('ip/fish_pike', item_fish((90, 120, 90), (220, 220, 190)))
    add('ip/fish_salmon', item_fish((200, 110, 90), (250, 220, 200)))
    add('ip/fish_golden', item_fish((240, 190, 60), (255, 240, 180)))
    add('ip/fish_cooked', item_fish((150, 90, 40), (210, 150, 80)))
    add('ip/plank', item_plank())
    for k, c in (('mint', None), ('lavender', (150, 110, 220)), ('sunpetal', (250, 210, 60)), ('emberroot', (230, 80, 50)), ('moonbloom', (170, 210, 255)), ('sage', (200, 210, 190))):
        add('ip/herb_' + k, item_herb_bundle(c))
    for k, c in (('red', (220, 50, 60)), ('blue', (60, 120, 230)), ('green', (80, 200, 90)), ('gold', (240, 190, 50)), ('purple', (160, 80, 210))):
        add('ip/potion_' + k, item_potion(c))
    add('ip/bread', item_bread())
    add('ip/bobber', item_bobber())
    add('ip/gem_ruby', item_gem((230, 40, 70)))
    add('ip/gem_sapphire', item_gem((50, 100, 240)))
    add('ip/gem_emerald', item_gem((40, 200, 100)))
    add('ip/ember', item_ember())
    add('ip/cloth', item_cloth((200, 190, 160)))
    add('ip/leather', item_cloth((150, 96, 60)))
    add('ip/scroll', item_scroll())
    # textures
    # foliage atlas 128x128: [grass A | grass B] / [flowers yellow | flowers purple+white]
    atlas = np.zeros((128, 128, 4), np.uint8)
    atlas[0:64, 0:64] = tex_grass_card(1)
    atlas[0:64, 64:128] = tex_grass_card(7)
    atlas[64:128, 0:64] = np.maximum(tex_flower_card((250, 220, 80)), tex_grass_card(11) * (np.random.default_rng(1).uniform(size=(64, 64, 1)) > 2))
    fl = tex_flower_card((200, 140, 240), 4)
    fw = tex_flower_card((250, 250, 250), 3)
    atlas[64:128, 64:128] = np.where(fw[..., 3:4] > fl[..., 3:4], fw, fl)
    b.add_texture('tx/foliage', atlas, 'RGB5A3', wrap=(0, 0))
    b.add_texture('tx/grass', tex_grass_card(1), 'RGB5A3', wrap=(0, 0))
    b.add_texture('tx/grass2', tex_grass_card(7), 'RGB5A3', wrap=(0, 0))
    b.add_texture('tx/flower_y', tex_flower_card((250, 220, 80)), 'RGB5A3')
    b.add_texture('tx/flower_w', tex_flower_card((250, 250, 250), 3), 'RGB5A3')
    b.add_texture('tx/flower_p', tex_flower_card((200, 140, 240), 4), 'RGB5A3')
    b.add_texture('tx/flower_r', tex_flower_card((240, 90, 90), 5), 'RGB5A3')
    b.add_texture('tx/dot', tex_soft_dot(32, 1.6), 'IA8')
    b.add_texture('tx/glow', tex_soft_dot(64, 2.6), 'IA8')
    b.add_texture('tx/ring', tex_ring(64), 'IA8')
    b.add_texture('tx/flame', tex_flame(32), 'IA8')
    b.add_texture('tx/leaf', tex_leaf(16), 'IA8')
    b.add_texture('tx/spark', tex_spark(32), 'IA8')
    b.add_texture('tx/shadow', tex_shadow(32), 'IA4')
    b.add_texture('tx/cloud', tex_cloud_puff(64), 'RGB5A3')
    b.add_texture('tx/detail', tex_noise_detail(64), 'I8', wrap=(1, 1), levels=4)
    b.add_texture('tx/water', tex_water(64), 'IA8', wrap=(1, 1), levels=3)

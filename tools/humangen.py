"""Procedural low-poly humans: chunky, flat-shaded, adult proportions and
simple blocky features, rigged rigidly (one bone per body part) to the
humanoid animation skeleton. Every character is built from parameters
(build, skin, hair, beard, outfit, colours, headwear), so the town gets
many distinct people without any hand-modelled art.

Geometry is authored in the *posed* rest space of the engine's stretched
skeleton (see rigpose.py) and mapped back to bind space per bone, so the
engine's proportion tweaks and the animations line up exactly.
"""
import math
import numpy as np

from meshbuild import Part, compute_flat_normals, BF_DOUBLESIDED
from meshkit import Mesh, merge, cylinder, box, icosphere, cone
from rigpose import stretched_rest

SKIN = {'fair': (236, 196, 160), 'light': (222, 172, 130), 'tan': (196, 140, 96), 'brown': (150, 100, 66), 'deep': (104, 70, 48)}
HAIR = {'black': (40, 34, 32), 'brown': (98, 62, 36), 'auburn': (150, 64, 34), 'blond': (222, 186, 104), 'grey': (170, 168, 164),
        'white': (232, 230, 226), 'red': (176, 52, 30)}


# ------------------------------------------------------------ geometry helpers
def _frame(d, up=(0, 0, 1)):
    """Orthonormal basis with Y along d."""
    y = np.asarray(d, float)
    y = y / np.linalg.norm(y)
    up = np.asarray(up, float)
    if abs(np.dot(up, y)) > 0.95:
        up = np.array([1.0, 0, 0])
    x = np.cross(y, up)
    x /= np.linalg.norm(x)
    z = np.cross(x, y)
    return np.stack([x, y, z], axis=1)


def limb(p0, p1, r0, r1, sides=6, sx=1.0, sz=1.0, caps=(True, True), up=(0, 0, 1), twist=0.0):
    """Tapered prism from p0 to p1 (elliptical section sx/sz)."""
    p0, p1 = np.asarray(p0, float), np.asarray(p1, float)
    L = np.linalg.norm(p1 - p0)
    m = cylinder(r0, r1, L, segs=sides, cap_top=caps[1], cap_bottom=caps[0])
    if twist:
        m.rotate_y(twist)
    m.scale(sx, 1, sz)
    R = _frame(p1 - p0, up)
    m.pos = (R @ m.pos.T).T + p0
    return m


def obox(center, size, rot_y=0.0, rot_x=0.0, rot_z=0.0):
    b = box(*size, y0=-size[1] / 2)
    if rot_z:
        b.rotate_z(rot_z)
    if rot_x:
        b.rotate_x(rot_x)
    if rot_y:
        b.rotate_y(rot_y)
    b.translate(*center)
    return b


def blob(center, rad, sub=0, sx=1, sy=1, sz=1):
    m = icosphere(sub).scale(rad * sx, rad * sy, rad * sz).translate(*center)
    return m


def col(m, rgb):
    m.color(tuple(int(c) for c in rgb))
    return m


def shade(rgb, k):
    return tuple(int(np.clip(c * k, 0, 255)) for c in rgb)


# ------------------------------------------------------------- the generator
class Person:
    def __init__(self, rig):
        self.rig = rig
        self.idx = {n: i for i, n in enumerate(rig.names)}
        self.base, self.jm = stretched_rest(rig)
        self.parts = {}   # joint name -> list of meshes

    def P(self, name):
        return self.base[self.idx[name]][:3, 3].copy()

    def add(self, joint, mesh):
        self.parts.setdefault(joint, []).append(mesh)

    def to_parts(self):
        """World-space meshes -> bind-space Parts rigidly bound to their joints."""
        out = []
        for joint, meshes in self.parts.items():
            j = self.idx[joint]
            m = merge(meshes).faceted()
            ibm = np.asarray(self.rig.ibm[j], float)
            M = np.linalg.inv(ibm) @ np.linalg.inv(self.jm[j])
            pos = (M[:3, :3] @ m.pos.T).T + M[:3, 3]
            fn = compute_flat_normals(pos, m.idx)
            nrm = np.zeros_like(pos)
            nrm[m.idx.reshape(-1)] = np.repeat(fn, 3, axis=0)
            clr = np.clip(np.round(m.clr), 0, 255).astype(np.uint8)
            out.append(Part(pos, m.idx, nrm, None, clr, None, 0, np.full(len(pos), j, np.int64)))
        return out


def build_person(rig, spec):
    """spec keys: skin, hair, hair_style, beard, build (0.85..1.2), outfit, c1, c2, legs, boots,
    hat, hat_c, belt, gloves, apron."""
    p = Person(rig)
    sk = SKIN[spec.get('skin', 'light')]
    hc = HAIR[spec.get('hair', 'brown')]
    bw = spec.get('build', 1.0) * 1.22       # shoulder/girth multiplier (chunky, RuneScape-like)
    lw = 1.25                                # limb thickness
    c1 = spec.get('c1', (70, 96, 160))       # main garment
    c2 = spec.get('c2', shade(c1, 0.7))      # trim / secondary
    lc = spec.get('legs', (80, 66, 52))      # trousers
    bc = spec.get('boots', (70, 46, 30))
    outfit = spec.get('outfit', 'tunic')
    robe = outfit == 'robe'
    armor = outfit == 'armor'

    hips, spine, chest, head = p.P('hips'), p.P('spine'), p.P('chest'), p.P('head')
    # ---- torso: pelvis / waist / chest blocks (hips, spine, chest bones)
    shoulder_y = p.P('upperarm.l')[1]
    neck_y = head[1]
    p.add('hips', col(limb(hips + [0, -0.12, 0], hips + [0, 0.12, 0], 0.17 * bw, 0.16 * bw, 8, 1.0, 0.72), lc))
    torso_c = c1 if not armor else (112, 118, 130)
    p.add('spine', col(limb(spine + [0, -0.06, 0], chest + [0, -0.04, 0], 0.155 * bw, 0.17 * bw, 8, 1.0, 0.68), torso_c))
    p.add('chest', col(limb(chest + [0, -0.06, 0], [0, shoulder_y + 0.06, 0], 0.18 * bw, 0.205 * bw, 8, 1.0, 0.62, caps=(False, False)), torso_c))
    # shoulders yoke
    p.add('chest', col(limb([0, shoulder_y + 0.06, 0], [0, shoulder_y + 0.11, 0], 0.205 * bw, 0.09, 8, 1.0, 0.62), torso_c))
    # neck
    p.add('head', col(limb([0, shoulder_y + 0.08, 0], [0, neck_y + 0.04, 0.0], 0.055, 0.05, 6), sk))
    if spec.get('belt', True) and not robe:
        p.add('spine', col(limb(spine + [0, -0.07, 0], spine + [0, -0.02, 0], 0.165 * bw, 0.165 * bw, 8, 1.0, 0.7), (60, 40, 26)))
        p.add('spine', col(obox(spine + [0, -0.045, 0.118 * bw], (0.07, 0.06, 0.02)), (200, 170, 70)))
    if outfit in ('tunic', 'armor', 'apron', 'vest'):
        # tunic skirt panels over the hips (front/back/sides)
        skirt = c2 if armor else c1
        for a in range(6):
            ang = a * math.pi / 3
            cx, cz = math.sin(ang) * 0.15 * bw, math.cos(ang) * 0.11 * bw
            p.add('hips', col(obox([cx, hips[1] - 0.12, cz], (0.15 * bw, 0.2, 0.03), rot_y=ang), skirt))
    if armor:
        # tabard + pauldrons
        p.add('chest', col(obox([0, chest[1] + 0.04, 0.128 * bw], (0.2 * bw, 0.34, 0.02)), c1))
        p.add('hips', col(obox([0, hips[1] - 0.14, 0.135 * bw], (0.18 * bw, 0.26, 0.02)), c1))
        for s in (1, -1):
            p.add('upperarm.' + ('l' if s > 0 else 'r'), col(blob([s * 0.25 * bw, shoulder_y + 0.04, 0], 0.13, 0, 1.1, 0.7, 1.0), (128, 134, 146)))
    if outfit == 'apron':
        p.add('chest', col(obox([0, chest[1] - 0.02, 0.122 * bw], (0.2 * bw, 0.3, 0.015)), spec.get('apron', (200, 190, 170))))
        p.add('hips', col(obox([0, hips[1] - 0.16, 0.13 * bw], (0.22 * bw, 0.34, 0.015)), spec.get('apron', (200, 190, 170))))
    if outfit == 'vest':
        p.add('chest', col(limb(chest + [0, -0.02, 0], [0, shoulder_y + 0.04, 0], 0.19 * bw, 0.21 * bw, 8, 1.0, 0.64, caps=(False, False)), c2))
    if robe:
        # long robe skirt from the waist to the ankles (on the hips bone)
        p.add('hips', col(limb([0, hips[1] + 0.06, 0], [0, 0.09, 0], 0.17 * bw, 0.27 * bw, 10, 1.0, 0.85, caps=(True, False)), c1))
        p.add('hips', col(limb([0, 0.13, 0], [0, 0.07, 0], 0.27 * bw, 0.28 * bw, 10, 1.0, 0.86, caps=(False, False)), c2))
        p.add('chest', col(obox([0, chest[1] + 0.02, 0.118 * bw], (0.06, 0.36, 0.02)), c2))
    # ---- arms (sleeves follow the garment; bare forearms for vests)
    for side in ('l', 'r'):
        s = 1 if side == 'l' else -1
        ua, la, wr, hd = p.P('upperarm.' + side), p.P('lowerarm.' + side), p.P('wrist.' + side), p.P('hand.' + side)
        sleeve = (c1 if not armor else (112, 118, 130)) if outfit != 'vest' else sk
        cuff = c2
        p.add('upperarm.' + side, col(limb(ua + [-s * 0.02, 0, 0], la, 0.07 * bw * lw / 1.22, 0.058 * bw * lw / 1.22, 6, 1.0, 0.95, up=(0, 1, 0)), sleeve))
        fore = sleeve if (robe or armor or spec.get('long_sleeves', True)) and outfit != 'vest' else sk
        p.add('lowerarm.' + side, col(blob(la, 0.06 * bw * lw / 1.22, 0), sleeve))   # elbow cap
        p.add('lowerarm.' + side, col(limb(la, wr, 0.057 * bw * lw / 1.22, 0.05 * lw, 6, 1.0, 0.9, up=(0, 1, 0)), fore))
        if robe:
            p.add('lowerarm.' + side, col(limb(wr + [-s * 0.1, 0, 0], wr + [s * 0.01, 0, 0], 0.075, 0.09, 6, 1, 1, up=(0, 1, 0)), c2))
        elif fore is sk or spec.get('gloves'):
            pass
        else:
            p.add('lowerarm.' + side, col(limb(wr + [-s * 0.05, 0, 0], wr + [s * 0.005, 0, 0], 0.052, 0.054, 6, 1, 1, up=(0, 1, 0)), cuff))
        hand_c = spec.get('gloves') or sk
        p.add('hand.' + side, col(obox(hd + [s * 0.045, -0.005, 0.005], (0.11, 0.06, 0.1)), hand_c))
        p.add('hand.' + side, col(obox(hd + [s * 0.01, 0.0, 0.055], (0.05, 0.035, 0.04)), hand_c))   # thumb
    # ---- legs
    for side in ('l', 'r'):
        ul, ll, ft, to = p.P('upperleg.' + side), p.P('lowerleg.' + side), p.P('foot.' + side), p.P('toes.' + side)
        p.add('upperleg.' + side, col(limb(ul + [0, 0.04, 0], ll, 0.092 * bw * lw / 1.22, 0.07 * lw, 7, 1, 1), lc))
        p.add('lowerleg.' + side, col(blob(ll, 0.066 * lw, 0), lc))   # knee cap
        boot_top = ll + (ft - ll) * 0.45
        p.add('lowerleg.' + side, col(limb(ll, boot_top, 0.067 * lw, 0.058 * lw, 7, 1, 1), lc))
        p.add('lowerleg.' + side, col(limb(boot_top + [0, 0.02, 0], ft, 0.07 * lw, 0.062 * lw, 7, 1, 1), bc))
        p.add('foot.' + side, col(obox([ft[0], 0.055, ft[2] + 0.03], (0.13, 0.11, 0.22)), bc))
        p.add('toes.' + side, col(obox([to[0], 0.045, to[2] + 0.045], (0.1, 0.09, 0.09)), shade(bc, 0.85)))
    # ---- head (bone scaled by 0.74 at runtime; authored in posed space)
    hy = head[1] + 0.13
    face = head + [0, 0.13, 0]
    skull = col(limb([0, head[1] + 0.015, 0.0], [0, head[1] + 0.25, -0.01], 0.105, 0.098, 8, 1.0, 1.05), sk)
    p.add('head', skull)
    p.add('head', col(limb([0, head[1] + 0.25, -0.01], [0, head[1] + 0.27, -0.015], 0.098, 0.06, 8, 1, 1.05), sk))   # crown
    p.add('head', col(obox([0, hy - 0.085, 0.07], (0.13, 0.05, 0.06)), shade(sk, 0.96)))                            # jaw
    p.add('head', col(obox([0, hy + 0.0, 0.11], (0.035, 0.06, 0.05), rot_x=0.25), shade(sk, 0.93)))                 # nose
    p.add('head', col(obox([0, hy + 0.05, 0.1], (0.16, 0.025, 0.03)), shade(hc, 0.9)))                              # brow
    for s in (1, -1):
        p.add('head', col(obox([s * 0.042, hy + 0.025, 0.103], (0.03, 0.022, 0.012)), (34, 28, 30)))                # eyes
        p.add('head', col(obox([s * 0.108, hy, -0.005], (0.025, 0.06, 0.04)), shade(sk, 0.92)))                     # ears
    p.add('head', col(obox([0, hy - 0.055, 0.1], (0.05, 0.012, 0.012)), shade(sk, 0.6)))                            # mouth
    style = spec.get('hair_style', 'short')
    top = head[1] + 0.27
    if style != 'bald':
        p.add('head', col(limb([0, head[1] + 0.17, -0.012], [0, top + 0.035, -0.02], 0.115, 0.08, 8, 1.0, 1.08), hc))
        p.add('head', col(obox([0, hy + 0.06, -0.07], (0.22, 0.14, 0.09)), hc))
    if style in ('long', 'ponytail', 'bun'):
        p.add('head', col(obox([0, hy - 0.04, -0.09], (0.21, 0.24 if style == 'long' else 0.12, 0.06)), hc))
    if style == 'long':
        for s in (1, -1):
            p.add('head', col(obox([s * 0.105, hy - 0.04, -0.02], (0.03, 0.2, 0.12)), hc))
    if style == 'ponytail':
        p.add('head', col(limb([0, hy + 0.03, -0.12], [0, hy - 0.2, -0.16], 0.035, 0.022, 5), hc))
    if style == 'bun':
        p.add('head', col(blob([0, top + 0.01, -0.07], 0.06, 0), hc))
    if style == 'mohawk':
        p.add('head', col(obox([0, top + 0.03, -0.01], (0.04, 0.08, 0.2)), hc))
    beard = spec.get('beard')
    if beard:
        bcol = HAIR[spec.get('beard_c', spec.get('hair', 'brown'))]
        if beard in ('full', 'long'):
            p.add('head', col(obox([0, hy - 0.08 - (0.04 if beard == 'long' else 0), 0.085], (0.15, 0.1 + (0.08 if beard == 'long' else 0), 0.07)), bcol))
        p.add('head', col(obox([0, hy - 0.035, 0.112], (0.1, 0.025, 0.025)), bcol))   # moustache
    # ---- headwear
    hat = spec.get('hat')
    hcol = spec.get('hat_c', (90, 60, 120))
    if hat == 'wizard':
        p.add('head', col(limb([0, top - 0.04, -0.01], [0, top - 0.02, -0.01], 0.2, 0.2, 10, 1, 1), hcol))
        p.add('head', col(limb([0, top - 0.02, -0.01], [0.05, top + 0.3, -0.06], 0.12, 0.01, 8, 1, 1), hcol))
    elif hat == 'hood':
        p.add('head', col(limb([0, head[1] + 0.02, -0.02], [0, top + 0.04, -0.03], 0.135, 0.105, 8, 1, 1.12, caps=(False, True)), hcol))
        p.add('head', col(obox([0, head[1] + 0.0, -0.02], (0.3, 0.06, 0.26)), hcol))
    elif hat == 'helm':
        p.add('head', col(limb([0, head[1] + 0.13, -0.01], [0, top + 0.05, -0.012], 0.122, 0.09, 8, 1, 1.06), hcol))
        p.add('head', col(obox([0, hy + 0.03, 0.115], (0.03, 0.12, 0.03)), shade(hcol, 0.8)))   # nose guard
    elif hat == 'straw':
        p.add('head', col(limb([0, top - 0.03, -0.01], [0, top - 0.01, -0.01], 0.24, 0.24, 10, 1, 1), hcol))
        p.add('head', col(limb([0, top - 0.01, -0.01], [0, top + 0.08, -0.01], 0.12, 0.1, 8, 1, 1), hcol))
    elif hat == 'cap':
        p.add('head', col(limb([0, top - 0.03, -0.01], [0, top + 0.04, -0.015], 0.122, 0.11, 8, 1, 1.06), hcol))
        p.add('head', col(obox([0, top - 0.02, 0.11], (0.16, 0.02, 0.08)), shade(hcol, 0.85)))
    elif hat == 'feather':
        p.add('head', col(limb([0, top - 0.04, -0.01], [0, top + 0.03, -0.01], 0.15, 0.12, 8, 1, 1.05), hcol))
        p.add('head', col(obox([0.08, top + 0.1, -0.06], (0.02, 0.2, 0.05), rot_z=-0.4), (240, 230, 220)))
    elif hat == 'bandana':
        p.add('head', col(limb([0, head[1] + 0.19, -0.012], [0, top + 0.04, -0.02], 0.118, 0.085, 8, 1, 1.08), hcol))
    # cloak / cape
    if spec.get('cape'):
        cc = spec['cape']
        p.add('chest', col(obox([0, chest[1] - 0.06, -0.15 * bw], (0.34 * bw, 0.62, 0.02), rot_x=-0.12), cc))
    return p.to_parts()


# ----------------------------------------------------------------- cast list
PEOPLE = {
    # player looks
    'hm/knight': dict(outfit='armor', c1=(40, 70, 160), c2=(30, 48, 110), hair='brown', hair_style='short', build=1.08, boots=(60, 60, 68), legs=(78, 80, 90), gloves=(96, 100, 110)),
    'hm/ranger': dict(outfit='vest', c1=(110, 74, 44), c2=(74, 96, 52), hair='red', hair_style='mohawk', beard='full', skin='tan', build=1.12, legs=(70, 60, 44)),
    'hm/mage': dict(outfit='robe', c1=(70, 52, 140), c2=(214, 180, 80), hair='blond', hair_style='long', skin='fair', build=0.95),
    'hm/rogue': dict(outfit='tunic', c1=(46, 92, 64), c2=(30, 60, 44), hair='black', hair_style='ponytail', skin='brown', build=0.98, legs=(52, 48, 46)),
    'hm/wanderer': dict(outfit='tunic', c1=(120, 96, 66), c2=(84, 64, 44), hat='hood', hat_c=(96, 70, 48), hair='brown', skin='light', cape=(96, 70, 48)),
    # townsfolk
    'hm/elder': dict(outfit='robe', c1=(110, 60, 120), c2=(230, 210, 150), hair='white', hair_style='long', skin='fair', build=0.92),
    'hm/smith': dict(outfit='apron', c1=(150, 92, 60), apron=(90, 64, 44), hair='black', hair_style='bald', beard='long', skin='tan', build=1.22, long_sleeves=False),
    'hm/cook': dict(outfit='apron', c1=(170, 70, 60), apron=(236, 230, 214), hair='auburn', hair_style='bun', skin='light'),
    'hm/carpenter': dict(outfit='tunic', c1=(80, 126, 70), c2=(60, 92, 52), hair='blond', hair_style='ponytail', skin='light', hat='bandana', hat_c=(170, 60, 40)),
    'hm/alchemist': dict(outfit='robe', c1=(40, 120, 120), c2=(200, 220, 210), hat='hood', hat_c=(30, 94, 96), hair='black', skin='deep'),
    'hm/fisher': dict(outfit='tunic', c1=(220, 176, 50), c2=(170, 130, 40), hair='grey', beard='full', hat='cap', hat_c=(220, 176, 50), skin='tan', build=1.05),
    'hm/captain': dict(outfit='armor', c1=(160, 40, 40), c2=(110, 30, 30), hair='auburn', hair_style='long', skin='light', hat=None, boots=(60, 60, 66)),
    'hm/merchant': dict(outfit='vest', c1=(220, 140, 60), c2=(120, 70, 140), hair='brown', beard='full', hat='feather', hat_c=(120, 70, 140), build=1.1, long_sleeves=True),
    'hm/farmer': dict(outfit='tunic', c1=(80, 110, 160), c2=(64, 84, 120), hat='straw', hat_c=(220, 196, 120), hair='brown', beard='full', skin='tan'),
    'hm/miner': dict(outfit='vest', c1=(96, 90, 84), c2=(60, 56, 52), hat='helm', hat_c=(150, 120, 60), hair='black', beard='full', skin='light', build=1.15),
    'hm/herbalist': dict(outfit='robe', c1=(96, 140, 70), c2=(200, 170, 110), hair='auburn', hair_style='long', skin='fair', build=0.9),
    'hm/hunter': dict(outfit='tunic', c1=(56, 80, 50), c2=(40, 56, 36), hat='hood', hat_c=(46, 66, 42), hair='black', skin='brown', cape=(46, 66, 42)),
    'hm/bard': dict(outfit='tunic', c1=(190, 60, 70), c2=(240, 210, 120), hat='feather', hat_c=(190, 60, 70), hair='blond', hair_style='ponytail', skin='light', build=0.95),
    'hm/priest': dict(outfit='robe', c1=(230, 226, 214), c2=(200, 160, 60), hair='grey', hair_style='bald', beard='full', skin='light'),
    'hm/child': dict(outfit='tunic', c1=(230, 150, 60), c2=(180, 110, 40), hair='blond', hair_style='bun', skin='fair', build=0.85),
    'hm/watchman': dict(outfit='armor', c1=(80, 80, 90), c2=(60, 60, 66), hat='helm', hat_c=(140, 144, 152), hair='brown', beard='full', build=1.12),
}

"""Procedural stylised humans in a soft, painterly fantasy style (smooth
rounded forms, heroic proportions, big hands and boots, painted colour
gradients), skinned to the humanoid animation skeleton.

Limbs, torso and neck are single lofted tubes whose vertices blend between
neighbouring bones around the joints, so elbows, knees, waist and neck bend
smoothly. Everything is authored in the posed rest space of the engine's
"athletic" skeleton (rigpose.py) and converted to bind space per vertex
using the vertex's own bone blend.
"""
import math
import numpy as np

from meshbuild import Part, compute_smooth_normals
from rigpose import stretched_rest

SKIN = {'fair': (240, 200, 168), 'light': (226, 176, 136), 'tan': (200, 144, 100), 'brown': (156, 104, 70), 'deep': (108, 72, 50)}
HAIR = {'black': (46, 38, 36), 'brown': (110, 70, 40), 'auburn': (160, 72, 38), 'blond': (232, 194, 112), 'grey': (178, 176, 172),
        'white': (238, 236, 230), 'red': (186, 58, 34)}


def shade(rgb, k):
    return tuple(float(np.clip(c * k, 0, 255)) for c in rgb)


def _frame(d, up):
    y = np.asarray(d, float)
    y = y / np.linalg.norm(y)
    up = np.asarray(up, float)
    if abs(np.dot(up, y)) > 0.95:
        up = np.array([0.0, 0, 1]) if abs(y[2]) < 0.9 else np.array([1.0, 0, 0])
    x = np.cross(y, up)
    x /= np.linalg.norm(x)
    z = np.cross(x, y)
    return x, y, z


class Piece:
    """A smooth mesh: positions, faces (CCW outside), colours, bone weights."""

    def __init__(self):
        self.pos, self.idx, self.clr, self.w = [], [], [], []

    def vert(self, p, c, w):
        self.pos.append(np.asarray(p, float))
        self.clr.append(c)
        self.w.append(w)
        return len(self.pos) - 1


class Person:
    def __init__(self, rig):
        self.rig = rig
        self.ji = {n: i for i, n in enumerate(rig.names)}
        self.base, self.jm = stretched_rest(rig)
        self.pieces = []

    def P(self, name):
        return self.base[self.ji[name]][:3, 3].copy()

    # ---------------------------------------------------------- primitives
    def tube(self, path, radii, color, weights, sides=12, up=(0, 0, 1), caps=(True, True), grad=(0.82, 1.08), twist=0.0):
        """Loft through `path` points. radii: (rx, rz) per ring. weights: dict per ring.
        color: rgb or per-ring list. grad: brightness at first/last ring (painted look)."""
        pc = Piece()
        path = [np.asarray(q, float) for q in path]
        n = len(path)
        rings = []
        for i in range(n):
            if i == 0:
                d = path[1] - path[0]
            elif i == n - 1:
                d = path[-1] - path[-2]
            else:
                d = path[i + 1] - path[i - 1]
            x, y, z = _frame(d, up)
            rx, rz = radii[i]
            cbase = color[i] if isinstance(color, list) else color
            k = grad[0] + (grad[1] - grad[0]) * (i / max(1, n - 1))
            ring = []
            for s in range(sides):
                a = 2 * math.pi * s / sides + twist
                ks = k * (0.94 + 0.08 * math.cos(a))   # light from the front: a painted feel
                p = path[i] + x * math.cos(a) * rx + z * math.sin(a) * rz
                ring.append(pc.vert(p, shade(cbase, ks), weights[i]))
            rings.append(ring)
        for i in range(n - 1):
            for s in range(sides):
                a, b = rings[i][s], rings[i][(s + 1) % sides]
                c, d = rings[i + 1][(s + 1) % sides], rings[i + 1][s]
                pc.idx += [(a, d, c), (a, c, b)]
        for end, ring in ((0, rings[0]), (1, rings[-1])):
            if not caps[end]:
                continue
            i = 0 if end == 0 else n - 1
            d = (path[1] - path[0]) if end == 0 else (path[-1] - path[-2])
            d = d / np.linalg.norm(d) * (-1 if end == 0 else 1)
            rr = max(radii[i])
            cbase = color[i] if isinstance(color, list) else color
            tip = pc.vert(path[i] + d * rr * 0.55, shade(cbase, grad[end]), weights[i])
            for s in range(sides):
                a, b = ring[s], ring[(s + 1) % sides]
                pc.idx.append((a, b, tip) if end == 0 else (b, a, tip))
        self.pieces.append(pc)
        return pc

    def ellipsoid(self, center, radii, color, weight, rows=7, cols=12, grad=(0.8, 1.1)):
        pc = Piece()
        center = np.asarray(center, float)
        verts = []
        for r in range(rows + 1):
            v = r / rows
            phi = math.pi * v
            row = []
            for c in range(cols):
                th = 2 * math.pi * c / cols
                p = np.array([math.sin(phi) * math.cos(th) * radii[0], math.cos(phi) * radii[1], math.sin(phi) * math.sin(th) * radii[2]])
                k = grad[1] + (grad[0] - grad[1]) * v
                row.append(pc.vert(center + p, shade(color, k), weight))
            verts.append(row)
        for r in range(rows):
            for c in range(cols):
                a, b = verts[r][c], verts[r][(c + 1) % cols]
                cc, d = verts[r + 1][(c + 1) % cols], verts[r + 1][c]
                pc.idx += [(a, b, cc), (a, cc, d)]
        self.pieces.append(pc)
        return pc

    def W(self, *pairs):
        """Weights dict from (bone, weight) pairs."""
        return {self.ji[b]: w for b, w in pairs if w > 0}

    # ------------------------------------------------------------- export
    def to_parts(self):
        """Converts world-space pieces to bind space; returns (parts, envelopes)."""
        nj = len(self.rig.names)
        A = [self.jm[j] @ np.asarray(self.rig.ibm[j], float) for j in range(nj)]
        env_index, envelopes = {}, []
        parts = []
        for pc in self.pieces:
            pos = np.array(pc.pos)
            idx = np.array(pc.idx, np.int64)
            nrm_world = compute_smooth_normals(pos, idx)
            bind = np.zeros_like(pos)
            nrm = np.zeros_like(pos)
            dm = np.zeros(len(pos), np.int64)
            for v in range(len(pos)):
                items = sorted(pc.w[v].items(), key=lambda kv: -kv[1])[:2]
                tot = sum(x for _, x in items)
                q = [(j, int(round(x / tot * 8))) for j, x in items]
                q = [(j, x) for j, x in q if x > 0]
                q[0] = (q[0][0], q[0][1] + 8 - sum(x for _, x in q))
                M = sum(A[j] * (x / 8.0) for j, x in q)
                Mi = np.linalg.inv(M)
                bind[v] = Mi[:3, :3] @ pos[v] + Mi[:3, 3]
                n = Mi[:3, :3] @ nrm_world[v]
                nrm[v] = n / max(np.linalg.norm(n), 1e-9)
                if len(q) == 1:
                    dm[v] = q[0][0]
                else:
                    key = tuple(sorted(q))
                    e = env_index.get(key)
                    if e is None:
                        e = len(envelopes)
                        env_index[key] = e
                        envelopes.append([(j, int(round(x * 255 / 8))) for j, x in key])
                    dm[v] = nj + e
            clr = np.clip(np.round(np.array([list(c) + [255] for c in pc.clr])), 0, 255).astype(np.uint8)
            parts.append((bind, idx, nrm, clr, dm))
        # one part for the whole body: fewer batches (draw calls) per character
        off, P, I, N, C, D = 0, [], [], [], [], []
        for bind, idx, nrm, clr, dm in parts:
            P.append(bind); I.append(idx + off); N.append(nrm); C.append(clr); D.append(dm)
            off += len(bind)
        return [Part(np.concatenate(P), np.concatenate(I), np.concatenate(N), None, np.concatenate(C), None, 0, np.concatenate(D))], envelopes


def _lerp(a, b, t):
    return a + (b - a) * t


def build_person(rig, spec):
    p = Person(rig)
    sk = SKIN[spec.get('skin', 'light')]
    hc = HAIR[spec.get('hair', 'brown')]
    bw = spec.get('build', 1.0)
    c1 = spec.get('c1', (70, 96, 160))
    c2 = spec.get('c2', shade(c1, 0.7))
    lc = spec.get('legs', (86, 70, 54))
    bc = spec.get('boots', (78, 52, 34))
    outfit = spec.get('outfit', 'tunic')
    robe, armor = outfit == 'robe', outfit == 'armor'
    metal = (128, 134, 148)
    top_c = metal if armor else c1
    if outfit == 'vest':
        top_c = spec.get('shirt', (226, 214, 190))

    hips, spine, chest, head = p.P('hips'), p.P('spine'), p.P('chest'), p.P('head')
    sh_y = p.P('upperarm.l')[1]
    W = p.W

    # ---------------------------------------------------------------- torso
    ys = [hips[1] - 0.16, hips[1] - 0.04, spine[1] + 0.02, _lerp(spine[1], chest[1], 0.55), chest[1] + 0.02, sh_y - 0.02, sh_y + 0.07, sh_y + 0.12]
    rr = [(0.2, 0.15), (0.205, 0.15), (0.18, 0.135), (0.205, 0.15), (0.235, 0.16), (0.25, 0.15), (0.2, 0.12), (0.09, 0.08)]
    wt = [W(('hips', 1)), W(('hips', 1)), W(('hips', 0.4), ('spine', 0.6)), W(('spine', 1)), W(('spine', 0.4), ('chest', 0.6)),
          W(('chest', 1)), W(('chest', 1)), W(('chest', 1))]
    cols = [lc, lc, top_c, top_c, top_c, top_c, top_c, top_c]
    p.tube([np.array([0, y, 0.0]) for y in ys], [(a * bw, b * bw) for a, b in rr], cols, wt, sides=14, grad=(0.78, 1.08))
    # neck
    p.tube([np.array([0, sh_y + 0.08, -0.01]), np.array([0, head[1] - 0.02, 0.0]), np.array([0, head[1] + 0.05, 0.0])],
           [(0.07, 0.068), (0.062, 0.062), (0.06, 0.06)], sk, [W(('chest', 1)), W(('chest', 0.5), ('head', 0.5)), W(('head', 1))],
           sides=10, caps=(False, False), grad=(0.8, 0.95))
    # belt with buckle and a pouch
    if not robe:
        p.tube([np.array([0, spine[1] - 0.05, 0.0]), np.array([0, spine[1] + 0.01, 0.0])], [(0.19 * bw, 0.142 * bw)] * 2, (64, 42, 28),
               [W(('hips', 0.5), ('spine', 0.5))] * 2, sides=14, caps=(False, False), grad=(0.9, 1.0))
        p.ellipsoid([0, spine[1] - 0.02, 0.142 * bw], (0.045, 0.035, 0.015), (214, 178, 80), W(('hips', 0.5), ('spine', 0.5)), rows=4, cols=8)
        p.ellipsoid([0.12 * bw, spine[1] - 0.06, 0.1 * bw], (0.055, 0.06, 0.04), (120, 84, 50), W(('hips', 1)), rows=4, cols=8)
    # tunic / armour skirt: a flared tube from the waist to mid-thigh
    if outfit in ('tunic', 'armor', 'apron', 'vest'):
        sk_c = c2 if armor else (c1 if outfit != 'vest' else lc)
        L = 0.3 if outfit != 'vest' else 0.16
        p.tube([np.array([0, spine[1] - 0.04, 0.0]), np.array([0, hips[1] - 0.05, 0.0]), np.array([0, hips[1] - L, 0.0])],
               [(0.2 * bw, 0.15 * bw), (0.225 * bw, 0.168 * bw), (0.26 * bw, 0.2 * bw)], sk_c,
               [W(('hips', 1))] * 3, sides=14, caps=(False, False), grad=(1.05, 0.8))
    if armor:
        p.tube([np.array([0, chest[1] + 0.1, 0.165 * bw]), np.array([0, hips[1] - 0.3, 0.205 * bw])], [(0.12 * bw, 0.012), (0.13 * bw, 0.012)], c1,
               [W(('chest', 1)), W(('hips', 1))], sides=8, grad=(1.05, 0.85))   # tabard
        for side, s in (('l', 1), ('r', -1)):
            p.ellipsoid([s * 0.26 * bw, sh_y + 0.05, 0.0], (0.13, 0.085, 0.13), shade(metal, 1.1), W(('upperarm.' + side, 1)), rows=5, cols=10)
    if outfit == 'apron':
        ac = spec.get('apron', (210, 198, 176))
        p.tube([np.array([0, chest[1] + 0.06, 0.17 * bw]), np.array([0, hips[1] - 0.36, 0.215 * bw])], [(0.15 * bw, 0.01), (0.19 * bw, 0.012)], ac,
               [W(('chest', 1)), W(('hips', 1))], sides=8, grad=(1.05, 0.85))
    if outfit == 'vest':
        p.tube([np.array([0, spine[1], 0.0]), np.array([0, chest[1] + 0.02, 0.0]), np.array([0, sh_y + 0.02, 0.0])],
               [(0.195 * bw, 0.145 * bw), (0.245 * bw, 0.168 * bw), (0.258 * bw, 0.158 * bw)], c1,
               [W(('spine', 1)), W(('chest', 0.6), ('spine', 0.4)), W(('chest', 1))], sides=14, caps=(False, False), grad=(0.85, 1.05))
    if robe:
        p.tube([np.array([0, spine[1] + 0.05, 0.0]), np.array([0, hips[1] - 0.1, 0.0]), np.array([0, 0.35, 0.0]), np.array([0, 0.06, 0.0])],
               [(0.2 * bw, 0.15 * bw), (0.25 * bw, 0.2 * bw), (0.3 * bw, 0.25 * bw), (0.34 * bw, 0.28 * bw)], [c1, c1, c1, c2],
               [W(('hips', 1))] * 4, sides=16, caps=(False, False), grad=(1.05, 0.72))
        p.tube([np.array([0, chest[1] + 0.12, 0.168 * bw]), np.array([0, hips[1] - 0.2, 0.235 * bw])], [(0.04, 0.008), (0.05, 0.01)], c2,
               [W(('chest', 1)), W(('hips', 1))], sides=6, grad=(1.1, 0.9))
    if spec.get('cape'):
        cc = spec['cape']
        p.tube([np.array([0, sh_y + 0.08, -0.13 * bw]), np.array([0, chest[1] - 0.2, -0.2 * bw]), np.array([0, hips[1] - 0.25, -0.26 * bw])],
               [(0.2 * bw, 0.012), (0.24 * bw, 0.014), (0.27 * bw, 0.014)], cc, [W(('chest', 1)), W(('chest', 0.5), ('spine', 0.5)), W(('hips', 1))],
               sides=10, grad=(1.0, 0.75))

    # ------------------------------------------------------------------ arms
    for side, s in (('l', 1), ('r', -1)):
        ua, la, wr, hd = p.P('upperarm.' + side), p.P('lowerarm.' + side), p.P('wrist.' + side), p.P('hand.' + side)
        arm_c = metal if armor else top_c
        bare = outfit == 'vest' and not spec.get('long_sleeves', True)
        upper_c = sk if bare else arm_c
        fore_c = sk if (bare or spec.get('rolled')) else arm_c
        path = [ua + [-s * 0.07, 0, 0], ua + [s * 0.02, 0, 0], _lerp(ua, la, 0.55), la, _lerp(la, wr, 0.35), wr + [-s * 0.03, 0, 0], wr + [s * 0.02, 0, 0]]
        radii = [(0.1, 0.1), (0.092, 0.09), (0.078, 0.076), (0.068, 0.066), (0.074, 0.07), (0.056, 0.052), (0.052, 0.05)]
        U, Lw = 'upperarm.' + side, 'lowerarm.' + side
        wts = [W(('chest', 0.5), (U, 0.5)), W((U, 1)), W((U, 1)), W((U, 0.5), (Lw, 0.5)), W((Lw, 1)), W((Lw, 1)), W((Lw, 1))]
        cols = [upper_c, upper_c, upper_c, upper_c, fore_c, fore_c, fore_c]
        p.tube(path, [(a * bw, b * bw) for a, b in radii], cols, wts, sides=10, up=(0, 1, 0), caps=(False, False), grad=(1.0, 0.9))
        if not bare:
            cuff = (96, 66, 42) if not armor else shade(metal, 0.9)
            p.tube([wr + [-s * 0.1, 0, 0], wr + [s * 0.015, 0, 0]], [(0.068 * bw, 0.064 * bw), (0.064 * bw, 0.06 * bw)], cuff,
                   [W((Lw, 1))] * 2, sides=10, up=(0, 1, 0), caps=(False, False))
        # big stylised hands: palm + thumb
        hand_c = spec.get('gloves') or sk
        p.ellipsoid(hd + [s * 0.045, -0.01, 0.0], (0.075, 0.045, 0.062), hand_c, W(('hand.' + side, 1)), rows=5, cols=10)
        p.ellipsoid(hd + [s * 0.02, 0.0, 0.06], (0.03, 0.026, 0.04), hand_c, W(('hand.' + side, 1)), rows=4, cols=8)

    # ------------------------------------------------------------------ legs
    for side, s in (('l', 1), ('r', -1)):
        ul, ll, ft, to = p.P('upperleg.' + side), p.P('lowerleg.' + side), p.P('foot.' + side), p.P('toes.' + side)
        U, Lg = 'upperleg.' + side, 'lowerleg.' + side
        boot_top = _lerp(ll, ft, 0.35)
        path = [ul + [0, 0.02, 0], _lerp(ul, ll, 0.35), _lerp(ul, ll, 0.75), ll, _lerp(ll, ft, 0.25), boot_top]
        radii = [(0.115, 0.115), (0.11, 0.11), (0.09, 0.09), (0.08, 0.08), (0.078, 0.08), (0.07, 0.072)]
        wts = [W(('hips', 0.5), (U, 0.5)), W((U, 1)), W((U, 1)), W((U, 0.5), (Lg, 0.5)), W((Lg, 1)), W((Lg, 1))]
        p.tube(path, [(a * bw, b * bw) for a, b in radii], lc, wts, sides=10, caps=(False, False), grad=(0.95, 0.85))
        # boot: shaft with a turned-down top, then a rounded foot
        p.tube([boot_top + [0, 0.04, 0], ft + [0, 0.02, 0]], [(0.088, 0.09), (0.08, 0.085)], bc, [W((Lg, 1))] * 2, sides=10, caps=(False, False), grad=(1.05, 0.85))
        p.tube([boot_top + [0, 0.06, 0], boot_top + [0, 0.02, 0]], [(0.098, 0.1), (0.095, 0.097)], shade(bc, 1.15), [W((Lg, 1))] * 2, sides=10, caps=(False, True))
        p.ellipsoid([ft[0], 0.07, ft[2] + 0.05], (0.085, 0.07, 0.15), bc, W(('foot.' + side, 1)), rows=5, cols=10, grad=(0.7, 1.0))
        p.ellipsoid([to[0], 0.05, to[2] + 0.05], (0.075, 0.05, 0.075), shade(bc, 0.9), W(('toes.' + side, 1)), rows=4, cols=8, grad=(0.7, 1.0))

    # ------------------------------------------------------------------ head
    first_head_piece = len(p.pieces)
    H = W(('head', 1))
    hy = head[1] + 0.15
    p.ellipsoid([0, hy, 0.0], (0.125, 0.15, 0.135), sk, H, rows=9, cols=14, grad=(0.86, 1.06))            # cranium
    p.ellipsoid([0, hy - 0.075, 0.035], (0.098, 0.07, 0.095), shade(sk, 0.97), H, rows=6, cols=12)       # jaw / cheeks
    p.ellipsoid([0, hy - 0.005, 0.135], (0.026, 0.034, 0.03), shade(sk, 1.02), H, rows=5, cols=8)         # nose
    for s in (1, -1):
        p.ellipsoid([s * 0.123, hy - 0.01, 0.0], (0.022, 0.04, 0.028), shade(sk, 0.92), H, rows=4, cols=8)       # ears
        # big, readable stylised eyes: they have to survive a third-person camera
        p.ellipsoid([s * 0.05, hy + 0.028, 0.119], (0.031, 0.037, 0.016), (250, 248, 244), H, rows=5, cols=8)   # eye whites
        p.ellipsoid([s * 0.048, hy + 0.024, 0.132], (0.019, 0.025, 0.008), (34, 40, 54), H, rows=4, cols=8)     # pupils
        p.ellipsoid([s * 0.042, hy + 0.036, 0.139], (0.006, 0.007, 0.003), (255, 255, 255), H, rows=3, cols=5)  # glint
        p.ellipsoid([s * 0.054, hy + 0.074, 0.12], (0.04, 0.013, 0.016), shade(hc, 0.7), H, rows=3, cols=8)    # brows
    p.ellipsoid([0, hy - 0.07, 0.12], (0.04, 0.01, 0.01), shade(sk, 0.55), H, rows=3, cols=8)               # mouth
    style = spec.get('hair_style', 'short')
    if style != 'bald':
        p.ellipsoid([0, hy + 0.05, -0.012], (0.138, 0.13, 0.15), hc, H, rows=7, cols=14, grad=(0.75, 1.1))
        p.ellipsoid([0, hy + 0.1, 0.05], (0.12, 0.06, 0.1), shade(hc, 1.05), H, rows=5, cols=12)    # fringe
    if style in ('long', 'ponytail', 'bun'):
        p.ellipsoid([0, hy - 0.04, -0.075], (0.13, 0.12, 0.08), hc, H, rows=6, cols=12)
    if style == 'long':
        p.tube([np.array([0, hy, -0.09]), np.array([0, hy - 0.22, -0.11]), np.array([0, hy - 0.34, -0.12])], [(0.13, 0.06), (0.14, 0.055), (0.11, 0.04)],
               hc, [H, W(('head', 0.5), ('chest', 0.5)), W(('chest', 1))], sides=10, grad=(1.0, 0.8))
    if style == 'ponytail':
        p.tube([np.array([0, hy + 0.04, -0.14]), np.array([0, hy - 0.12, -0.2]), np.array([0, hy - 0.3, -0.19])], [(0.04, 0.04), (0.045, 0.045), (0.02, 0.02)],
               hc, [H, H, W(('head', 0.5), ('chest', 0.5))], sides=8)
    if style == 'bun':
        p.ellipsoid([0, hy + 0.13, -0.08], (0.07, 0.06, 0.07), hc, H, rows=5, cols=10)
    if style == 'mohawk':
        p.ellipsoid([0, hy + 0.14, -0.01], (0.035, 0.06, 0.15), hc, H, rows=5, cols=10)
    beard = spec.get('beard')
    if beard:
        bcol = HAIR[spec.get('beard_c', spec.get('hair', 'brown'))]
        long_ = beard == 'long'
        p.ellipsoid([0, hy - 0.1 - (0.04 if long_ else 0), 0.07], (0.105, 0.08 + (0.05 if long_ else 0), 0.085), bcol, H, rows=6, cols=12, grad=(0.75, 1.05))
        for s in (1, -1):
            p.ellipsoid([s * 0.03, hy - 0.045, 0.125], (0.04, 0.014, 0.018), bcol, H, rows=3, cols=8)   # moustache
    # ------------------------------------------------------------- headwear
    hat = spec.get('hat')
    hcol = spec.get('hat_c', (90, 60, 120))
    if hat == 'wizard':
        p.tube([np.array([0, hy + 0.08, -0.01]), np.array([0, hy + 0.1, -0.01])], [(0.25, 0.25), (0.24, 0.24)], hcol, [H, H], sides=16, grad=(0.85, 1.0))
        p.tube([np.array([0, hy + 0.1, -0.01]), np.array([0.02, hy + 0.3, -0.05]), np.array([0.08, hy + 0.46, -0.12])], [(0.14, 0.14), (0.08, 0.08), (0.01, 0.01)],
               hcol, [H, H, H], sides=12, grad=(0.9, 1.1))
    elif hat == 'hood':
        p.ellipsoid([0, hy + 0.035, -0.035], (0.165, 0.18, 0.175), hcol, H, rows=8, cols=14, grad=(0.7, 1.05))
        p.tube([np.array([0, sh_y + 0.1, 0.0]), np.array([0, sh_y + 0.05, 0.0])], [(0.17, 0.15), (0.24, 0.18)], hcol, [W(('chest', 1))] * 2, sides=14, caps=(False, False))
    elif hat == 'helm':
        p.ellipsoid([0, hy + 0.06, -0.008], (0.145, 0.12, 0.155), hcol, H, rows=7, cols=14, grad=(0.75, 1.15))
        p.tube([np.array([0, hy + 0.0, -0.008]), np.array([0, hy + 0.03, -0.008])], [(0.15, 0.16)] * 2, shade(hcol, 0.85), [H, H], sides=14, caps=(False, False))
        p.ellipsoid([0, hy + 0.04, 0.15], (0.014, 0.06, 0.012), shade(hcol, 0.85), H, rows=4, cols=6)
    elif hat == 'straw':
        p.tube([np.array([0, hy + 0.09, -0.01]), np.array([0, hy + 0.11, -0.01])], [(0.28, 0.28), (0.26, 0.26)], hcol, [H, H], sides=16, grad=(0.85, 1.05))
        p.ellipsoid([0, hy + 0.13, -0.01], (0.14, 0.09, 0.14), hcol, H, rows=5, cols=12)
    elif hat == 'cap':
        p.ellipsoid([0, hy + 0.08, -0.012], (0.142, 0.1, 0.152), hcol, H, rows=6, cols=14)
        p.ellipsoid([0, hy + 0.06, 0.13], (0.1, 0.012, 0.06), shade(hcol, 0.85), H, rows=3, cols=10)
    elif hat == 'feather':
        p.ellipsoid([0, hy + 0.09, -0.01], (0.16, 0.09, 0.165), hcol, H, rows=6, cols=14)
        p.tube([np.array([0.08, hy + 0.1, -0.05]), np.array([0.14, hy + 0.3, -0.13])], [(0.02, 0.04), (0.005, 0.01)], (244, 236, 222), [H, H], sides=6)
    elif hat == 'bandana':
        p.ellipsoid([0, hy + 0.07, -0.012], (0.142, 0.1, 0.153), hcol, H, rows=6, cols=14)
    # stylised heads read a little larger than life
    hs = spec.get('head_scale', 1.18)
    pivot = np.array([0, head[1] + 0.02, 0.0])
    for pc in p.pieces[first_head_piece:]:
        pc.pos = [pivot + (q - pivot) * hs for q in pc.pos]
    return p.to_parts()


# ----------------------------------------------------------------- cast list
PEOPLE = {
    # player looks
    'hm/knight': dict(outfit='armor', c1=(40, 70, 160), c2=(30, 48, 110), hair='brown', hair_style='short', build=1.12, boots=(60, 60, 68), legs=(78, 80, 90), gloves=(96, 100, 110)),
    'hm/ranger': dict(outfit='vest', c1=(120, 80, 46), shirt=(90, 120, 70), hair='red', hair_style='mohawk', beard='full', skin='tan', build=1.18, legs=(76, 64, 46), long_sleeves=False),
    'hm/mage': dict(outfit='robe', c1=(70, 56, 150), c2=(222, 186, 86), hair='blond', hair_style='long', skin='fair', build=1.0),
    'hm/rogue': dict(outfit='tunic', c1=(50, 98, 70), c2=(32, 64, 46), hair='black', hair_style='ponytail', skin='brown', build=1.02, legs=(58, 52, 50)),
    'hm/wanderer': dict(outfit='tunic', c1=(126, 100, 70), c2=(88, 66, 46), hat='hood', hat_c=(100, 74, 50), hair='brown', skin='light', cape=(100, 74, 50)),
    # townsfolk
    'hm/elder': dict(outfit='robe', c1=(116, 64, 128), c2=(234, 214, 156), hair='white', hair_style='long', skin='fair', build=0.95),
    'hm/smith': dict(outfit='apron', c1=(160, 98, 64), apron=(94, 66, 44), hair='black', hair_style='bald', beard='long', skin='tan', build=1.3, rolled=True),
    'hm/cook': dict(outfit='apron', c1=(176, 74, 62), apron=(240, 234, 218), hair='auburn', hair_style='bun', skin='light', build=1.08),
    'hm/carpenter': dict(outfit='tunic', c1=(84, 132, 74), c2=(62, 96, 54), hair='blond', hair_style='ponytail', skin='light', hat='bandana', hat_c=(176, 64, 42), rolled=True),
    'hm/alchemist': dict(outfit='robe', c1=(42, 126, 126), c2=(206, 226, 214), hat='hood', hat_c=(32, 98, 100), hair='black', skin='deep'),
    'hm/fisher': dict(outfit='tunic', c1=(226, 182, 54), c2=(176, 134, 42), hair='grey', beard='full', hat='cap', hat_c=(226, 182, 54), skin='tan', build=1.1),
    'hm/captain': dict(outfit='armor', c1=(166, 42, 42), c2=(114, 30, 30), hair='auburn', hair_style='long', skin='light', boots=(60, 60, 66), build=1.08),
    'hm/merchant': dict(outfit='vest', c1=(226, 146, 62), shirt=(236, 226, 200), hair='brown', beard='full', hat='feather', hat_c=(124, 72, 146), build=1.18),
    'hm/farmer': dict(outfit='tunic', c1=(84, 114, 166), c2=(66, 88, 124), hat='straw', hat_c=(226, 200, 124), hair='brown', beard='full', skin='tan', rolled=True),
    'hm/miner': dict(outfit='vest', c1=(98, 92, 86), shirt=(150, 130, 100), hat='helm', hat_c=(156, 124, 62), hair='black', beard='full', skin='light', build=1.22),
    'hm/herbalist': dict(outfit='robe', c1=(100, 146, 74), c2=(206, 176, 114), hair='auburn', hair_style='long', skin='fair', build=0.95),
    'hm/hunter': dict(outfit='tunic', c1=(58, 84, 52), c2=(42, 58, 38), hat='hood', hat_c=(48, 70, 44), hair='black', skin='brown', cape=(48, 70, 44)),
    'hm/bard': dict(outfit='tunic', c1=(196, 62, 72), c2=(244, 214, 124), hat='feather', hat_c=(196, 62, 72), hair='blond', hair_style='ponytail', skin='light', build=0.98),
    'hm/priest': dict(outfit='robe', c1=(234, 230, 218), c2=(206, 166, 62), hair='grey', hair_style='bald', beard='full', skin='light'),
    'hm/child': dict(outfit='tunic', c1=(236, 154, 62), c2=(186, 114, 42), hair='blond', hair_style='bun', skin='fair', build=0.9),
    'hm/watchman': dict(outfit='armor', c1=(84, 84, 96), c2=(62, 62, 70), hat='helm', hat_c=(146, 150, 160), hair='brown', beard='full', build=1.16),
}

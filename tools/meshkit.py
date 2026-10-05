"""Tiny procedural mesh toolkit (numpy) producing meshbuild.Part objects.

Shapes are built as (pos, idx) with optional per-vertex colours; helpers
apply transforms, noise displacement and colour gradients so procedural
props match the chunky, gradient-shaded KayKit look.
"""
import math
import numpy as np
from meshbuild import Part, compute_smooth_normals, compute_flat_normals


class Mesh:
    def __init__(self, pos=None, idx=None, clr=None):
        self.pos = np.zeros((0, 3)) if pos is None else np.asarray(pos, np.float64)
        self.idx = np.zeros((0, 3), np.int64) if idx is None else np.asarray(idx, np.int64)
        self.clr = None if clr is None else np.asarray(clr, np.float64)
        self.flat = False

    def copy(self):
        m = Mesh(self.pos.copy(), self.idx.copy(), None if self.clr is None else self.clr.copy())
        m.flat = self.flat
        return m

    # ---------------------------------------------------------- transforms
    def translate(self, x, y, z):
        self.pos = self.pos + np.array([x, y, z])
        return self

    def scale(self, sx, sy=None, sz=None):
        if sy is None:
            sy = sz = sx
        self.pos = self.pos * np.array([sx, sy, sz])
        return self

    def rotate_y(self, a):
        c, s = math.cos(a), math.sin(a)
        m = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
        self.pos = self.pos @ m.T
        return self

    def rotate_x(self, a):
        c, s = math.cos(a), math.sin(a)
        m = np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
        self.pos = self.pos @ m.T
        return self

    def rotate_z(self, a):
        c, s = math.cos(a), math.sin(a)
        m = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
        self.pos = self.pos @ m.T
        return self

    def apply(self, m3, t=(0, 0, 0)):
        self.pos = self.pos @ np.asarray(m3).T + np.asarray(t)
        return self

    def displace(self, rng, amount, freq=1.0):
        """Smooth-ish noise displacement along radial direction from centroid."""
        c = self.pos.mean(axis=0)
        d = self.pos - c
        n = d / np.maximum(np.linalg.norm(d, axis=1, keepdims=True), 1e-9)
        # value noise from a few random sinusoids
        k = rng.normal(size=(4, 3)) * freq
        ph = rng.uniform(0, 6.28, size=4)
        v = np.zeros(len(self.pos))
        for i in range(4):
            v += np.sin(self.pos @ k[i] + ph[i])
        self.pos = self.pos + n * (v / 4.0 * amount)[:, None]
        return self

    # ------------------------------------------------------------- colour
    def color(self, rgb):
        c = np.array(list(rgb) + ([255] if len(rgb) == 3 else []), np.float64)
        self.clr = np.tile(c, (len(self.pos), 1))
        return self

    def gradient_y(self, bottom, top, y0=None, y1=None, power=1.0):
        y = self.pos[:, 1]
        y0 = y.min() if y0 is None else y0
        y1 = y.max() if y1 is None else y1
        t = np.clip((y - y0) / max(1e-6, y1 - y0), 0, 1) ** power
        b = np.array(list(bottom) + [255] * (4 - len(bottom)), np.float64)
        tp = np.array(list(top) + [255] * (4 - len(top)), np.float64)
        self.clr = b[None, :] * (1 - t[:, None]) + tp[None, :] * t[:, None]
        return self

    def jitter_color(self, rng, amount):
        if self.clr is None:
            self.color((255, 255, 255))
        j = rng.uniform(-amount, amount, size=(len(self.pos), 1))
        self.clr[:, :3] = np.clip(self.clr[:, :3] * (1 + j), 0, 255)
        return self

    def faceted(self):
        """Split vertices per face for a flat-shaded low-poly look."""
        p = self.pos[self.idx.reshape(-1)]
        c = None if self.clr is None else self.clr[self.idx.reshape(-1)]
        m = Mesh(p, np.arange(len(p)).reshape(-1, 3), c)
        m.flat = True
        return m

    def to_part(self, flags=0):
        if self.flat:
            fn = compute_flat_normals(self.pos, self.idx)
            nrm = np.repeat(fn, 3, axis=0)
            # vertices are already unique per face
            nn = np.zeros_like(self.pos)
            nn[self.idx.reshape(-1)] = nrm
            nrm = nn
        else:
            nrm = compute_smooth_normals(self.pos, self.idx)
        clr = None if self.clr is None else np.clip(np.round(self.clr), 0, 255).astype(np.uint8)
        return Part(self.pos, self.idx, nrm, None, clr, None, flags)


def merge(meshes):
    pos, idx, clr = [], [], []
    base = 0
    any_clr = any(m.clr is not None for m in meshes)
    for m in meshes:
        pos.append(m.pos)
        idx.append(m.idx + base)
        if any_clr:
            clr.append(m.clr if m.clr is not None else np.full((len(m.pos), 4), 255.0))
        base += len(m.pos)
    out = Mesh(np.concatenate(pos), np.concatenate(idx), np.concatenate(clr) if any_clr else None)
    return out


# ---------------------------------------------------------------- shapes
def icosphere(subdiv=1):
    t = (1.0 + math.sqrt(5.0)) / 2.0
    verts = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
             (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
             (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
             (8, 6, 7), (9, 8, 1)]
    verts = [np.array(v, np.float64) / np.linalg.norm(v) for v in verts]
    for _ in range(subdiv):
        cache = {}
        nf = []

        def mid(a, b):
            key = (min(a, b), max(a, b))
            if key not in cache:
                m = verts[a] + verts[b]
                verts.append(m / np.linalg.norm(m))
                cache[key] = len(verts) - 1
            return cache[key]
        for a, b, c in faces:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = nf
    return Mesh(np.array(verts), np.array(faces))


def cylinder(r0, r1, h, segs=8, cap_top=True, cap_bottom=False, y0=0.0):
    """Tapered cylinder along +Y from y0 to y0+h. Faces CCW from outside."""
    pos = []
    for ring, (r, y) in enumerate([(r0, y0), (r1, y0 + h)]):
        for i in range(segs):
            a = 2 * math.pi * i / segs
            pos.append((math.cos(a) * r, y, math.sin(a) * r))
    idx = []
    for i in range(segs):
        j = (i + 1) % segs
        a, b, c, d = i, j, segs + j, segs + i
        idx += [(a, c, b), (a, d, c)]
    if cap_top:
        ci = len(pos)
        pos.append((0, y0 + h, 0))
        for i in range(segs):
            j = (i + 1) % segs
            idx.append((segs + i, ci, segs + j))
    if cap_bottom:
        ci = len(pos)
        pos.append((0, y0, 0))
        for i in range(segs):
            j = (i + 1) % segs
            idx.append((i, j, ci))
    return Mesh(np.array(pos, np.float64), np.array(idx))


def box(sx, sy, sz, y0=0.0):
    x, z = sx / 2, sz / 2
    p = np.array([[-x, y0, -z], [x, y0, -z], [x, y0, z], [-x, y0, z],
                  [-x, y0 + sy, -z], [x, y0 + sy, -z], [x, y0 + sy, z], [-x, y0 + sy, z]], np.float64)
    f = [(0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6), (0, 4, 5), (0, 5, 1), (1, 5, 6), (1, 6, 2), (2, 6, 7), (2, 7, 3), (3, 7, 4), (3, 4, 0)]
    m = Mesh(p, np.array(f))
    return m.faceted()


def cone(r, h, segs=8, y0=0.0):
    pos = [(0, y0 + h, 0)]
    for i in range(segs):
        a = 2 * math.pi * i / segs
        pos.append((math.cos(a) * r, y0, math.sin(a) * r))
    idx = []
    for i in range(segs):
        j = (i + 1) % segs
        idx.append((0, 1 + j, 1 + i))
    ci = len(pos)
    pos.append((0, y0, 0))
    for i in range(segs):
        j = (i + 1) % segs
        idx.append((1 + i, 1 + j, ci))
    return Mesh(np.array(pos, np.float64), np.array(idx))


def quad_cross(w, h, n=2, uv=True):
    """Crossed vertical quads (billboard cluster) centred at origin, base at y=0.
    Returns (pos, idx, uv) for a textured double-sided cutout."""
    pos, idx, uvs = [], [], []
    for k in range(n):
        a = math.pi * k / n
        dx, dz = math.cos(a) * w / 2, math.sin(a) * w / 2
        b = len(pos)
        pos += [(-dx, 0, -dz), (dx, 0, dz), (dx, h, dz), (-dx, h, -dz)]
        uvs += [(0, 1), (1, 1), (1, 0), (0, 0)]
        idx += [(b, b + 1, b + 2), (b, b + 2, b + 3)]
    return np.array(pos, np.float64), np.array(idx), np.array(uvs, np.float64)

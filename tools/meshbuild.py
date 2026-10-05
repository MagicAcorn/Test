"""Builds GameCube-ready model assets (MDL1) from triangle soups.

A model is a set of indexed vertex arrays (positions s16, normals s8,
colours RGBA8, texcoords s16) plus one precompiled GX display list per batch.
Skinned models use J3D-style "envelopes": every distinct weight blend becomes
a draw matrix computed by the CPU each frame; each vertex then references a
single draw matrix through the GX position/normal matrix index, so skinning
itself runs entirely in the GX transform unit.
"""
import struct
import numpy as np

GX_TRIANGLES = 0x90
GX_TRIANGLESTRIP = 0x98
MAX_MTX_PER_BATCH = 10

# model flags
MF_SKINNED = 1
MF_COLORS = 2
MF_UVS = 4
MF_NORMALS = 8

# batch flags
BF_ALPHATEST = 1
BF_DOUBLESIDED = 2
BF_UNLIT = 4
BF_TRANSLUCENT = 8
BF_FOLIAGE = 16   # gets wind sway / special shading


class Part:
    """One material group of a mesh: triangles + per-vertex attributes."""

    def __init__(self, pos, idx, nrm=None, uv=None, clr=None, tex=None, flags=0, dmtx=None):
        self.pos = np.asarray(pos, np.float64).reshape(-1, 3)
        self.idx = np.asarray(idx, np.int64).reshape(-1, 3)
        self.nrm = None if nrm is None else np.asarray(nrm, np.float64).reshape(-1, 3)
        self.uv = None if uv is None else np.asarray(uv, np.float64).reshape(-1, 2)
        self.clr = None if clr is None else np.asarray(clr, np.uint8).reshape(-1, 4)
        self.tex = tex
        self.flags = flags
        self.dmtx = None if dmtx is None else np.asarray(dmtx, np.int64).reshape(-1)

    def transformed(self, m):
        p = Part(self.pos, self.idx, self.nrm, self.uv, self.clr, self.tex, self.flags, self.dmtx)
        p.pos = (m[:3, :3] @ self.pos.T).T + m[:3, 3]
        if self.nrm is not None:
            nm = np.linalg.inv(m[:3, :3]).T
            n = (nm @ self.nrm.T).T
            p.nrm = n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
        if np.linalg.det(m[:3, :3]) < 0:
            p.idx = self.idx[:, [0, 2, 1]]
        return p


def compute_flat_normals(pos, idx):
    a = pos[idx[:, 0]]
    b = pos[idx[:, 1]]
    c = pos[idx[:, 2]]
    n = np.cross(b - a, c - a)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)


def compute_smooth_normals(pos, idx):
    fn = np.cross(pos[idx[:, 1]] - pos[idx[:, 0]], pos[idx[:, 2]] - pos[idx[:, 0]])
    n = np.zeros_like(pos)
    for k in range(3):
        np.add.at(n, idx[:, k], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)


def _frac_for(maxabs, limit=32767, maxfrac=15):
    frac = maxfrac
    while frac > 0 and maxabs * (1 << frac) > limit:
        frac -= 1
    return frac


def _align(b, a=32):
    pad = (-len(b)) % a
    return b + b'\0' * pad


class _UnusedDedup:
    def __init__(self):
        self.map = {}
        self.items = []

    def add(self, key):
        i = self.map.get(key)
        if i is None:
            i = len(self.items)
            self.map[key] = i
            self.items.append(key)
        return i


def decimate_parts(parts, cell, origin):
    """Vertex-clustering LOD: snaps vertices to a grid of `cell` size and drops
    collapsed triangles. Positions are clustered (per draw matrix for skinned
    meshes, so limbs never fuse), while every corner keeps its own normal, UV
    and colour, so colour regions stay sharp and no cracks open between them."""
    out = []
    for p in parts:
        key = np.floor((p.pos - origin) / cell).astype(np.int64)
        if p.dmtx is not None:
            key = np.concatenate([key, p.dmtx[:, None]], axis=1)
        ukey, cl = np.unique(key, axis=0, return_inverse=True)
        cl = cl.reshape(-1)
        nc = len(ukey)
        cnt = np.bincount(cl, minlength=nc).astype(np.float64)
        mean = np.stack([np.bincount(cl, weights=p.pos[:, k], minlength=nc) for k in range(3)], axis=1) / cnt[:, None]
        pos = mean[cl]
        idx = p.idx
        c = cl[idx]
        keep = (c[:, 0] != c[:, 1]) & (c[:, 1] != c[:, 2]) & (c[:, 0] != c[:, 2])
        idx = idx[keep]
        c = c[keep]
        if len(idx) == 0:
            continue
        # drop duplicate triangles (same clusters, same winding)
        r = np.argmin(c, axis=1)
        rot = np.stack([np.take_along_axis(c, ((r + k) % 3)[:, None], axis=1)[:, 0] for k in range(3)], axis=1)
        _, first = np.unique(rot, axis=0, return_index=True)
        idx = idx[np.sort(first)]
        q = Part(pos, idx, p.nrm, p.uv, p.clr, p.tex, p.flags, p.dmtx)
        out.append(q)
    return out


def make_lod(parts, target_ratio):
    """Finest vertex-clustering LOD whose triangle count is <= target_ratio of the original."""
    allpos = np.concatenate([p.pos for p in parts])
    bmin, bmax = allpos.min(axis=0), allpos.max(axis=0)
    size = float(np.max(bmax - bmin))
    total = sum(len(p.idx) for p in parts)
    best = None
    for k in (48, 40, 34, 28, 24, 20, 17, 14, 12, 10, 8, 6):
        cell = size / k
        lod = decimate_parts(parts, cell, bmin - cell * 0.37)
        n = sum(len(p.idx) for p in lod)
        if n == 0:
            break
        best = (lod, n, k)
        if n <= total * target_ratio:
            break
    return best


def stripify(tris):
    """Greedy triangle stripper. tris: (N,3) int array of vertex ids in GX
    winding. GX strips alternate orientation like OpenGL: triangle t of a strip
    is (s[t], s[t+1], s[t+2]) when t is even and (s[t+1], s[t], s[t+2]) when odd.
    Returns (strips, singles): lists of vertex-id lists and leftover triangles."""
    tris = [tuple(t) for t in np.asarray(tris).tolist()]
    n = len(tris)
    edge = {}
    for i, (a, b, c) in enumerate(tris):
        if a == b or b == c or a == c:
            continue
        edge.setdefault((a, b), []).append((i, c))
        edge.setdefault((b, c), []).append((i, a))
        edge.setdefault((c, a), []).append((i, b))
    used = bytearray(n)
    for i, (a, b, c) in enumerate(tris):
        if a == b or b == c or a == c:
            used[i] = 1   # drop degenerate triangles

    def free_neighbours(i):
        a, b, c = tris[i]
        k = 0
        for x, y in ((b, a), (c, b), (a, c)):
            for j, _ in edge.get((x, y), ()):
                if not used[j] and j != i:
                    k += 1
        return k

    def grow(start, rot, mark):
        a, b, c = tris[start]
        s = [a, b, c] if rot == 0 else ([b, c, a] if rot == 1 else [c, a, b])
        taken = [start]
        local = {start}
        while True:
            t = len(s) - 2       # index of the next triangle
            x, y = (s[-2], s[-1]) if t % 2 == 0 else (s[-1], s[-2])
            nxt = None
            best = 99
            for j, r in edge.get((x, y), ()):
                if used[j] or j in local:
                    continue
                # prefer the neighbour with the fewest free neighbours (keeps future options)
                fn = free_neighbours(j)
                if fn < best:
                    best = fn
                    nxt = (j, r)
            if nxt is None:
                break
            local.add(nxt[0])
            taken.append(nxt[0])
            s.append(nxt[1])
        if mark:
            for j in taken:
                used[j] = 1
        return s, taken

    order = sorted(range(n), key=free_neighbours)
    strips, singles = [], []
    for i in order:
        if used[i]:
            continue
        best = None
        for rot in range(3):
            s, taken = grow(i, rot, False)
            if best is None or len(taken) > best[1]:
                best = (rot, len(taken))
        s, taken = grow(i, best[0], True)
        if len(taken) == 1:
            singles.append(s)
        else:
            strips.append(s)
    return strips, singles


def build_batches_skinned(tri_mtx_sets):
    """Greedy grouping of triangles so each batch references <= 10 draw matrices.
    tri_mtx_sets: list of frozensets. Returns list of (matrix list, triangle index list)."""
    groups = {}
    for ti, s in enumerate(tri_mtx_sets):
        groups.setdefault(s, []).append(ti)
    remaining = dict(groups)
    batches = []
    while remaining:
        # start with the set having the most triangles
        seed = max(remaining, key=lambda s: (len(s), len(remaining[s])))
        cur = set(seed)
        tris = list(remaining.pop(seed))
        while True:
            best = None
            best_key = None
            for s, tl in remaining.items():
                u = cur | s
                if len(u) > MAX_MTX_PER_BATCH:
                    continue
                key = (len(u) - len(cur), -len(tl))
                if best_key is None or key < best_key:
                    best_key = key
                    best = s
            if best is None:
                break
            cur |= best
            tris += remaining.pop(best)
        batches.append((sorted(cur), tris))
    return batches


def build_model(parts, tex_names=None, skin=None, name='?', lod_hash=0, lod_dist=0.0):
    """parts: list[Part]. skin: dict(num_joints, envelopes=[[(joint,weight),...]], skel_hash)
    Returns (bytes, stats)."""
    skinned = skin is not None
    has_uv = any(p.uv is not None for p in parts)
    has_clr = any(p.clr is not None for p in parts)
    allpos = np.concatenate([p.pos for p in parts])
    bmin = allpos.min(axis=0)
    bmax = allpos.max(axis=0)
    center = (bmin + bmax) * 0.5
    radius = float(np.sqrt(((allpos - center) ** 2).sum(axis=1)).max())
    maxabs = float(np.abs(allpos).max())
    pos_frac = _frac_for(maxabs)
    uv_frac = 0
    if has_uv:
        maxuv = max(float(np.abs(p.uv).max()) for p in parts if p.uv is not None)
        uv_frac = _frac_for(maxuv, maxfrac=14)

    textures = []
    for p in parts:
        if p.tex is not None and p.tex not in textures:
            textures.append(p.tex)
    assert len(textures) <= 4, (name, textures)

    # ---- quantise all attributes of all parts, then dedup each attribute stream
    qpos, qnrm, quv, qclr = [], [], [], []
    for p in parts:
        nrm = p.nrm if p.nrm is not None else compute_smooth_normals(p.pos, p.idx)
        qpos.append(np.clip(np.round(p.pos * (1 << pos_frac)), -32768, 32767).astype(np.int16))
        qnrm.append(np.clip(np.round(nrm * 64), -64, 64).astype(np.int8))
        if has_uv:
            uv = p.uv if p.uv is not None else np.zeros((len(p.pos), 2))
            quv.append(np.clip(np.round(uv * (1 << uv_frac)), -32768, 32767).astype(np.int16))
        if has_clr:
            clr = p.clr if p.clr is not None else np.full((len(p.pos), 4), 255, np.uint8)
            qclr.append(clr.astype(np.uint8))
    def uniq(arrs):
        a = np.concatenate(arrs)
        u, inv = np.unique(a, axis=0, return_inverse=True)
        return u, inv.reshape(-1)
    upos, ipos = uniq(qpos)
    unrm, inrm = uniq(qnrm)
    if has_uv:
        uuv, iuv = uniq(quv)
    if has_clr:
        uclr, iclr = uniq(qclr)
    assert len(upos) < 65536 and len(unrm) < 65536

    batch_blobs = []
    stats = dict(tris=0, verts=len(upos), batches=0)
    base = 0
    for p in parts:
        n = len(p.pos)
        vpos = ipos[base:base + n]
        vnrm = inrm[base:base + n]
        vuv = iuv[base:base + n] if has_uv else None
        vclr = iclr[base:base + n] if has_clr else None
        base += n
        texslot = textures.index(p.tex) if p.tex is not None else 0xFF
        if skinned:
            sets = [frozenset(int(p.dmtx[v]) for v in tri) for tri in p.idx.tolist()]
            groups = build_batches_skinned(sets)
        else:
            groups = [([], list(range(len(p.idx))))]
        for mtx_list, tris in groups:
            # GX front faces are clockwise -> flip glTF CCW winding
            tri = p.idx[np.asarray(tris, np.int64)][:, [0, 2, 1]].reshape(-1)
            fields = []
            if skinned:
                slot = np.zeros(int(p.dmtx.max()) + 1, np.int64)
                for i, m in enumerate(mtx_list):
                    slot[m] = i
                fields.append(('m', 'u1', (slot[p.dmtx[tri]] * 3)))
            fields.append(('p', '>u2', vpos[tri]))
            fields.append(('n', '>u2', vnrm[tri]))
            if has_clr:
                fields.append(('c', '>u2', vclr[tri]))
            if has_uv:
                fields.append(('t', '>u2', vuv[tri]))
            rec = np.zeros(len(tri), dtype=[(f, t) for f, t, _ in fields])
            for f, t, v in fields:
                rec[f] = v
            # identify identical vertex records, then build triangle strips
            urec, vid = np.unique(rec, return_inverse=True)
            vid = vid.reshape(-1, 3)
            strips, singles = stripify(vid)
            vsize = urec.dtype.itemsize
            out = bytearray()
            for st in strips:
                out += struct.pack('>BH', GX_TRIANGLESTRIP, len(st))
                out += urec[np.asarray(st, np.int64)].tobytes()
            if singles:
                flat = np.asarray(singles, np.int64).reshape(-1)
                per = 65535 - 65535 % 3
                for start in range(0, len(flat), per):
                    cnt = min(per, len(flat) - start)
                    out += struct.pack('>BH', GX_TRIANGLES, cnt)
                    out += urec[flat[start:start + cnt]].tobytes()
            batch_blobs.append((_align(bytes(out)), texslot, p.flags, mtx_list))
            stats['tris'] += len(tri) // 3
            stats['dlverts'] = stats.get('dlverts', 0) + sum(len(st) for st in strips) + 3 * len(singles)
            stats['batches'] += 1

    pos_arr = _align(upos.astype('>i2').tobytes())
    nrm_arr = _align(unrm.astype('i1').tobytes())
    uv_arr = _align(uuv.astype('>i2').tobytes()) if has_uv else b''
    clr_arr = _align(uclr.astype('u1').tobytes()) if has_clr else b''
    npos, nnrm = len(upos), len(unrm)
    nuv = len(uuv) if has_uv else 0
    nclr = len(uclr) if has_clr else 0

    flags = MF_NORMALS | (MF_SKINNED if skinned else 0) | (MF_COLORS if has_clr else 0) | (MF_UVS if has_uv else 0)
    num_joints = skin['num_joints'] if skinned else 0
    envs = skin['envelopes'] if skinned else []

    HDR = 160
    nb = len(batch_blobs)
    batch_tab_size = 32 * nb
    off = HDR
    pos_off = off; off += len(pos_arr)
    nrm_off = off; off += len(nrm_arr)
    uv_off = off if has_uv else 0; off += len(uv_arr)
    clr_off = off if has_clr else 0; off += len(clr_arr)
    batch_off = off; off += batch_tab_size
    env_bytes = b''
    for e in envs:
        js = [j for j, w in e] + [0] * (4 - len(e))
        ws = [w for j, w in e] + [0] * (4 - len(e))
        env_bytes += struct.pack('>4B4B', *js, *ws)
    env_bytes = _align(env_bytes)
    env_off = off if envs else 0; off += len(env_bytes)
    dl_offs = []
    for blob, *_ in batch_blobs:
        dl_offs.append(off)
        off += len(blob)

    tex_hashes = [fnv1a(t) for t in textures] + [0] * (4 - len(textures))
    hdr = struct.pack('>4sHBBHHHHHHHH', b'MDL1', flags, pos_frac, uv_frac,
                      npos, nnrm, nuv, nclr,
                      nb, num_joints + len(envs), num_joints, len(envs))
    hdr += struct.pack('>3f3f3ff', *bmin, *bmax, *center, radius)
    hdr += struct.pack('>IIIIII', pos_off, nrm_off, uv_off, clr_off, batch_off, env_off)
    hdr += struct.pack('>I', skin['skel_hash'] if skinned else 0)
    hdr += struct.pack('>4I', *tex_hashes)
    hdr += struct.pack('>B', len(textures))
    hdr = hdr + b'\0' * (112 - len(hdr))
    hdr += struct.pack('>If', lod_hash, lod_dist)
    hdr = hdr + b'\0' * (HDR - len(hdr))
    assert len(hdr) == HDR, len(hdr)

    btab = b''
    for (blob, texslot, bflags, mtx_list), dlo in zip(batch_blobs, dl_offs):
        m = list(mtx_list) + [0] * (10 - len(mtx_list))
        btab += struct.pack('>IIBBBB10H', dlo, len(blob), texslot, bflags, len(mtx_list), 0, *m)
    assert len(btab) == batch_tab_size

    data = hdr + pos_arr + nrm_arr + uv_arr + clr_arr + btab + env_bytes
    for blob, *_ in batch_blobs:
        data += blob
    assert len(data) == off
    return data, stats


def fnv1a(s):
    h = 0x811C9DC5
    for c in s.lower().encode('utf-8'):
        h ^= c
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h

"""Minimal glTF 2.0 helpers on top of pygltflib + numpy."""
import io
import os
import numpy as np
import json
import struct

_CTYPES = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
_NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT2': 4, 'MAT3': 9, 'MAT4': 16}


class Obj:
    """Attribute view over parsed JSON: missing keys read as None."""
    __slots__ = ('_d',)

    def __init__(self, d):
        self._d = d

    def __getattr__(self, k):
        v = self._d.get(k)
        return _wrap(v)

    def __repr__(self):
        return 'Obj(%r)' % (self._d,)


def _wrap(v):
    if isinstance(v, dict):
        return Obj(v)
    if isinstance(v, list):
        return [_wrap(x) for x in v] if v and isinstance(v[0], dict) else v
    return v


class _Doc:
    """Top level glTF document with list attributes pre-wrapped."""

    def __init__(self, d):
        self._d = d
        for k in ('nodes', 'meshes', 'skins', 'animations', 'accessors', 'bufferViews', 'buffers',
                  'images', 'materials', 'textures', 'scenes', 'samplers'):
            setattr(self, k, [Obj(x) for x in d.get(k, [])])


def _load(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] == b'glTF':
        length = struct.unpack_from('<I', data, 8)[0]
        off = 12
        js = None
        binchunk = None
        while off < length:
            clen, ctype = struct.unpack_from('<II', data, off)
            chunk = data[off + 8:off + 8 + clen]
            if ctype == 0x4E4F534A:
                js = json.loads(chunk.decode('utf-8'))
            elif ctype == 0x004E4942:
                binchunk = chunk
            off += 8 + clen
        return _Doc(js), binchunk
    return _Doc(json.loads(data.decode('utf-8'))), None


class Gltf:
    def __init__(self, path):
        self.path = path
        self.dir = os.path.dirname(path)
        self.g, self._glb_bin = _load(path)
        self._buffers = {}

    # ------------------------------------------------------------------ data
    def buffer(self, i):
        if i not in self._buffers:
            b = self.g.buffers[i]
            if b.uri is None:
                self._buffers[i] = self._glb_bin
            elif b.uri.startswith('data:'):
                import base64
                self._buffers[i] = base64.b64decode(b.uri.split(',', 1)[1])
            else:
                with open(os.path.join(self.dir, b.uri), 'rb') as f:
                    self._buffers[i] = f.read()
        return self._buffers[i]

    def accessor(self, i):
        a = self.g.accessors[i]
        ctype = _CTYPES[a.componentType]
        n = _NCOMP[a.type]
        if a.bufferView is None:
            arr = np.zeros((a.count, n), ctype)
        else:
            bv = self.g.bufferViews[a.bufferView]
            data = self.buffer(bv.buffer)
            off = (bv.byteOffset or 0) + (a.byteOffset or 0)
            itemsize = np.dtype(ctype).itemsize * n
            stride = bv.byteStride or itemsize
            if stride != itemsize:
                raw = np.frombuffer(data, np.uint8, count=stride * (a.count - 1) + itemsize, offset=off)
                rows = np.lib.stride_tricks.as_strided(raw, shape=(a.count, itemsize), strides=(stride, 1)).copy()
                arr = rows.view(ctype).reshape(a.count, n)
            else:
                arr = np.frombuffer(data, ctype, count=a.count * n, offset=off).reshape(a.count, n).copy()
        if a.normalized and np.issubdtype(arr.dtype, np.integer):
            arr = arr.astype(np.float32) / np.iinfo(ctype).max
        return arr

    def image_bytes(self, img_index):
        im = self.g.images[img_index]
        if im.uri:
            if im.uri.startswith('data:'):
                import base64
                return base64.b64decode(im.uri.split(',', 1)[1])
            with open(os.path.join(self.dir, im.uri), 'rb') as f:
                return f.read()
        bv = self.g.bufferViews[im.bufferView]
        data = self.buffer(bv.buffer)
        off = bv.byteOffset or 0
        return data[off:off + bv.byteLength]

    def image_name(self, img_index):
        im = self.g.images[img_index]
        if im.uri and not im.uri.startswith('data:'):
            return os.path.splitext(os.path.basename(im.uri))[0]
        return im.name or ('img%d' % img_index)

    def material_image(self, mat_index):
        """Return image index used as base colour texture for material, or None."""
        if mat_index is None:
            return None
        m = self.g.materials[mat_index]
        pbr = m.pbrMetallicRoughness
        if pbr and pbr.baseColorTexture is not None:
            tex = self.g.textures[pbr.baseColorTexture.index]
            return tex.source
        return None

    def material_color(self, mat_index):
        if mat_index is None:
            return (1.0, 1.0, 1.0, 1.0)
        m = self.g.materials[mat_index]
        pbr = m.pbrMetallicRoughness
        if pbr and pbr.baseColorFactor:
            return tuple(pbr.baseColorFactor)
        return (1.0, 1.0, 1.0, 1.0)

    # -------------------------------------------------------------- transforms
    def local_matrix(self, ni):
        n = self.g.nodes[ni]
        if n.matrix:
            return np.array(n.matrix, np.float64).reshape(4, 4).T
        t = n.translation or [0, 0, 0]
        r = n.rotation or [0, 0, 0, 1]
        s = n.scale or [1, 1, 1]
        return trs_matrix(t, r, s)

    def parents(self):
        p = [-1] * len(self.g.nodes)
        for i, n in enumerate(self.g.nodes):
            for c in (n.children or []):
                p[c] = i
        return p

    def world_matrices(self):
        par = self.parents()
        cache = {}

        def wm(i):
            if i in cache:
                return cache[i]
            m = self.local_matrix(i)
            if par[i] >= 0:
                m = wm(par[i]) @ m
            cache[i] = m
            return m
        return [wm(i) for i in range(len(self.g.nodes))]


def quat_to_mat3(q):
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ], np.float64)


def trs_matrix(t, r, s):
    m = np.eye(4)
    m[:3, :3] = quat_to_mat3(r) * np.array(s)[None, :]
    m[:3, 3] = t
    return m


def quat_slerp(a, b, t):
    a = np.asarray(a, np.float64)
    b = np.asarray(b, np.float64)
    d = np.dot(a, b)
    if d < 0:
        b = -b
        d = -d
    if d > 0.9995:
        r = a + (b - a) * t
        return r / np.linalg.norm(r)
    th = np.arccos(d)
    s = np.sin(th)
    return (np.sin((1 - t) * th) * a + np.sin(t * th) * b) / s


def sample_channel(times, values, path, t, interp):
    """Sample a glTF animation channel at time t (values already reshaped)."""
    if t <= times[0]:
        i0, i1, f = 0, 0, 0.0
    elif t >= times[-1]:
        i0, i1, f = len(times) - 1, len(times) - 1, 0.0
    else:
        i1 = int(np.searchsorted(times, t, side='right'))
        i0 = i1 - 1
        span = times[i1] - times[i0]
        f = 0.0 if span <= 0 else (t - times[i0]) / span
    if interp == 'CUBICSPLINE':
        # values laid out as (in-tangent, value, out-tangent) triplets; use the value
        vals = values[1::3]
    else:
        vals = values
    if interp == 'STEP':
        f = 0.0
    a = vals[i0]
    b = vals[i1]
    if path == 'rotation':
        return quat_slerp(a, b, f)
    return a + (b - a) * f

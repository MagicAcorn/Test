"""Skeleton + animation extraction for KayKit-style rigs.

Only joints that deform the mesh (plus their ancestors and named attachment
joints) are kept, which drops the IK/control helpers the rigs ship with.
"""
import struct
import numpy as np
from gltfutil import Gltf, sample_channel, trs_matrix
from meshbuild import Part, fnv1a, build_model

ATTACH_JOINTS = ('handslot.l', 'handslot.r', 'head')


class Rig:
    def __init__(self, gl: Gltf, extra=ATTACH_JOINTS):
        g = gl.g
        self.gl = gl
        skin = g.skins[0]
        self.skin_joints = list(skin.joints)
        names = [g.nodes[j].name for j in skin.joints]
        ibm = gl.accessor(skin.inverseBindMatrices).reshape(-1, 4, 4).transpose(0, 2, 1)  # column-major -> rows
        parents = gl.parents()

        used = set()
        for n in g.nodes:
            if n.mesh is None or n.skin is None:
                continue
            for p in g.meshes[n.mesh].primitives:
                J = gl.accessor(p.attributes.JOINTS_0).reshape(-1)
                W = gl.accessor(p.attributes.WEIGHTS_0).reshape(-1)
                for j in np.unique(J[W > 0.01]):
                    used.add(skin.joints[int(j)])
        for j, nm in zip(skin.joints, names):
            if nm in extra:
                used.add(j)
        # add ancestors (within the joint set)
        jointset = set(skin.joints)
        keep = set()
        for j in used:
            k = j
            while k >= 0 and k in jointset:
                keep.add(k)
                k = parents[k]
        # topological order following skin order (parents first)
        order = []
        placed = set()
        pending = [j for j in skin.joints if j in keep]
        while pending:
            progressed = False
            for j in list(pending):
                pj = parents[j]
                if pj not in keep or pj in placed:
                    order.append(j)
                    placed.add(j)
                    pending.remove(j)
                    progressed = True
            assert progressed
        self.nodes = order
        self.node_to_idx = {n: i for i, n in enumerate(order)}
        self.names = [g.nodes[n].name for n in order]
        self.parent = [self.node_to_idx.get(parents[n], -1) for n in order]
        self.ibm = [ibm[self.skin_joints.index(n)] for n in order]
        self.rest = []
        for n in order:
            nd = g.nodes[n]
            self.rest.append((np.array(nd.translation or [0, 0, 0], np.float64),
                              np.array(nd.rotation or [0, 0, 0, 1], np.float64),
                              np.array(nd.scale or [1, 1, 1], np.float64)))
        # world transform of the skeleton root's parent (e.g. the 'Rig' node)
        wm = gl.world_matrices()
        root_parent = parents[order[0]]
        self.root_parent_world = wm[root_parent] if root_parent >= 0 else np.eye(4)

    def signature(self):
        return tuple(self.names)

    def to_bytes(self):
        out = struct.pack('>4sHH', b'SKL1', len(self.nodes), 0)
        for i in range(len(self.nodes)):
            t, r, s = self.rest[i]
            out += struct.pack('>Ih2x3f4f3f', fnv1a(self.names[i]), self.parent[i], *t, *r, *s)
        for m in self.ibm:
            out += struct.pack('>12f', *m[:3, :].reshape(-1))
        rp = self.root_parent_world
        out += struct.pack('>12f', *rp[:3, :].reshape(-1))
        return out

    # -------------------------------------------------------------- skinning
    def skinned_parts(self, texname_for_image, max_infl=2, quant=8, ignore_nodes=()):
        """Returns (parts, envelopes) where each vertex has a draw matrix id:
        id < num_joints => rigid to joint, else envelope id - num_joints."""
        gl = self.gl
        g = gl.g
        nj = len(self.nodes)
        env_index = {}
        envelopes = []
        parts = []
        skin = g.skins[0]
        for n in g.nodes:
            if n.mesh is None or n.skin is None or n.name in ignore_nodes:
                continue
            for p in g.meshes[n.mesh].primitives:
                pos = gl.accessor(p.attributes.POSITION).astype(np.float64)
                nrm = gl.accessor(p.attributes.NORMAL).astype(np.float64)
                uv = gl.accessor(p.attributes.TEXCOORD_0).astype(np.float64) if p.attributes.TEXCOORD_0 is not None else None
                J = gl.accessor(p.attributes.JOINTS_0).astype(np.int64)
                W = gl.accessor(p.attributes.WEIGHTS_0).astype(np.float64)
                idx = gl.accessor(p.indices).reshape(-1, 3).astype(np.int64)
                dm = np.zeros(len(pos), np.int64)
                for v in range(len(pos)):
                    infl = {}
                    for j, w in zip(J[v], W[v]):
                        if w <= 0.0:
                            continue
                        node = skin.joints[int(j)]
                        ji = self.node_to_idx.get(node)
                        if ji is None:
                            continue
                        infl[ji] = infl.get(ji, 0.0) + float(w)
                    items = sorted(infl.items(), key=lambda kv: -kv[1])[:max_infl]
                    tot = sum(w for _, w in items)
                    items = [(j, w / tot) for j, w in items]
                    # quantise weights to 1/quant steps, drop tiny ones
                    q = [(j, int(round(w * quant))) for j, w in items]
                    q = [(j, w) for j, w in q if w > 0]
                    diff = quant - sum(w for _, w in q)
                    q[0] = (q[0][0], q[0][1] + diff)
                    if len(q) == 1:
                        dm[v] = q[0][0]
                    else:
                        key = tuple(sorted((j, w) for j, w in q))
                        e = env_index.get(key)
                        if e is None:
                            e = len(envelopes)
                            env_index[key] = e
                            envelopes.append([(j, int(round(w * 255 / quant))) for j, w in key])
                        dm[v] = nj + e
                img = gl.material_image(p.material)
                tex = texname_for_image(img) if img is not None else None
                if uv is not None:
                    uv = uv.copy()
                parts.append(Part(pos, idx, nrm, uv, None, tex, 0, dm))
        # rigid cosmetic meshes parented to joints (hats, capes, helmets...)
        wm = gl.world_matrices()
        par = gl.parents()
        for ni, n in enumerate(g.nodes):
            if n.mesh is None or n.skin is not None or n.name in ignore_nodes:
                continue
            k = par[ni]
            while k >= 0 and k not in self.node_to_idx:
                k = par[k]
            if k < 0:
                continue
            jname = g.nodes[k].name
            if jname.startswith('handslot'):
                continue  # held items are separate attachments
            ji = self.node_to_idx[k]
            to_mesh = np.linalg.inv(self.ibm[ji]) @ np.linalg.inv(wm[k]) @ wm[ni]
            for p in g.meshes[n.mesh].primitives:
                pos = gl.accessor(p.attributes.POSITION).astype(np.float64)
                nrm = gl.accessor(p.attributes.NORMAL).astype(np.float64)
                uv = gl.accessor(p.attributes.TEXCOORD_0).astype(np.float64) if p.attributes.TEXCOORD_0 is not None else None
                idx = gl.accessor(p.indices).reshape(-1, 3).astype(np.int64)
                img = gl.material_image(p.material)
                tex = texname_for_image(img) if img is not None else None
                part = Part(pos, idx, nrm, uv, None, tex, 0, np.full(len(pos), ji))
                parts.append(part.transformed(to_mesh))
        # make envelope weights sum to exactly 255
        for e in envelopes:
            s = sum(w for _, w in e)
            e[0] = (e[0][0], e[0][1] + 255 - s)
        return parts, envelopes

    def item_parts(self, node_name, texname_for_image):
        """Mesh of a held item node expressed in its handslot joint's local space."""
        gl = self.gl
        g = gl.g
        wm = gl.world_matrices()
        par = gl.parents()
        for ni, n in enumerate(g.nodes):
            if n.name != node_name or n.mesh is None:
                continue
            k = par[ni]
            while k >= 0 and k not in self.node_to_idx:
                k = par[k]
            local = np.linalg.inv(wm[k]) @ wm[ni]
            parts = []
            for p in g.meshes[n.mesh].primitives:
                pos = gl.accessor(p.attributes.POSITION).astype(np.float64)
                nrm = gl.accessor(p.attributes.NORMAL).astype(np.float64)
                uv = gl.accessor(p.attributes.TEXCOORD_0).astype(np.float64) if p.attributes.TEXCOORD_0 is not None else None
                idx = gl.accessor(p.indices).reshape(-1, 3).astype(np.int64)
                img = gl.material_image(p.material)
                tex = texname_for_image(img) if img is not None else None
                parts.append(Part(pos, idx, nrm, uv, None, tex, 0).transformed(local))
            return parts, g.nodes[k].name
        raise KeyError(node_name)

    # ------------------------------------------------------------ animation
    def sample_clip(self, anim, fps=30.0):
        gl = self.gl
        chans = {}
        dur = 0.0
        for c in anim.channels:
            node = c.target.node
            if node not in self.node_to_idx:
                continue
            s = anim.samplers[c.sampler]
            times = gl.accessor(s.input).reshape(-1).astype(np.float64)
            vals = gl.accessor(s.output).astype(np.float64)
            if (s.interpolation or 'LINEAR') == 'CUBICSPLINE':
                vals = vals[1::3]
            dur = max(dur, float(times[-1]))
            chans[(self.node_to_idx[node], c.target.path)] = (times, vals, s.interpolation or 'LINEAR')
        nframes = max(2, int(round(dur * fps)) + 1)
        ft = np.minimum(np.arange(nframes) / fps, dur)
        nj = len(self.nodes)
        rot = np.zeros((nframes, nj, 4))
        tra = np.zeros((nframes, nj, 3))

        def locate(times):
            i1 = np.clip(np.searchsorted(times, ft, side='right'), 1, max(1, len(times) - 1))
            i0 = i1 - 1
            if len(times) == 1:
                return np.zeros(nframes, int), np.zeros(nframes, int), np.zeros(nframes)
            span = times[i1] - times[i0]
            f = np.where(span > 0, (ft - times[i0]) / np.where(span > 0, span, 1), 0.0)
            return i0, i1, np.clip(f, 0.0, 1.0)

        for j in range(nj):
            rt, rr, rs = self.rest[j]
            ch = chans.get((j, 'rotation'))
            if ch:
                times, vals, interp = ch
                i0, i1, f = locate(times)
                if interp == 'STEP':
                    f = np.zeros_like(f)
                a = vals[i0]
                b = vals[i1]
                d = (a * b).sum(axis=1, keepdims=True)
                b = np.where(d < 0, -b, b)
                q = a + (b - a) * f[:, None]
                rot[:, j] = q / np.linalg.norm(q, axis=1, keepdims=True)
            else:
                rot[:, j] = rr
            ch = chans.get((j, 'translation'))
            if ch:
                times, vals, interp = ch
                i0, i1, f = locate(times)
                if interp == 'STEP':
                    f = np.zeros_like(f)
                tra[:, j] = vals[i0] + (vals[i1] - vals[i0]) * f[:, None]
            else:
                tra[:, j] = rt
        # keep quaternion hemisphere continuous for nlerp at runtime
        for j in range(nj):
            for f in range(1, nframes):
                if np.dot(rot[f, j], rot[f - 1, j]) < 0:
                    rot[f, j] = -rot[f, j]
        return dur, fps, rot, tra


def anim_to_bytes(dur, fps, rot, tra, rest, loop):
    nframes, nj = rot.shape[:2]
    # which joints have animated translation (beyond rest)
    animated = []
    for j in range(nj):
        if np.abs(tra[:, j] - rest[j][0]).max() > 1e-4:
            animated.append(j)
    tscale = max(1e-6, float(np.abs(tra[:, animated]).max()) if animated else 1.0)
    tmap = [-1] * nj
    for k, j in enumerate(animated):
        tmap[j] = k
    hdr = struct.pack('>4sHHffIfHH', b'ANM1', nj, nframes, fps, dur, 1 if loop else 0, tscale / 32767.0,
                      len(animated), 0)
    tm = struct.pack('>%db' % nj, *tmap)
    tm += b'\0' * ((-len(tm)) % 4)
    q = np.clip(np.round(rot / np.linalg.norm(rot, axis=-1, keepdims=True) * 32767), -32767, 32767).astype('>i2')
    rq = q.tobytes()
    if animated:
        tq = np.clip(np.round(tra[:, animated] / tscale * 32767), -32767, 32767).astype('>i2').tobytes()
    else:
        tq = b''
    return hdr + tm + rq + tq

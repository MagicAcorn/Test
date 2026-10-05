#!/usr/bin/env python3
"""Builds data/assets.pak from third_party CC0 packs + procedural content.

Usage: python3 tools/build_assets.py [--kaykit DIR] [--out data/assets.pak]
"""
import argparse
import io
import os
import struct
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import gxtex  # noqa: E402
from gltfutil import Gltf  # noqa: E402
from meshbuild import Part, build_model, fnv1a  # noqa: E402
from pak import PakWriter  # noqa: E402
from skelanim import Rig, anim_to_bytes  # noqa: E402
import manifest  # noqa: E402
import procgen  # noqa: E402
import worldgen  # noqa: E402
import fontgen  # noqa: E402
import uigen  # noqa: E402
import audiogen  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))


def tex_asset(img, fmt, wrap=(0, 0), filt=(1, 1), levels=1):
    """img: HxWx4 uint8. wrap: 0 clamp,1 repeat,2 mirror. filt min/mag: 0 near,1 linear, (min 5 = lin_mip_lin)"""
    h, w = img.shape[:2]
    data = gxtex.encode_mips(img, fmt, levels) if levels > 1 else gxtex.encode(img, fmt)
    hdr = struct.pack('>4sHHBBBBBB2xII', b'TEX1', w, h, fmt, levels, wrap[0], wrap[1], filt[0], filt[1], 32, len(data))
    hdr += b'\0' * (32 - len(hdr))
    return hdr + data


class Builder:
    def __init__(self, kaykit):
        self.kaykit = kaykit
        self.pak = PakWriter()
        self.report = []
        self.tex_done = set()

    def path(self, rel):
        return os.path.join(self.kaykit, rel)

    # ------------------------------------------------------------- textures
    def add_texture_png(self, name, png_bytes_or_path, size, fmt='CMPR', wrap=(0, 0), levels=1, filt=(1, 1)):
        if name in self.tex_done:
            return name
        if isinstance(png_bytes_or_path, (bytes, bytearray)):
            im = Image.open(io.BytesIO(png_bytes_or_path))
        else:
            im = Image.open(png_bytes_or_path)
        im = im.convert('RGBA')
        if size and im.size != (size, size):
            im = im.resize((size, size), Image.LANCZOS)
        arr = np.asarray(im).copy()
        self.add_texture(name, arr, fmt, wrap, levels, filt)
        return name

    def add_texture(self, name, arr, fmt='CMPR', wrap=(0, 0), levels=1, filt=(1, 1)):
        f = gxtex.FORMAT_NAMES[fmt] if isinstance(fmt, str) else fmt
        data = tex_asset(arr, f, wrap, filt, levels)
        self.pak.add(name, 'TEX ', data)
        self.tex_done.add(name)
        self.report.append(('tex', name, len(data)))

    # --------------------------------------------------------------- models
    def gltf_texname(self, gl, tex_override=None):
        cache = {}

        def fn(img_index):
            if img_index in cache:
                return cache[img_index]
            iname = gl.image_name(img_index)
            if tex_override:
                iname = tex_override.get(iname, iname)
            name = 'tex/' + iname.lower().replace('.png', '')
            if name not in self.tex_done:
                self.add_texture_png(name, gl.image_bytes(img_index), 256, 'CMPR')
            cache[img_index] = name
            return name
        return fn

    def static_parts_from_gltf(self, gl, scale=1.0, uv_remap=None, flags=0, skip_nodes=(), only_nodes=None):
        g = gl.g
        wm = gl.world_matrices()
        texname = self.gltf_texname(gl)
        parts = []
        S = np.diag([scale, scale, scale, 1.0])
        for ni, n in enumerate(g.nodes):
            if n.mesh is None or n.skin is not None:
                continue
            if n.name in skip_nodes:
                continue
            if only_nodes is not None and n.name not in only_nodes:
                continue
            m = S @ wm[ni]
            for p in g.meshes[n.mesh].primitives:
                pos = gl.accessor(p.attributes.POSITION).astype(np.float64)
                nrm = gl.accessor(p.attributes.NORMAL).astype(np.float64) if p.attributes.NORMAL is not None else None
                uv = gl.accessor(p.attributes.TEXCOORD_0).astype(np.float64) if p.attributes.TEXCOORD_0 is not None else None
                if p.indices is not None:
                    idx = gl.accessor(p.indices).reshape(-1, 3).astype(np.int64)
                else:
                    idx = np.arange(len(pos)).reshape(-1, 3)
                img = gl.material_image(p.material)
                tex = texname(img) if img is not None else None
                clr = None
                if tex is None:
                    c = gl.material_color(p.material)
                    clr = np.tile(np.array([int(c[0] * 255), int(c[1] * 255), int(c[2] * 255), 255], np.uint8), (len(pos), 1))
                if uv is not None and uv_remap is not None:
                    uv = uv_remap(uv)
                part = Part(pos, idx, nrm, uv, clr, tex, flags)
                parts.append(part.transformed(m))
        return parts

    def add_model(self, name, parts, skin=None):
        data, st = build_model(parts, skin=skin, name=name)
        self.pak.add(name, 'MDL ', data)
        self.report.append(('mdl', name, len(data), st['tris'], st['batches']))
        return st

    def add_static_gltf(self, name, rel, scale=1.0, ground=False, uv_remap=None, flags=0, skip_nodes=(), only_nodes=None, center_xz=False):
        gl = Gltf(self.path(rel))
        parts = self.static_parts_from_gltf(gl, scale, uv_remap, flags, skip_nodes, only_nodes)
        if ground or center_xz:
            allp = np.concatenate([p.pos for p in parts])
            off = np.zeros(3)
            if ground:
                off[1] = -allp[:, 1].min()
            if center_xz:
                off[0] = -(allp[:, 0].min() + allp[:, 0].max()) / 2
                off[2] = -(allp[:, 2].min() + allp[:, 2].max()) / 2
            for p in parts:
                p.pos = p.pos + off
        return self.add_model(name, parts)

    # ------------------------------------------------------------ characters
    def add_rig_and_anims(self, skel_name, rel, clips, anim_prefix):
        gl = Gltf(self.path(rel))
        rig = Rig(gl)
        self.pak.add(skel_name, 'SKEL', rig.to_bytes())
        self.report.append(('skel', skel_name, len(rig.names)))
        byname = {a.name: a for a in gl.g.animations}
        for clip, (src, loop) in clips.items():
            a = byname.get(src)
            if a is None:
                print('  !! missing clip', src, 'in', rel)
                continue
            dur, fps, rot, tra = rig.sample_clip(a, 30.0)
            data = anim_to_bytes(dur, fps, rot, tra, rig.rest, loop)
            self.pak.add(anim_prefix + clip, 'ANIM', data)
            self.report.append(('anim', anim_prefix + clip, len(data)))
        return rig

    def add_character(self, name, rel, skel_name, ref_rig, ignore_nodes=()):
        gl = Gltf(self.path(rel))
        rig = Rig(gl)
        assert rig.signature() == ref_rig.signature(), (rel, rig.names, ref_rig.names)
        texname = self.gltf_texname(gl)
        parts, envs = rig.skinned_parts(texname, ignore_nodes=ignore_nodes)
        skin = dict(num_joints=len(rig.names), envelopes=envs, skel_hash=fnv1a(skel_name))
        # use this character's own inverse bind matrices if they differ from the reference
        st = self.add_model(name, parts, skin)
        if not all(np.allclose(a, b, atol=1e-4) for a, b in zip(rig.ibm, ref_rig.ibm)):
            # store a per-model skeleton override
            self.pak.add(name + '#skel', 'SKEL', rig.to_bytes())
        return st, len(envs)

    def add_attachment(self, name, rel, scale=1.0, skip_nodes=()):
        """Weapon/prop meshes that get parented to a joint (no baked node transform at root)."""
        return self.add_static_gltf(name, rel, scale, skip_nodes=skip_nodes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kaykit', default=os.path.join(ROOT, 'third_party', 'kaykit'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'data', 'assets.pak'))
    ap.add_argument('--only', default=None, help='comma list of stages')
    args = ap.parse_args()
    t0 = time.time()
    b = Builder(args.kaykit)
    stages = args.only.split(',') if args.only else None
    manifest.build_all(b, stages)
    procgen.build_all(b, stages)
    worldgen.build_all(b, stages)
    fontgen.build_all(b, stages)
    uigen.build_all(b, stages)
    audiogen.build_all(b, stages)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    total = b.pak.write(args.out)
    kinds = {}
    for r in b.report:
        kinds.setdefault(r[0], [0, 0])
        kinds[r[0]][0] += 1
        kinds[r[0]][1] += r[2]
    for k, (n, sz) in sorted(kinds.items()):
        print('  %-5s %4d assets %9d bytes' % (k, n, sz))
    print('wrote %s: %d assets, %.2f MB in %.1fs' % (args.out, len(b.pak.entries), total / 1048576.0, time.time() - t0))


if __name__ == '__main__':
    main()

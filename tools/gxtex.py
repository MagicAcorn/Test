"""GameCube/Wii GX texture encoders (and reference decoders for testing).

All output is big-endian and laid out in the GX tile order the GPU expects,
so the bytes can be handed straight to GX_InitTexObj().
"""
import numpy as np

GX_TF_I4 = 0x0
GX_TF_I8 = 0x1
GX_TF_IA4 = 0x2
GX_TF_IA8 = 0x3
GX_TF_RGB565 = 0x4
GX_TF_RGB5A3 = 0x5
GX_TF_RGBA8 = 0x6
GX_TF_CMPR = 0xE

# (tile width, tile height) for each format
TILE = {
    GX_TF_I4: (8, 8),
    GX_TF_I8: (8, 4),
    GX_TF_IA4: (8, 4),
    GX_TF_IA8: (4, 4),
    GX_TF_RGB565: (4, 4),
    GX_TF_RGB5A3: (4, 4),
    GX_TF_RGBA8: (4, 4),
    GX_TF_CMPR: (8, 8),
}

FORMAT_NAMES = {
    'I4': GX_TF_I4, 'I8': GX_TF_I8, 'IA4': GX_TF_IA4, 'IA8': GX_TF_IA8,
    'RGB565': GX_TF_RGB565, 'RGB5A3': GX_TF_RGB5A3, 'RGBA8': GX_TF_RGBA8,
    'CMPR': GX_TF_CMPR,
}


def _pad_to_tiles(img, tw, th):
    h, w = img.shape[:2]
    ph = (th - h % th) % th
    pw = (tw - w % tw) % tw
    if ph or pw:
        img = np.pad(img, ((0, ph), (0, pw), (0, 0)), mode='edge')
    return img


def _tiles(img, tw, th):
    """Return array (tilesY, tilesX, th, tw, C) in row-major tile order."""
    h, w, c = img.shape
    return img.reshape(h // th, th, w // tw, tw, c).transpose(0, 2, 1, 3, 4)


def _luma(rgb):
    rgb = rgb.astype(np.float32)
    return np.clip(rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114 + 0.5, 0, 255).astype(np.uint8)


def to565(rgb):
    rgb = rgb.astype(np.int32)
    r = (rgb[..., 0] * 31 + 127) // 255
    g = (rgb[..., 1] * 63 + 127) // 255
    b = (rgb[..., 2] * 31 + 127) // 255
    return ((r << 11) | (g << 5) | b).astype(np.uint16)


def from565(c):
    c = c.astype(np.int32)
    r = (c >> 11) & 31
    g = (c >> 5) & 63
    b = c & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], axis=-1)


def encode(img, fmt):
    """img: HxWx4 uint8 RGBA. Returns bytes in GX tiled layout."""
    assert img.dtype == np.uint8 and img.ndim == 3 and img.shape[2] == 4
    tw, th = TILE[fmt]
    img = _pad_to_tiles(img, tw, th)
    if fmt == GX_TF_CMPR:
        return _encode_cmpr(img)
    t = _tiles(img, tw, th)  # (ty, tx, th, tw, 4)
    if fmt == GX_TF_I4:
        l = _luma(t[..., :3]) >> 4
        l = l.reshape(-1, 2)
        return ((l[:, 0] << 4) | l[:, 1]).astype(np.uint8).tobytes()
    if fmt == GX_TF_I8:
        return _luma(t[..., :3]).astype(np.uint8).tobytes()
    if fmt == GX_TF_IA4:
        l = _luma(t[..., :3]) >> 4
        a = t[..., 3] >> 4
        return ((a << 4) | l).astype(np.uint8).tobytes()
    if fmt == GX_TF_IA8:
        l = _luma(t[..., :3])
        a = t[..., 3]
        return np.stack([a, l], axis=-1).astype(np.uint8).tobytes()
    if fmt == GX_TF_RGB565:
        return to565(t[..., :3]).astype('>u2').tobytes()
    if fmt == GX_TF_RGB5A3:
        rgb = t[..., :3].astype(np.int32)
        a = t[..., 3].astype(np.int32)
        opaque = a >= 0xE0
        r5 = (rgb[..., 0] * 31 + 127) // 255
        g5 = (rgb[..., 1] * 31 + 127) // 255
        b5 = (rgb[..., 2] * 31 + 127) // 255
        v_op = 0x8000 | (r5 << 10) | (g5 << 5) | b5
        a3 = (a * 7 + 127) // 255
        r4 = (rgb[..., 0] * 15 + 127) // 255
        g4 = (rgb[..., 1] * 15 + 127) // 255
        b4 = (rgb[..., 2] * 15 + 127) // 255
        v_tr = (a3 << 12) | (r4 << 8) | (g4 << 4) | b4
        v = np.where(opaque, v_op, v_tr)
        return v.astype('>u2').tobytes()
    if fmt == GX_TF_RGBA8:
        flat = t.reshape(t.shape[0], t.shape[1], 16, 4)
        ar = flat[..., [3, 0]].reshape(t.shape[0], t.shape[1], 32)
        gb = flat[..., [1, 2]].reshape(t.shape[0], t.shape[1], 32)
        return np.concatenate([ar, gb], axis=-1).astype(np.uint8).tobytes()
    raise ValueError('unsupported format %r' % fmt)


def _palette(c0, c1):
    """GameCube CMPR palette for arrays of 565 endpoints -> (N,4,4) RGBA int."""
    p0 = from565(c0)
    p1 = from565(c1)
    four = (c0 > c1)[:, None]
    # hardware blends with 3/8 and 5/8 weights
    p2_4 = (p0 * 5 + p1 * 3) >> 3
    p3_4 = (p0 * 3 + p1 * 5) >> 3
    p2_3 = (p0 + p1) >> 1
    p2 = np.where(four, p2_4, p2_3)
    p3 = np.where(four, p3_4, p2_3)
    pal = np.stack([p0, p1, p2, p3], axis=1)  # (N,4,3)
    alpha = np.full((len(c0), 4), 255, np.int32)
    alpha[:, 3] = np.where(four[:, 0], 255, 0)
    return np.concatenate([pal, alpha[..., None]], axis=-1)


def _encode_cmpr_blocks(blocks):
    """blocks: (N,16,4) uint8 RGBA -> (N,8) bytes."""
    n = blocks.shape[0]
    px = blocks.astype(np.float32)
    rgb = px[..., :3]
    transparent = blocks[..., 3] < 128            # (N,16)
    has_alpha = transparent.any(axis=1)          # (N,)
    w = (~transparent).astype(np.float32)          # weight opaque pixels only
    wsum = np.maximum(w.sum(axis=1, keepdims=True), 1e-6)
    mean = (rgb * w[..., None]).sum(axis=1) / wsum  # (N,3)
    d = (rgb - mean[:, None, :]) * w[..., None]
    cov = np.einsum('nki,nkj->nij', d, d)
    # principal axis via power iteration
    v = np.ones((n, 3), np.float32) / np.sqrt(3)
    for _ in range(8):
        v = np.einsum('nij,nj->ni', cov, v)
        v /= np.maximum(np.linalg.norm(v, axis=1, keepdims=True), 1e-6)
    proj = np.einsum('nki,ni->nk', rgb - mean[:, None, :], v)
    big = 1e9
    pmin = np.where(w > 0, proj, big).min(axis=1)
    pmax = np.where(w > 0, proj, -big).max(axis=1)
    pmin = np.where(pmin > big / 2, 0, pmin)
    pmax = np.where(pmax < -big / 2, 0, pmax)
    e0 = np.clip(mean + v * pmax[:, None], 0, 255)
    e1 = np.clip(mean + v * pmin[:, None], 0, 255)

    best_err = np.full(n, np.inf)
    best_c0 = np.zeros(n, np.uint16)
    best_c1 = np.zeros(n, np.uint16)
    best_idx = np.zeros((n, 16), np.int32)

    # Try a few endpoint candidates (inset variants) and keep the best.
    for inset in (0.0, 1.0 / 16, 1.0 / 8):
        a = e0 + (e1 - e0) * inset
        b = e1 + (e0 - e1) * inset
        c0 = to565(a)
        c1 = to565(b)
        # opaque blocks need c0 > c1 (4 colour mode); transparent need c0 <= c1
        sw = np.where(has_alpha, c0 > c1, c0 < c1)
        c0s = np.where(sw, c1, c0)
        c1s = np.where(sw, c0, c1)
        # solid opaque colour with c0 == c1 falls into 3-colour mode which is fine
        pal = _palette(c0s, c1s)  # (N,4,4)
        diff = rgb[:, :, None, :] - pal[:, None, :, :3]
        err = (diff * diff).sum(axis=-1)  # (N,16,4)
        # never pick the transparent entry for opaque pixels / in 3 colour mode
        three = ~(c0s > c1s)
        err[:, :, 3] = np.where(three[:, None], np.inf, err[:, :, 3])
        idx = err.argmin(axis=-1)
        e = np.take_along_axis(err, idx[..., None], axis=-1)[..., 0]
        idx = np.where(transparent, 3, idx)
        e = np.where(transparent, 0, e).sum(axis=1)
        better = e < best_err
        best_err = np.where(better, e, best_err)
        best_c0 = np.where(better, c0s, best_c0)
        best_c1 = np.where(better, c1s, best_c1)
        best_idx = np.where(better[:, None], idx, best_idx)

    out = np.zeros((n, 8), np.uint8)
    out[:, 0] = best_c0 >> 8
    out[:, 1] = best_c0 & 0xFF
    out[:, 2] = best_c1 >> 8
    out[:, 3] = best_c1 & 0xFF
    rows = best_idx.reshape(n, 4, 4)
    out[:, 4:8] = ((rows[..., 0] << 6) | (rows[..., 1] << 4) | (rows[..., 2] << 2) | rows[..., 3]).astype(np.uint8)
    return out


def _encode_cmpr(img):
    t = _tiles(img, 8, 8)  # (ty, tx, 8, 8, 4)
    ty, tx = t.shape[:2]
    # split each 8x8 tile into 4 sub blocks TL, TR, BL, BR
    sub = t.reshape(ty, tx, 2, 4, 2, 4, 4).transpose(0, 1, 2, 4, 3, 5, 6)  # ty,tx,by,bx,4,4,4
    blocks = sub.reshape(-1, 16, 4)
    enc = _encode_cmpr_blocks(blocks)
    return enc.tobytes()


def decode(data, w, h, fmt):
    """Reference decoder (used for previews/tests). Returns HxWx4 uint8."""
    tw, th = TILE[fmt]
    pw = (w + tw - 1) // tw * tw
    ph = (h + th - 1) // th * th
    ntx, nty = pw // tw, ph // th
    buf = np.frombuffer(data, np.uint8)
    if fmt == GX_TF_CMPR:
        blocks = buf[:ntx * nty * 32].reshape(-1, 8)
        c0 = (blocks[:, 0].astype(np.uint16) << 8) | blocks[:, 1]
        c1 = (blocks[:, 2].astype(np.uint16) << 8) | blocks[:, 3]
        pal = _palette(c0, c1)
        idx = np.stack([(blocks[:, 4:8] >> s) & 3 for s in (6, 4, 2, 0)], axis=-1)  # (N,4rows,4cols)
        out_blocks = np.zeros((len(blocks), 4, 4, 4), np.int32)
        for r in range(4):
            for c in range(4):
                out_blocks[:, r, c] = pal[np.arange(len(blocks)), idx[:, r, c]]
        sub = out_blocks.reshape(nty, ntx, 2, 2, 4, 4, 4).transpose(0, 1, 2, 4, 3, 5, 6).reshape(nty, ntx, 8, 8, 4)
        img = sub.transpose(0, 2, 1, 3, 4).reshape(ph, pw, 4)
        return img[:h, :w].astype(np.uint8)
    if fmt == GX_TF_RGB565:
        v = buf[:pw * ph * 2].view('>u2').reshape(nty, ntx, th, tw)
        rgb = from565(v)
        a = np.full(v.shape + (1,), 255)
        t = np.concatenate([rgb, a], axis=-1)
    elif fmt == GX_TF_RGB5A3:
        v = buf[:pw * ph * 2].view('>u2').reshape(nty, ntx, th, tw).astype(np.int32)
        op = (v & 0x8000) != 0
        r = np.where(op, ((v >> 10) & 31) * 255 // 31, ((v >> 8) & 15) * 17)
        g = np.where(op, ((v >> 5) & 31) * 255 // 31, ((v >> 4) & 15) * 17)
        b = np.where(op, (v & 31) * 255 // 31, (v & 15) * 17)
        a = np.where(op, 255, ((v >> 12) & 7) * 255 // 7)
        t = np.stack([r, g, b, a], axis=-1)
    elif fmt == GX_TF_I8:
        v = buf[:pw * ph].reshape(nty, ntx, th, tw).astype(np.int32)
        t = np.stack([v, v, v, v], axis=-1)
    elif fmt == GX_TF_I4:
        v = buf[:pw * ph // 2]
        v = np.stack([v >> 4, v & 15], axis=-1).reshape(nty, ntx, th, tw).astype(np.int32) * 17
        t = np.stack([v, v, v, v], axis=-1)
    elif fmt == GX_TF_IA4:
        v = buf[:pw * ph].reshape(nty, ntx, th, tw).astype(np.int32)
        l = (v & 15) * 17
        a = (v >> 4) * 17
        t = np.stack([l, l, l, a], axis=-1)
    elif fmt == GX_TF_IA8:
        v = buf[:pw * ph * 2].reshape(nty, ntx, th, tw, 2).astype(np.int32)
        t = np.stack([v[..., 1], v[..., 1], v[..., 1], v[..., 0]], axis=-1)
    elif fmt == GX_TF_RGBA8:
        v = buf[:pw * ph * 4].reshape(nty, ntx, 2, 16, 2).astype(np.int32)
        a = v[:, :, 0, :, 0]
        r = v[:, :, 0, :, 1]
        g = v[:, :, 1, :, 0]
        b = v[:, :, 1, :, 1]
        t = np.stack([r, g, b, a], axis=-1).reshape(nty, ntx, th, tw, 4)
    else:
        raise ValueError(fmt)
    img = t.transpose(0, 2, 1, 3, 4).reshape(ph, pw, 4)
    return img[:h, :w].astype(np.uint8)


def level_size(w, h, fmt):
    tw, th = TILE[fmt]
    pw = (w + tw - 1) // tw * tw
    ph = (h + th - 1) // th * th
    bpp = {GX_TF_I4: 4, GX_TF_I8: 8, GX_TF_IA4: 8, GX_TF_IA8: 16, GX_TF_RGB565: 16,
           GX_TF_RGB5A3: 16, GX_TF_RGBA8: 32, GX_TF_CMPR: 4}[fmt]
    return pw * ph * bpp // 8


def encode_mips(img, fmt, levels):
    """Encode img plus (levels-1) box-filtered mip levels, concatenated."""
    from PIL import Image
    out = bytearray()
    cur = img
    h, w = img.shape[:2]
    for lv in range(levels):
        out += encode(cur, fmt)
        w = max(1, w // 2)
        h = max(1, h // 2)
        if lv + 1 < levels:
            cur = np.asarray(Image.fromarray(cur, 'RGBA').resize((w, h), Image.BOX))
    return bytes(out)

// gxemu: software GX for the PC preview/test build. See gxemu.h.
#include "gxemu.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>

namespace {

const int EFB_W = 640;
const int EFB_H = 528;

void gxerr(const char *fmt, ...);

// ------------------------------------------------------------ big endian
inline u16 be16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
inline u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
inline f32 bef32(const u8 *p) {
    u32 v = be32(p);
    f32 f;
    memcpy(&f, &v, 4);
    return f;
}

struct VatAttr {
    u8 cnt = 0, type = 0, frac = 0;
};

struct TexGen {
    u32 type = GX_TG_MTX2x4, src = GX_TG_TEX0, mtx = GX_IDENTITY, normalize = 0, postmtx = 125;
};

struct ChanCtrl {
    u8 enable = 0, ambsrc = GX_SRC_REG, matsrc = GX_SRC_REG, litmask = 0, diffn = GX_DF_NONE, attnfn = GX_AF_NONE;
};

struct TevStage {
    u8 texcoord = GX_TEXCOORDNULL;
    u32 texmap = GX_TEXMAP_NULL;
    u8 color = GX_COLORNULL;
    u8 ca = GX_CC_ZERO, cb = GX_CC_ZERO, cc = GX_CC_ZERO, cd = GX_CC_RASC;
    u8 aa = GX_CA_ZERO, ab = GX_CA_ZERO, ac = GX_CA_ZERO, ad = GX_CA_RASA;
    u8 cop = GX_TEV_ADD, cbias = GX_TB_ZERO, cscale = GX_CS_SCALE_1, cclamp = 1, creg = GX_TEVPREV;
    u8 aop = GX_TEV_ADD, abias = GX_TB_ZERO, ascale = GX_CS_SCALE_1, aclamp = 1, areg = GX_TEVPREV;
    u8 ksel = GX_TEV_KCSEL_1, kasel = GX_TEV_KASEL_1;
    u8 rasswap = 0, texswap = 0;
};

struct Vtx {
    // clip space
    f32 cx, cy, cz, cw;
    // outputs
    f32 col[2][4];   // channel outputs 0..1 (rgba)
    f32 tex[8][3];   // s,t,q
    // screen
    f32 sx, sy, sz, invw;
};

struct DecodedTex {
    int w = 0, h = 0, levels = 0;
    std::vector<std::vector<u8>> lv;  // RGBA8 per level
    std::vector<int> lw, lh;
};

struct State {
    // vertex description
    u8 vcd[GX_VA_MAXATTR];
    VatAttr vat[8][GX_VA_MAXATTR];
    const u8 *arr[GX_VA_MAXATTR];
    u8 stride[GX_VA_MAXATTR];

    // transform unit
    f32 pos[256][4];   // pos/tex matrix memory (rows), dual tex at 64+
    f32 nrm[32][3];    // normal matrix rows
    f32 proj[6];
    u8 projType = GX_PERSPECTIVE;
    u32 curPos = 0;
    u32 curTex[8];
    f32 vpX = 0, vpY = 0, vpW = 640, vpH = 480, vpN = 0, vpF = 1;
    int scX0 = 0, scY0 = 0, scX1 = 640, scY1 = 480;
    u8 cull = GX_CULL_BACK;

    u32 numTexGens = 0;
    TexGen tg[8];

    u8 numChans = 0;
    ChanCtrl chan[4];
    GXColor amb[2], mat[2];
    GXLightObj light[8];

    u8 numTev = 1;
    TevStage tev[16];
    s16 reg[4][4];
    GXColor kcol[4];
    u8 swap[4][4];

    GXTexObj tmap[8];
    bool tmapValid[8];

    // pixel engine
    u8 zEnable = 1, zFunc = GX_LEQUAL, zUpdate = 1, zBefore = 1;
    u8 bmType = GX_BM_NONE, bmSrc = GX_BL_ONE, bmDst = GX_BL_ZERO, bmOp = GX_LO_CLEAR;
    u8 acComp0 = GX_ALWAYS, acRef0 = 0, acOp = GX_AOP_AND, acComp1 = GX_ALWAYS, acRef1 = 0;
    u8 colorUpdate = 1, alphaUpdate = 1, dstAlphaEn = 0, dstAlpha = 0;
    u8 pixFmt = GX_PF_RGB8_Z24;
    u8 fogType = GX_FOG_NONE;
    f32 fogStart = 0, fogEnd = 1, fogNear = 0.1f, fogFar = 1;
    GXColor fogColor = {0, 0, 0, 0};

    // copies
    GXColor clearColor = {0, 0, 0, 255};
    u32 clearZ = 0xFFFFFF;
    u16 dispSrc[4] = {0, 0, 640, 480};
    u16 dispDst[2] = {640, 480};
    u8 vfEnable = 0;
    u8 vfilter[7] = {0, 0, 21, 22, 21, 0, 0};
    u16 texSrc[4] = {0, 0, 0, 0};
    u16 texDst[2] = {0, 0};
    u32 texDstFmt = GX_TF_RGB565;
    u8 texDstMip = 0;

    // framebuffer
    std::vector<u8> efb;      // RGBA
    std::vector<f32> zbuf;
    std::vector<u8> frame;    // RGB of last copy
    int frameW = 640, frameH = 480;

    // display list recording
    bool recording = false;
    u8 *dl = nullptr;
    u32 dlCap = 0, dlLen = 0;
    bool dlOverflow = false;

    // primitive in progress (immediate or recording)
    bool inPrim = false;
    u8 primType = 0, primFmt = 0;
    u16 primCount = 0;
    u32 primVtxSize = 0;
    std::vector<u8> primData;     // immediate mode bytes
    u32 primBytes = 0;            // bytes written so far
    std::vector<std::pair<u8, u8>> vtxLayout;  // (attr, size) sequence for one vertex
    u32 layoutPos = 0;

    std::unordered_map<const void *, DecodedTex> texCache;

    bool raster = true;
    GXEmuStats stats;
    int errors = 0;
    bool inited = false;
};

State *S;

void gxerr(const char *fmt, ...) {
    S->errors++;
    S->stats.errors++;
    if (S->errors > 50) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[gxemu] ERROR: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void checkNotInPrim(const char *fn) {
    if (S->inPrim) gxerr("%s called inside GX_Begin before all %u vertices were sent (would hang/corrupt GP)", fn, S->primCount);
    if (S->recording) {
        // state changes recorded into display lists are not used by this game
        gxerr("%s called while recording a display list (unsupported in this engine)", fn);
    }
}

#define STATE_CALL(name) do { checkNotInPrim(name); } while (0)

// ------------------------------------------------------------ vtx sizes
int compSize(u8 type) {
    switch (type) {
        case GX_U8: case GX_S8: return 1;
        case GX_U16: case GX_S16: return 2;
        case GX_F32: return 4;
    }
    return 0;
}

int clrSize(u8 type) {
    switch (type) {
        case GX_RGB565: return 2;
        case GX_RGB8: return 3;
        case GX_RGBX8: return 4;
        case GX_RGBA4: return 2;
        case GX_RGBA6: return 3;
        case GX_RGBA8: return 4;
    }
    return 0;
}

const u8 kOrder[] = {
    GX_VA_PTNMTXIDX, GX_VA_TEX0MTXIDX, GX_VA_TEX1MTXIDX, GX_VA_TEX2MTXIDX, GX_VA_TEX3MTXIDX,
    GX_VA_TEX4MTXIDX, GX_VA_TEX5MTXIDX, GX_VA_TEX6MTXIDX, GX_VA_TEX7MTXIDX,
    GX_VA_POS, GX_VA_NRM, GX_VA_CLR0, GX_VA_CLR1,
    GX_VA_TEX0, GX_VA_TEX1, GX_VA_TEX2, GX_VA_TEX3, GX_VA_TEX4, GX_VA_TEX5, GX_VA_TEX6, GX_VA_TEX7};

// size of the attribute data itself (not the index)
int attrDataSize(u8 fmt, u8 attr) {
    const VatAttr &v = S->vat[fmt][attr];
    if (attr == GX_VA_POS) return compSize(v.type) * (v.cnt == GX_POS_XYZ ? 3 : 2);
    if (attr == GX_VA_NRM) return compSize(v.type) * (v.cnt == GX_NRM_XYZ ? 3 : 9);
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) return clrSize(v.type);
    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) return compSize(v.type) * (v.cnt == GX_TEX_ST ? 2 : 1);
    return 1;
}

void buildLayout(u8 fmt, std::vector<std::pair<u8, u8>> &out, u32 &size) {
    out.clear();
    size = 0;
    for (u8 a : kOrder) {
        u8 t = S->vcd[a];
        if (t == GX_NONE) continue;
        int sz;
        if (a <= GX_VA_TEX7MTXIDX) {
            if (t != GX_DIRECT) gxerr("matrix index attribute %d must be GX_DIRECT", a);
            sz = 1;
        } else if (t == GX_DIRECT) {
            sz = attrDataSize(fmt, a);
            if (sz == 0) gxerr("attribute %d has invalid format in VTXFMT%d", a, fmt);
        } else if (t == GX_INDEX8) {
            sz = 1;
        } else {
            sz = 2;
        }
        out.push_back({a, (u8)sz});
        size += sz;
    }
    if (S->vcd[GX_VA_POS] == GX_NONE) gxerr("vertex descriptor has no position");
}

// --------------------------------------------------------------- math
inline f32 clamp01(f32 v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

void mtxRow(const f32 *row, const f32 *v4, f32 &out) { out = row[0] * v4[0] + row[1] * v4[1] + row[2] * v4[2] + row[3] * v4[3]; }

// ------------------------------------------------------------ textures
int texTileW(u8 fmt) {
    switch (fmt) {
        case GX_TF_I4: case GX_TF_CMPR: case GX_TF_I8: case GX_TF_IA4: return 8;
        default: return 4;
    }
}
int texTileH(u8 fmt) {
    switch (fmt) {
        case GX_TF_I4: case GX_TF_CMPR: return 8;
        case GX_TF_I8: case GX_TF_IA4: return 4;
        default: return 4;
    }
}
int texBpp(u8 fmt) {
    switch (fmt) {
        case GX_TF_I4: case GX_TF_CMPR: return 4;
        case GX_TF_I8: case GX_TF_IA4: return 8;
        case GX_TF_IA8: case GX_TF_RGB565: case GX_TF_RGB5A3: return 16;
        case GX_TF_RGBA8: return 32;
    }
    return 0;
}

u32 levelBytes(int w, int h, u8 fmt) {
    int tw = texTileW(fmt), th = texTileH(fmt);
    int pw = (w + tw - 1) / tw * tw, ph = (h + th - 1) / th * th;
    return (u32)(pw * ph * texBpp(fmt) / 8);
}

inline void put(u8 *o, int r, int g, int b, int a) { o[0] = r; o[1] = g; o[2] = b; o[3] = a; }

void decodeLevel(const u8 *src, int w, int h, u8 fmt, std::vector<u8> &out) {
    out.assign((size_t)w * h * 4, 0);
    int tw = texTileW(fmt), th = texTileH(fmt);
    int ntx = (w + tw - 1) / tw, nty = (h + th - 1) / th;
    const u8 *p = src;
    for (int ty = 0; ty < nty; ty++) {
        for (int tx = 0; tx < ntx; tx++) {
            if (fmt == GX_TF_CMPR) {
                for (int sb = 0; sb < 4; sb++) {
                    int bx = tx * 8 + (sb & 1) * 4, by = ty * 8 + (sb >> 1) * 4;
                    u16 c0 = be16(p), c1 = be16(p + 2);
                    int pal[4][4];
                    int r0 = (c0 >> 11) & 31, g0 = (c0 >> 5) & 63, b0 = c0 & 31;
                    int r1 = (c1 >> 11) & 31, g1 = (c1 >> 5) & 63, b1 = c1 & 31;
                    r0 = (r0 << 3) | (r0 >> 2); g0 = (g0 << 2) | (g0 >> 4); b0 = (b0 << 3) | (b0 >> 2);
                    r1 = (r1 << 3) | (r1 >> 2); g1 = (g1 << 2) | (g1 >> 4); b1 = (b1 << 3) | (b1 >> 2);
                    pal[0][0] = r0; pal[0][1] = g0; pal[0][2] = b0; pal[0][3] = 255;
                    pal[1][0] = r1; pal[1][1] = g1; pal[1][2] = b1; pal[1][3] = 255;
                    if (c0 > c1) {
                        pal[2][0] = (r0 * 5 + r1 * 3) >> 3; pal[2][1] = (g0 * 5 + g1 * 3) >> 3; pal[2][2] = (b0 * 5 + b1 * 3) >> 3; pal[2][3] = 255;
                        pal[3][0] = (r0 * 3 + r1 * 5) >> 3; pal[3][1] = (g0 * 3 + g1 * 5) >> 3; pal[3][2] = (b0 * 3 + b1 * 5) >> 3; pal[3][3] = 255;
                    } else {
                        pal[2][0] = (r0 + r1) >> 1; pal[2][1] = (g0 + g1) >> 1; pal[2][2] = (b0 + b1) >> 1; pal[2][3] = 255;
                        pal[3][0] = pal[2][0]; pal[3][1] = pal[2][1]; pal[3][2] = pal[2][2]; pal[3][3] = 0;
                    }
                    for (int y = 0; y < 4; y++) {
                        u8 row = p[4 + y];
                        for (int x = 0; x < 4; x++) {
                            int i = (row >> (6 - 2 * x)) & 3;
                            int X = bx + x, Y = by + y;
                            if (X < w && Y < h) put(&out[((size_t)Y * w + X) * 4], pal[i][0], pal[i][1], pal[i][2], pal[i][3]);
                        }
                    }
                    p += 8;
                }
                continue;
            }
            for (int y = 0; y < th; y++) {
                for (int x = 0; x < tw; x++) {
                    int X = tx * tw + x, Y = ty * th + y;
                    int r = 0, g = 0, b = 0, a = 255;
                    int i = y * tw + x;
                    switch (fmt) {
                        case GX_TF_I4: {
                            u8 v = p[i >> 1];
                            int l = (i & 1) ? (v & 15) : (v >> 4);
                            r = g = b = a = l * 17;
                            break;
                        }
                        case GX_TF_I8: r = g = b = a = p[i]; break;
                        case GX_TF_IA4: { u8 v = p[i]; r = g = b = (v & 15) * 17; a = (v >> 4) * 17; break; }
                        case GX_TF_IA8: a = p[i * 2]; r = g = b = p[i * 2 + 1]; break;
                        case GX_TF_RGB565: {
                            u16 v = be16(p + i * 2);
                            r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31;
                            r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
                            break;
                        }
                        case GX_TF_RGB5A3: {
                            u16 v = be16(p + i * 2);
                            if (v & 0x8000) {
                                r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31;
                                r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
                            } else {
                                a = (v >> 12) & 7; a = (a << 5) | (a << 2) | (a >> 1);
                                r = ((v >> 8) & 15) * 17; g = ((v >> 4) & 15) * 17; b = (v & 15) * 17;
                            }
                            break;
                        }
                        case GX_TF_RGBA8:
                            a = p[i * 2]; r = p[i * 2 + 1]; g = p[32 + i * 2]; b = p[32 + i * 2 + 1];
                            break;
                        default:
                            gxerr("unsupported texture format %d", fmt);
                            return;
                    }
                    if (X < w && Y < h) put(&out[((size_t)Y * w + X) * 4], r, g, b, a);
                }
            }
            p += tw * th * texBpp(fmt) / 8;
        }
    }
}

DecodedTex &getTex(const GXTexObj &t) {
    auto it = S->texCache.find(t.data);
    if (it != S->texCache.end() && it->second.w == t.width && it->second.h == t.height) return it->second;
    DecodedTex &d = S->texCache[t.data];
    d = DecodedTex();
    d.w = t.width;
    d.h = t.height;
    int levels = 1;
    if (t.mipmap) levels = (int)t.maxlod + 1;
    d.levels = levels;
    const u8 *p = (const u8 *)t.data;
    int w = t.width, h = t.height;
    for (int l = 0; l < levels; l++) {
        d.lv.emplace_back();
        decodeLevel(p, w, h, t.fmt, d.lv.back());
        d.lw.push_back(w);
        d.lh.push_back(h);
        p += levelBytes(w, h, t.fmt);
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    return d;
}

inline int wrapCoord(int c, int size, u8 mode) {
    if (mode == GX_CLAMP) return c < 0 ? 0 : (c >= size ? size - 1 : c);
    if (mode == GX_REPEAT) { c %= size; return c < 0 ? c + size : c; }
    // mirror
    int period = size * 2;
    c %= period;
    if (c < 0) c += period;
    return c < size ? c : period - 1 - c;
}

void sampleLevel(const DecodedTex &d, int lv, const GXTexObj &t, f32 s, f32 tt, bool linear, f32 out[4]) {
    int w = d.lw[lv], h = d.lh[lv];
    const u8 *px = d.lv[lv].data();
    if (!linear) {
        int x = wrapCoord((int)floorf(s * w), w, t.wrap_s);
        int y = wrapCoord((int)floorf(tt * h), h, t.wrap_t);
        const u8 *q = px + ((size_t)y * w + x) * 4;
        out[0] = q[0]; out[1] = q[1]; out[2] = q[2]; out[3] = q[3];
        return;
    }
    f32 u = s * w - 0.5f, v = tt * h - 0.5f;
    int x0 = (int)floorf(u), y0 = (int)floorf(v);
    f32 fx = u - x0, fy = v - y0;
    int xa = wrapCoord(x0, w, t.wrap_s), xb = wrapCoord(x0 + 1, w, t.wrap_s);
    int ya = wrapCoord(y0, h, t.wrap_t), yb = wrapCoord(y0 + 1, h, t.wrap_t);
    const u8 *a = px + ((size_t)ya * w + xa) * 4;
    const u8 *b = px + ((size_t)ya * w + xb) * 4;
    const u8 *c = px + ((size_t)yb * w + xa) * 4;
    const u8 *e = px + ((size_t)yb * w + xb) * 4;
    for (int k = 0; k < 4; k++) {
        f32 top = a[k] + (b[k] - a[k]) * fx;
        f32 bot = c[k] + (e[k] - c[k]) * fx;
        out[k] = top + (bot - top) * fy;
    }
}

void sampleTex(int map, f32 s, f32 t, f32 lod, f32 out[4]) {
    if (map < 0 || map > 7 || !S->tmapValid[map]) {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    const GXTexObj &to = S->tmap[map];
    DecodedTex &d = getTex(to);
    lod += to.lodbias;
    bool magnify = lod <= 0.0f;
    u8 filt = magnify ? to.magfilt : to.minfilt;
    bool linear = (filt == GX_LINEAR || filt == GX_LIN_MIP_NEAR || filt == GX_LIN_MIP_LIN);
    bool mip = !magnify && d.levels > 1 && (filt >= GX_NEAR_MIP_NEAR);
    if (!mip) {
        sampleLevel(d, 0, to, s, t, linear, out);
        return;
    }
    f32 maxl = (f32)(d.levels - 1);
    if (to.maxlod < maxl) maxl = to.maxlod;
    if (lod < to.minlod) lod = to.minlod;
    if (lod > maxl) lod = maxl;
    bool mipLin = (filt == GX_NEAR_MIP_LIN || filt == GX_LIN_MIP_LIN);
    if (!mipLin) {
        sampleLevel(d, (int)(lod + 0.5f), to, s, t, linear, out);
        return;
    }
    int l0 = (int)floorf(lod);
    int l1 = std::min(l0 + 1, d.levels - 1);
    f32 f = lod - l0;
    f32 a[4], b[4];
    sampleLevel(d, l0, to, s, t, linear, a);
    sampleLevel(d, l1, to, s, t, linear, b);
    for (int k = 0; k < 4; k++) out[k] = a[k] + (b[k] - a[k]) * f;
}

}  // namespace

// ====================================================================
//                         API: general
// ====================================================================
static void initState() {
    S = new State();
    memset(S->vcd, 0, sizeof(S->vcd));
    memset(S->arr, 0, sizeof(S->arr));
    memset(S->stride, 0, sizeof(S->stride));
    memset(S->pos, 0, sizeof(S->pos));
    memset(S->nrm, 0, sizeof(S->nrm));
    memset(S->proj, 0, sizeof(S->proj));
    memset(&S->stats, 0, sizeof(S->stats));
    for (int i = 0; i < 8; i++) S->curTex[i] = GX_IDENTITY;
    // identity at row 60 like GX_Init
    S->pos[60][0] = 1; S->pos[61][1] = 1; S->pos[62][2] = 1;
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 4; k++) S->reg[i][k] = 0;
        S->kcol[i] = {0, 0, 0, 0};
        for (int k = 0; k < 4; k++) S->swap[i][k] = k;
    }
    S->amb[0] = S->amb[1] = {0, 0, 0, 0};
    S->mat[0] = S->mat[1] = {255, 255, 255, 255};
    memset(S->light, 0, sizeof(S->light));
    for (int i = 0; i < 8; i++) S->tmapValid[i] = false;
    S->efb.assign(EFB_W * EFB_H * 4, 0);
    S->zbuf.assign(EFB_W * EFB_H, 1.0f);
    S->frame.assign(640 * 480 * 3, 0);
    // GX_Init defaults that matter
    S->tev[0].ca = GX_CC_ZERO; S->tev[0].cb = GX_CC_ZERO; S->tev[0].cc = GX_CC_ZERO; S->tev[0].cd = GX_CC_RASC;
    S->inited = true;
}

extern "C" {

GXFifoObj *GX_Init(void *base, u32 size) {
    (void)base;
    if (size < GX_FIFO_MINSIZE) fprintf(stderr, "[gxemu] warning: FIFO smaller than GX_FIFO_MINSIZE\n");
    if (((uintptr_t)base) & 31) fprintf(stderr, "[gxemu] ERROR: FIFO base not 32-byte aligned\n");
    if (!S) initState();
    static GXFifoObj fifo;
    return &fifo;
}

void GX_Flush(void) { checkNotInPrim("GX_Flush"); }
void GX_DrawDone(void) { checkNotInPrim("GX_DrawDone"); }
void GX_SetDrawDone(void) { checkNotInPrim("GX_SetDrawDone"); }
void GX_WaitDrawDone(void) {}
void GX_InvVtxCache(void) { STATE_CALL("GX_InvVtxCache"); }
void GX_InvalidateTexAll(void) {
    STATE_CALL("GX_InvalidateTexAll");
    S->texCache.clear();
}
void GX_SetMisc(u32, u32) {}

void DCFlushRange(void *, u32) {}
void DCInvalidateRange(void *, u32) {}
void DCStoreRange(void *, u32) {}

// ====================================================================
//                         API: vertex setup
// ====================================================================
void GX_ClearVtxDesc(void) {
    STATE_CALL("GX_ClearVtxDesc");
    memset(S->vcd, 0, sizeof(S->vcd));
}

void GX_SetVtxDesc(u8 attr, u8 type) {
    STATE_CALL("GX_SetVtxDesc");
    if (attr >= GX_VA_MAXATTR) { gxerr("GX_SetVtxDesc bad attr %d", attr); return; }
    S->vcd[attr] = type;
}

void GX_SetVtxAttrFmt(u8 vtxfmt, u32 vtxattr, u32 comptype, u32 compsize, u32 frac) {
    STATE_CALL("GX_SetVtxAttrFmt");
    if (vtxfmt > 7 || vtxattr >= GX_VA_MAXATTR) { gxerr("GX_SetVtxAttrFmt bad args"); return; }
    VatAttr &v = S->vat[vtxfmt][vtxattr];
    v.cnt = (u8)comptype;
    v.type = (u8)compsize;
    v.frac = (u8)frac;
    if (vtxattr == GX_VA_NRM) {
        if (compsize == GX_S8) v.frac = 6;
        else if (compsize == GX_S16) v.frac = 14;
        else if (compsize == GX_U8 || compsize == GX_U16) gxerr("unsigned normals are not supported by GX");
    }
    if (frac > 31) gxerr("frac out of range");
}

void GX_SetArray(u32 attr, void *ptr, u8 stride) {
    STATE_CALL("GX_SetArray");
    if (attr >= GX_VA_MAXATTR) { gxerr("GX_SetArray bad attr"); return; }
    S->arr[attr] = (const u8 *)ptr;
    S->stride[attr] = stride;
    if (((uintptr_t)ptr) & 31) gxerr("GX_SetArray(%d): array pointer not 32-byte aligned", attr);
}

}  // extern "C"

// ====================================================================
//                  vertex decode + transform + raster
// ====================================================================
namespace {

struct RawVtx {
    u8 pnidx;
    u8 texidx[8];
    f32 p[3];
    f32 n[3];
    bool hasN;
    f32 c[2][4];
    bool hasC[2];
    f32 t[8][2];
    bool hasT[8];
};

f32 readComp(const u8 *&p, u8 type, u8 frac) {
    f32 v = 0;
    switch (type) {
        case GX_U8: v = (f32)p[0]; p += 1; break;
        case GX_S8: v = (f32)(s8)p[0]; p += 1; break;
        case GX_U16: v = (f32)be16(p); p += 2; break;
        case GX_S16: v = (f32)(s16)be16(p); p += 2; break;
        case GX_F32: return v = bef32(p), p += 4, v;
    }
    return v / (f32)(1 << frac);
}

void readColor(const u8 *p, u8 type, f32 out[4]) {
    int r = 255, g = 255, b = 255, a = 255;
    switch (type) {
        case GX_RGB565: { u16 v = be16(p); r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31; r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2); break; }
        case GX_RGB8: r = p[0]; g = p[1]; b = p[2]; break;
        case GX_RGBX8: r = p[0]; g = p[1]; b = p[2]; break;
        case GX_RGBA4: { u16 v = be16(p); r = ((v >> 12) & 15) * 17; g = ((v >> 8) & 15) * 17; b = ((v >> 4) & 15) * 17; a = (v & 15) * 17; break; }
        case GX_RGBA6: { u32 v = (p[0] << 16) | (p[1] << 8) | p[2]; r = ((v >> 18) & 63) * 255 / 63; g = ((v >> 12) & 63) * 255 / 63; b = ((v >> 6) & 63) * 255 / 63; a = (v & 63) * 255 / 63; break; }
        case GX_RGBA8: r = p[0]; g = p[1]; b = p[2]; a = p[3]; break;
    }
    out[0] = r / 255.0f; out[1] = g / 255.0f; out[2] = b / 255.0f; out[3] = a / 255.0f;
}

const u8 *attrSrc(const u8 *&p, u8 attr, u8 t) {
    if (t == GX_DIRECT) return p;
    u32 idx = (t == GX_INDEX8) ? p[0] : be16(p);
    p += (t == GX_INDEX8) ? 1 : 2;
    if (t == GX_INDEX8 && idx == 0xFF) return nullptr;     // index 0xff/0xffff = skip vertex
    if (t == GX_INDEX16 && idx == 0xFFFF) return nullptr;
    if (!S->arr[attr]) { gxerr("indexed attribute %d has no array set", attr); return nullptr; }
    return S->arr[attr] + (size_t)idx * S->stride[attr];
}

void decodeVertex(const u8 *&p, u8 fmt, RawVtx &v) {
    memset(&v, 0, sizeof(v));
    v.pnidx = (u8)S->curPos;
    for (int i = 0; i < 8; i++) v.texidx[i] = (u8)S->curTex[i];
    for (u8 a : kOrder) {
        u8 t = S->vcd[a];
        if (t == GX_NONE) continue;
        if (a == GX_VA_PTNMTXIDX) { v.pnidx = p[0] & 63; p++; continue; }
        if (a <= GX_VA_TEX7MTXIDX) { v.texidx[a - GX_VA_TEX0MTXIDX] = p[0] & 63; p++; continue; }
        const VatAttr &f = S->vat[fmt][a];
        const u8 *src = attrSrc(p, a, t);
        const u8 *q = src;
        int dsz = attrDataSize(fmt, a);
        if (a == GX_VA_POS) {
            if (q) {
                v.p[0] = readComp(q, f.type, f.frac);
                v.p[1] = readComp(q, f.type, f.frac);
                v.p[2] = (f.cnt == GX_POS_XYZ) ? readComp(q, f.type, f.frac) : 0.0f;
            }
        } else if (a == GX_VA_NRM) {
            if (q) {
                v.n[0] = readComp(q, f.type, f.frac);
                v.n[1] = readComp(q, f.type, f.frac);
                v.n[2] = readComp(q, f.type, f.frac);
                v.hasN = true;
            }
        } else if (a == GX_VA_CLR0 || a == GX_VA_CLR1) {
            int ci = a - GX_VA_CLR0;
            if (q) readColor(q, f.type, v.c[ci]);
            v.hasC[ci] = true;
        } else {
            int ti = a - GX_VA_TEX0;
            if (q) {
                v.t[ti][0] = readComp(q, f.type, f.frac);
                v.t[ti][1] = (f.cnt == GX_TEX_ST) ? readComp(q, f.type, f.frac) : 0.0f;
            }
            v.hasT[ti] = true;
        }
        if (t == GX_DIRECT) p += dsz;
    }
}

void lightChannel(int c, const f32 P[3], const f32 N[3], const RawVtx &rv, f32 out[4]) {
    // colour part (chan c) and alpha part (chan c+2)
    for (int part = 0; part < 2; part++) {
        const ChanCtrl &cc = S->chan[c + part * 2];
        f32 mat[4], amb[4];
        if (cc.matsrc == GX_SRC_VTX) {
            if (!rv.hasC[c]) gxerr("channel %d uses vertex material colour but CLR%d is not in the vertex descriptor", c, c);
            memcpy(mat, rv.c[c], sizeof(mat));
        } else {
            GXColor m = S->mat[c];
            mat[0] = m.r / 255.f; mat[1] = m.g / 255.f; mat[2] = m.b / 255.f; mat[3] = m.a / 255.f;
        }
        f32 res[4];
        if (!cc.enable) {
            memcpy(res, mat, sizeof(res));
        } else {
            if (cc.ambsrc == GX_SRC_VTX) {
                if (!rv.hasC[c]) gxerr("channel %d uses vertex ambient but CLR%d missing", c, c);
                memcpy(amb, rv.c[c], sizeof(amb));
            } else {
                GXColor a = S->amb[c];
                amb[0] = a.r / 255.f; amb[1] = a.g / 255.f; amb[2] = a.b / 255.f; amb[3] = a.a / 255.f;
            }
            if (!rv.hasN && cc.litmask) gxerr("lighting enabled but vertex has no normal");
            f32 lit[4] = {amb[0], amb[1], amb[2], amb[3]};
            for (int i = 0; i < 8; i++) {
                if (!(cc.litmask & (1 << i))) continue;
                const GXLightObj &L = S->light[i];
                f32 ld[3] = {L.pos[0] - P[0], L.pos[1] - P[1], L.pos[2] - P[2]};
                f32 dist2 = ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2];
                f32 dist = sqrtf(dist2);
                if (dist > 0) { ld[0] /= dist; ld[1] /= dist; ld[2] /= dist; }
                f32 ndl = N[0] * ld[0] + N[1] * ld[1] + N[2] * ld[2];
                f32 diff = 1.0f;
                if (cc.attnfn == GX_AF_SPEC) {
                    diff = 1.0f;  // spec forces DF_NONE
                } else if (cc.diffn == GX_DF_SIGNED) {
                    diff = ndl;
                } else if (cc.diffn == GX_DF_CLAMP) {
                    diff = ndl > 0 ? ndl : 0;
                }
                f32 attn = 1.0f;
                if (cc.attnfn == GX_AF_SPOT) {
                    f32 cosang = -(ld[0] * L.dir[0] + ld[1] * L.dir[1] + ld[2] * L.dir[2]);
                    f32 aa = L.a[0] + L.a[1] * cosang + L.a[2] * cosang * cosang;
                    f32 kk = L.k[0] + L.k[1] * dist + L.k[2] * dist2;
                    if (aa < 0) aa = 0;
                    attn = kk > 0 ? aa / kk : 0;
                } else if (cc.attnfn == GX_AF_SPEC) {
                    f32 ndh = N[0] * L.dir[0] + N[1] * L.dir[1] + N[2] * L.dir[2];
                    f32 cosv = ndl > 0 ? (ndh > 0 ? ndh : 0) : 0;
                    f32 aa = L.a[0] + L.a[1] * cosv + L.a[2] * cosv * cosv;
                    f32 kk = L.k[0] + L.k[1] * cosv + L.k[2] * cosv * cosv;
                    attn = kk > 0 ? aa / kk : 0;
                    if (attn < 0) attn = 0;
                }
                f32 lc[4] = {L.color.r / 255.f, L.color.g / 255.f, L.color.b / 255.f, L.color.a / 255.f};
                for (int k = 0; k < 4; k++) lit[k] += lc[k] * diff * attn;
            }
            for (int k = 0; k < 4; k++) res[k] = mat[k] * clamp01(lit[k]);
        }
        if (part == 0) { out[0] = res[0]; out[1] = res[1]; out[2] = res[2]; }
        else out[3] = res[3];
    }
    for (int k = 0; k < 4; k++) out[k] = clamp01(out[k]);
}

void transformVertex(const RawVtx &rv, Vtx &o) {
    const f32 *m0 = S->pos[rv.pnidx], *m1 = S->pos[rv.pnidx + 1], *m2 = S->pos[rv.pnidx + 2];
    f32 v4[4] = {rv.p[0], rv.p[1], rv.p[2], 1.0f};
    f32 P[3];
    mtxRow(m0, v4, P[0]);
    mtxRow(m1, v4, P[1]);
    mtxRow(m2, v4, P[2]);
    f32 N[3] = {0, 0, 1};
    if (rv.hasN) {
        const f32 *n = S->nrm[(rv.pnidx / 3) * 3];
        const f32 *n1 = S->nrm[(rv.pnidx / 3) * 3 + 1];
        const f32 *n2 = S->nrm[(rv.pnidx / 3) * 3 + 2];
        N[0] = n[0] * rv.n[0] + n[1] * rv.n[1] + n[2] * rv.n[2];
        N[1] = n1[0] * rv.n[0] + n1[1] * rv.n[1] + n1[2] * rv.n[2];
        N[2] = n2[0] * rv.n[0] + n2[1] * rv.n[1] + n2[2] * rv.n[2];
        f32 l = sqrtf(N[0] * N[0] + N[1] * N[1] + N[2] * N[2]);
        if (l > 0) { N[0] /= l; N[1] /= l; N[2] /= l; }
    }
    // projection (GX uses only 6 parameters)
    const f32 *p = S->proj;
    if (S->projType == GX_PERSPECTIVE) {
        o.cx = p[0] * P[0] + p[1] * P[2];
        o.cy = p[2] * P[1] + p[3] * P[2];
        o.cz = p[4] * P[2] + p[5];
        o.cw = -P[2];
    } else {
        o.cx = p[0] * P[0] + p[1];
        o.cy = p[2] * P[1] + p[3];
        o.cz = p[4] * P[2] + p[5];
        o.cw = 1.0f;
    }
    // lighting
    for (int c = 0; c < 2; c++) {
        if (c < S->numChans) lightChannel(c, P, N, rv, o.col[c]);
        else o.col[c][0] = o.col[c][1] = o.col[c][2] = o.col[c][3] = 0;
    }
    // texgen
    for (u32 i = 0; i < S->numTexGens; i++) {
        const TexGen &g = S->tg[i];
        f32 src[4] = {0, 0, 1, 1};
        if (g.type == GX_TG_SRTG) {
            int ch = (g.src == GX_TG_COLOR1) ? 1 : 0;
            if (g.src != GX_TG_COLOR0 && g.src != GX_TG_COLOR1) gxerr("SRTG texgen needs GX_TG_COLOR0/1 source");
            if (ch >= S->numChans) gxerr("SRTG texgen reads colour channel %d but GX_SetNumChans(%d)", ch, S->numChans);
            o.tex[i][0] = o.col[ch][0];
            o.tex[i][1] = o.col[ch][1];
            o.tex[i][2] = 1.0f;
            continue;
        }
        if (g.type != GX_TG_MTX2x4 && g.type != GX_TG_MTX3x4) { gxerr("unsupported texgen type %u", g.type); continue; }
        switch (g.src) {
            case GX_TG_POS: src[0] = rv.p[0]; src[1] = rv.p[1]; src[2] = rv.p[2]; src[3] = 1; break;
            case GX_TG_NRM:
                src[0] = rv.n[0]; src[1] = rv.n[1]; src[2] = rv.n[2]; src[3] = 1;
                if (!rv.hasN) gxerr("texgen from normal but no normal in vertex");
                break;
            default:
                if (g.src >= GX_TG_TEX0 && g.src <= GX_TG_TEX7) {
                    int ti = g.src - GX_TG_TEX0;
                    if (!rv.hasT[ti]) gxerr("texgen %u reads TEX%d which is not in the vertex descriptor", i, ti);
                    src[0] = rv.t[ti][0]; src[1] = rv.t[ti][1]; src[2] = 1; src[3] = 1;
                } else {
                    gxerr("unsupported texgen source %u", g.src);
                }
        }
        u32 mi = g.mtx;
        if (S->vcd[GX_VA_TEX0MTXIDX + i] != GX_NONE) mi = rv.texidx[i];
        f32 r[3];
        if (mi == GX_IDENTITY) {
            r[0] = src[0]; r[1] = src[1]; r[2] = (g.type == GX_TG_MTX3x4) ? src[2] : 1.0f;
            if (g.src >= GX_TG_TEX0 && g.src <= GX_TG_TEX7) r[2] = 1.0f;
        } else {
            mtxRow(S->pos[mi], src, r[0]);
            mtxRow(S->pos[mi + 1], src, r[1]);
            if (g.type == GX_TG_MTX3x4) mtxRow(S->pos[mi + 2], src, r[2]);
            else r[2] = 1.0f;
        }
        if (g.postmtx != 125) gxerr("post-transform texture matrices are not supported");
        o.tex[i][0] = r[0];
        o.tex[i][1] = r[1];
        o.tex[i][2] = r[2];
    }
}

// --------------------------------------------------------------- raster
struct Edge {
    f32 a, b, c;
};

inline f32 tevBiasVal(u8 bias) { return bias == GX_TB_ADDHALF ? 128.f : (bias == GX_TB_SUBHALF ? -128.f : 0.f); }
inline f32 tevScaleVal(u8 s) { return s == GX_CS_SCALE_2 ? 2.f : (s == GX_CS_SCALE_4 ? 4.f : (s == GX_CS_DIVIDE_2 ? 0.5f : 1.f)); }

f32 kcSel(u8 sel, int comp) {
    if (sel <= GX_TEV_KCSEL_1_8) {
        static const f32 frac[8] = {255, 223, 191, 159, 127, 95, 63, 31};
        return frac[sel];
    }
    if (sel >= GX_TEV_KCSEL_K0 && sel <= GX_TEV_KCSEL_K3) {
        const GXColor &k = S->kcol[sel - GX_TEV_KCSEL_K0];
        return comp == 0 ? k.r : comp == 1 ? k.g : comp == 2 ? k.b : k.a;
    }
    if (sel >= GX_TEV_KCSEL_K0_R && sel <= GX_TEV_KCSEL_K3_A) {
        int idx = (sel - GX_TEV_KCSEL_K0_R) & 3;
        int ch = (sel - GX_TEV_KCSEL_K0_R) >> 2;
        const GXColor &k = S->kcol[idx];
        return ch == 0 ? k.r : ch == 1 ? k.g : ch == 2 ? k.b : k.a;
    }
    gxerr("bad konst selection %d", sel);
    return 0;
}

struct PixelIn {
    f32 col[2][4];      // 0..255
    f32 tex[8][2];
    f32 lod[8];
};

// Runs the TEV for one pixel. Returns colour (0..255 floats) in out.
void runTev(const PixelIn &in, f32 out[4]) {
    f32 regs[4][4];
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++) regs[r][k] = S->reg[r][k];
    for (int s = 0; s < S->numTev; s++) {
        const TevStage &t = S->tev[s];
        f32 tex[4] = {0, 0, 0, 0}, ras[4] = {0, 0, 0, 0};
        if (t.texmap != GX_TEXMAP_NULL && (t.texmap & 0xFF) < 8 && t.texcoord < 8) {
            const f32 *tc = in.tex[t.texcoord];
            f32 raw[4];
            sampleTex(t.texmap & 7, tc[0], tc[1], in.lod[t.texcoord], raw);
            const u8 *sw = S->swap[t.texswap];
            for (int k = 0; k < 4; k++) tex[k] = raw[sw[k]];
        }
        if (t.color == GX_COLOR0A0 || t.color == GX_COLOR1A1) {
            const f32 *c = in.col[t.color - GX_COLOR0A0];
            const u8 *sw = S->swap[t.rasswap];
            for (int k = 0; k < 4; k++) ras[k] = c[sw[k]];
        } else if (t.color == GX_COLOR0 || t.color == GX_COLOR1) {
            const f32 *c = in.col[t.color];
            ras[0] = c[0]; ras[1] = c[1]; ras[2] = c[2]; ras[3] = c[3];
        } else if (t.color == GX_ALPHA0 || t.color == GX_ALPHA1) {
            const f32 *c = in.col[t.color - GX_ALPHA0];
            ras[0] = ras[1] = ras[2] = ras[3] = c[3];
        }
        auto cin = [&](u8 sel, int k) -> f32 {
            switch (sel) {
                case GX_CC_CPREV: return regs[0][k];
                case GX_CC_APREV: return regs[0][3];
                case GX_CC_C0: return regs[1][k];
                case GX_CC_A0: return regs[1][3];
                case GX_CC_C1: return regs[2][k];
                case GX_CC_A1: return regs[2][3];
                case GX_CC_C2: return regs[3][k];
                case GX_CC_A2: return regs[3][3];
                case GX_CC_TEXC: return tex[k];
                case GX_CC_TEXA: return tex[3];
                case GX_CC_RASC: return ras[k];
                case GX_CC_RASA: return ras[3];
                case GX_CC_ONE: return 255.f;
                case GX_CC_HALF: return 128.f;
                case GX_CC_KONST: return kcSel(t.ksel, k);
                case GX_CC_ZERO: return 0.f;
            }
            return 0.f;
        };
        auto ain = [&](u8 sel) -> f32 {
            switch (sel) {
                case GX_CA_APREV: return regs[0][3];
                case GX_CA_A0: return regs[1][3];
                case GX_CA_A1: return regs[2][3];
                case GX_CA_A2: return regs[3][3];
                case GX_CA_TEXA: return tex[3];
                case GX_CA_RASA: return ras[3];
                case GX_CA_KONST: return kcSel(t.kasel, 3);
                case GX_CA_ZERO: return 0.f;
            }
            return 0.f;
        };
        f32 res[4];
        if (t.cop > GX_TEV_SUB) gxerr("TEV compare ops not supported by gxemu");
        for (int k = 0; k < 3; k++) {
            f32 a = cin(t.ca, k), b = cin(t.cb, k), c = cin(t.cc, k), d = cin(t.cd, k);
            // a,b,c are 8-bit inputs
            a = fminf(fmaxf(a, 0), 255); b = fminf(fmaxf(b, 0), 255); c = fminf(fmaxf(c, 0), 255);
            f32 cc = c + (c >= 128 ? 1 : 0);
            f32 lerp = (a * (256 - cc) + b * cc) / 256.f;
            f32 v = (t.cop == GX_TEV_SUB) ? d - lerp : d + lerp;
            v = (v + tevBiasVal(t.cbias)) * tevScaleVal(t.cscale);
            res[k] = t.cclamp ? fminf(fmaxf(v, 0), 255) : fminf(fmaxf(v, -1024), 1023);
        }
        {
            f32 a = ain(t.aa), b = ain(t.ab), c = ain(t.ac), d = ain(t.ad);
            a = fminf(fmaxf(a, 0), 255); b = fminf(fmaxf(b, 0), 255); c = fminf(fmaxf(c, 0), 255);
            f32 cc = c + (c >= 128 ? 1 : 0);
            f32 lerp = (a * (256 - cc) + b * cc) / 256.f;
            f32 v = (t.aop == GX_TEV_SUB) ? d - lerp : d + lerp;
            v = (v + tevBiasVal(t.abias)) * tevScaleVal(t.ascale);
            res[3] = t.aclamp ? fminf(fmaxf(v, 0), 255) : fminf(fmaxf(v, -1024), 1023);
        }
        for (int k = 0; k < 3; k++) regs[t.creg][k] = res[k];
        regs[t.areg][3] = res[3];
        if (s == S->numTev - 1) {
            if (t.creg != GX_TEVPREV || t.areg != GX_TEVPREV) gxerr("last TEV stage must output to GX_TEVPREV");
        }
    }
    for (int k = 0; k < 4; k++) out[k] = fminf(fmaxf(regs[0][k], 0), 255);
}

bool alphaCmp(u8 comp, f32 a, u8 ref) {
    int A = (int)(a + 0.5f);
    switch (comp) {
        case GX_NEVER: return false;
        case GX_LESS: return A < ref;
        case GX_EQUAL: return A == ref;
        case GX_LEQUAL: return A <= ref;
        case GX_GREATER: return A > ref;
        case GX_NEQUAL: return A != ref;
        case GX_GEQUAL: return A >= ref;
        default: return true;
    }
}

bool zTest(f32 z, f32 zb) {
    switch (S->zFunc) {
        case GX_NEVER: return false;
        case GX_LESS: return z < zb;
        case GX_EQUAL: return fabsf(z - zb) < 1e-7f;
        case GX_LEQUAL: return z <= zb;
        case GX_GREATER: return z > zb;
        case GX_NEQUAL: return z != zb;
        case GX_GEQUAL: return z >= zb;
        default: return true;
    }
}

f32 blendFactor(u8 f, bool isSrc, const f32 src[4], const u8 *dst, int k) {
    switch (f) {
        case GX_BL_ZERO: return 0;
        case GX_BL_ONE: return 1;
        case GX_BL_SRCCLR:  // as source factor this means DSTCLR
            return isSrc ? dst[k] / 255.f : src[k] / 255.f;
        case GX_BL_INVSRCCLR:
            return isSrc ? 1 - dst[k] / 255.f : 1 - src[k] / 255.f;
        case GX_BL_SRCALPHA: return src[3] / 255.f;
        case GX_BL_INVSRCALPHA: return 1 - src[3] / 255.f;
        case GX_BL_DSTALPHA: return dst[3] / 255.f;
        case GX_BL_INVDSTALPHA: return 1 - dst[3] / 255.f;
    }
    return 0;
}

void shadePixel(int x, int y, f32 z, f32 eyeDepth, const PixelIn &in) {
    S->stats.pixels++;
    size_t idx = (size_t)y * EFB_W + x;
    bool alphaTestActive = !(S->acComp0 == GX_ALWAYS && S->acComp1 == GX_ALWAYS && (S->acOp == GX_AOP_AND || S->acOp == GX_AOP_OR));
    if (S->zEnable && S->zBefore) {
        if (!zTest(z, S->zbuf[idx])) return;
        if (S->zUpdate) S->zbuf[idx] = z;  // early z writes even if alpha test later fails (like HW)
    }
    f32 c[4];
    runTev(in, c);
    bool a0 = alphaCmp(S->acComp0, c[3], S->acRef0), a1 = alphaCmp(S->acComp1, c[3], S->acRef1);
    bool pass;
    switch (S->acOp) {
        case GX_AOP_AND: pass = a0 && a1; break;
        case GX_AOP_OR: pass = a0 || a1; break;
        case GX_AOP_XOR: pass = a0 != a1; break;
        default: pass = a0 == a1; break;
    }
    if (!pass) return;
    if (S->zEnable && !S->zBefore) {
        if (!zTest(z, S->zbuf[idx])) return;
        if (S->zUpdate) S->zbuf[idx] = z;
    }
    (void)alphaTestActive;
    // fog
    if (S->fogType != GX_FOG_NONE) {
        f32 d = eyeDepth;
        f32 f = (S->fogEnd > S->fogStart) ? (d - S->fogStart) / (S->fogEnd - S->fogStart) : 0;
        f = clamp01(f);
        switch (S->fogType & 7) {
            case 2: break;
            case 4: f = 1.0f - exp2f(-8.0f * f); break;
            case 5: f = 1.0f - exp2f(-8.0f * f * f); break;
            case 6: f = exp2f(-8.0f * (1.0f - f)); break;
            case 7: f = exp2f(-8.0f * (1.0f - f) * (1.0f - f)); break;
        }
        c[0] = c[0] + (S->fogColor.r - c[0]) * f;
        c[1] = c[1] + (S->fogColor.g - c[1]) * f;
        c[2] = c[2] + (S->fogColor.b - c[2]) * f;
    }
    u8 *dst = &S->efb[idx * 4];
    f32 o[4] = {c[0], c[1], c[2], c[3]};
    if (S->bmType == GX_BM_BLEND) {
        for (int k = 0; k < 4; k++) {
            f32 sf = blendFactor(S->bmSrc, true, c, dst, k);
            f32 df = blendFactor(S->bmDst, false, c, dst, k);
            o[k] = c[k] * sf + dst[k] * df;
        }
    } else if (S->bmType == GX_BM_SUBTRACT) {
        for (int k = 0; k < 4; k++) o[k] = dst[k] - c[k];
    } else if (S->bmType == GX_BM_LOGIC) {
        for (int k = 0; k < 4; k++) {
            int sv = (int)c[k], dv = dst[k], r = sv;
            switch (S->bmOp) {
                case GX_LO_CLEAR: r = 0; break;
                case GX_LO_COPY: r = sv; break;
                case GX_LO_XOR: r = sv ^ dv; break;
                case GX_LO_OR: r = sv | dv; break;
                case GX_LO_AND: r = sv & dv; break;
                case GX_LO_NOOP: r = dv; break;
                case GX_LO_INV: r = ~dv & 255; break;
                case GX_LO_INVCOPY: r = ~sv & 255; break;
                case GX_LO_SET: r = 255; break;
                default: r = sv; break;
            }
            o[k] = (f32)r;
        }
    }
    if (S->colorUpdate) {
        for (int k = 0; k < 3; k++) {
            int v = (int)(fminf(fmaxf(o[k], 0), 255) + 0.5f);
            if (S->pixFmt == GX_PF_RGBA6_Z24) v = (v & 0xFC) | (v >> 6);
            else if (S->pixFmt == GX_PF_RGB565_Z16) v = (k == 1) ? ((v & 0xFC) | (v >> 6)) : ((v & 0xF8) | (v >> 5));
            dst[k] = (u8)v;
        }
    }
    if (S->alphaUpdate) {
        if (S->pixFmt == GX_PF_RGBA6_Z24) {
            int v = S->dstAlphaEn ? S->dstAlpha : (int)(fminf(fmaxf(o[3], 0), 255) + 0.5f);
            dst[3] = (u8)((v & 0xFC) | (v >> 6));
        } else {
            dst[3] = 255;
        }
    }
}

struct RasterTri {
    Vtx v[3];
};

void rasterTriangle(const Vtx &A, const Vtx &B, const Vtx &C) {
    f32 area = (B.sx - A.sx) * (C.sy - A.sy) - (C.sx - A.sx) * (B.sy - A.sy);
    if (area == 0) return;
    S->stats.tris_drawn++;
    if (!S->raster) return;
    int minx = (int)floorf(std::min(A.sx, std::min(B.sx, C.sx)));
    int maxx = (int)ceilf(std::max(A.sx, std::max(B.sx, C.sx)));
    int miny = (int)floorf(std::min(A.sy, std::min(B.sy, C.sy)));
    int maxy = (int)ceilf(std::max(A.sy, std::max(B.sy, C.sy)));
    minx = std::max(minx, S->scX0);
    miny = std::max(miny, S->scY0);
    maxx = std::min(maxx, S->scX1 - 1);
    maxy = std::min(maxy, S->scY1 - 1);
    maxx = std::min(maxx, EFB_W - 1);
    maxy = std::min(maxy, EFB_H - 1);
    if (minx > maxx || miny > maxy) return;
    f32 inv = 1.0f / area;
    int ntex = (int)S->numTexGens;
    // texture LOD per texcoord from screen-space derivatives (per triangle, at centroid)
    PixelIn pin;
    auto bary = [&](f32 px, f32 py, f32 &w0, f32 &w1, f32 &w2) {
        w0 = ((B.sx - px) * (C.sy - py) - (C.sx - px) * (B.sy - py)) * inv;
        w1 = ((C.sx - px) * (A.sy - py) - (A.sx - px) * (C.sy - py)) * inv;
        w2 = 1.0f - w0 - w1;
    };
    auto texAt = [&](f32 px, f32 py, int i, f32 &s, f32 &t) {
        f32 w0, w1, w2;
        bary(px, py, w0, w1, w2);
        f32 iw = w0 * A.invw + w1 * B.invw + w2 * C.invw;
        f32 q = (w0 * A.tex[i][2] * A.invw + w1 * B.tex[i][2] * B.invw + w2 * C.tex[i][2] * C.invw) / iw;
        s = (w0 * A.tex[i][0] * A.invw + w1 * B.tex[i][0] * B.invw + w2 * C.tex[i][0] * C.invw) / iw;
        t = (w0 * A.tex[i][1] * A.invw + w1 * B.tex[i][1] * B.invw + w2 * C.tex[i][1] * C.invw) / iw;
        if (q != 0 && q != 1.0f) { s /= q; t /= q; }
    };
    // find which texmaps each texcoord samples (for LOD sizes)
    int tcSize[8] = {0};
    for (int s = 0; s < S->numTev; s++) {
        const TevStage &t = S->tev[s];
        if (t.texcoord < 8 && t.texmap != GX_TEXMAP_NULL && (t.texmap & 0xFF) < 8 && S->tmapValid[t.texmap & 7])
            tcSize[t.texcoord] = std::max<int>(S->tmap[t.texmap & 7].width, S->tmap[t.texmap & 7].height);
    }
    for (int y = miny; y <= maxy; y++) {
        f32 py = y + 0.5f;
        for (int x = minx; x <= maxx; x++) {
            f32 px = x + 0.5f;
            f32 w0, w1, w2;
            bary(px, py, w0, w1, w2);
            if (w0 < 0 || w1 < 0 || w2 < 0) {
                // top-left-ish rule: allow tiny negative due to fp
                if (w0 < -1e-6f || w1 < -1e-6f || w2 < -1e-6f) continue;
            }
            f32 z = w0 * A.sz + w1 * B.sz + w2 * C.sz;
            if (z < 0) z = 0;
            if (z > 1) z = 1;
            f32 iw = w0 * A.invw + w1 * B.invw + w2 * C.invw;
            f32 pw0 = w0 * A.invw / iw, pw1 = w1 * B.invw / iw, pw2 = w2 * C.invw / iw;
            for (int c = 0; c < 2; c++)
                for (int k = 0; k < 4; k++)
                    pin.col[c][k] = (pw0 * A.col[c][k] + pw1 * B.col[c][k] + pw2 * C.col[c][k]) * 255.f;
            for (int i = 0; i < ntex; i++) {
                f32 s, t;
                texAt(px, py, i, s, t);
                pin.tex[i][0] = s;
                pin.tex[i][1] = t;
                if (tcSize[i]) {
                    f32 s1, t1, s2, t2;
                    texAt(px + 1, py, i, s1, t1);
                    texAt(px, py + 1, i, s2, t2);
                    f32 dx = sqrtf((s1 - s) * (s1 - s) + (t1 - t) * (t1 - t)) * tcSize[i];
                    f32 dy = sqrtf((s2 - s) * (s2 - s) + (t2 - t) * (t2 - t)) * tcSize[i];
                    f32 d = std::max(dx, dy);
                    pin.lod[i] = d > 0 ? log2f(d) : -10.f;
                } else {
                    pin.lod[i] = 0;
                }
            }
            f32 eye = (S->projType == GX_PERSPECTIVE) ? 1.0f / iw : z;
            shadePixel(x, y, z, eye, pin);
        }
    }
}

void finishVertexScreen(Vtx &v) {
    f32 iw = 1.0f / v.cw;
    f32 nx = v.cx * iw, ny = v.cy * iw, nz = v.cz * iw;
    v.sx = S->vpX + S->vpW * 0.5f + nx * S->vpW * 0.5f;
    v.sy = S->vpY + S->vpH * 0.5f - ny * S->vpH * 0.5f;
    // GX maps z_ndc in [-1,0] to [near, far]
    v.sz = S->vpF + nz * (S->vpF - S->vpN);
    v.invw = iw;
}

Vtx lerpVtx(const Vtx &a, const Vtx &b, f32 t) {
    Vtx o;
    o.cx = a.cx + (b.cx - a.cx) * t;
    o.cy = a.cy + (b.cy - a.cy) * t;
    o.cz = a.cz + (b.cz - a.cz) * t;
    o.cw = a.cw + (b.cw - a.cw) * t;
    for (int c = 0; c < 2; c++)
        for (int k = 0; k < 4; k++) o.col[c][k] = a.col[c][k] + (b.col[c][k] - a.col[c][k]) * t;
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 3; k++) o.tex[i][k] = a.tex[i][k] + (b.tex[i][k] - a.tex[i][k]) * t;
    return o;
}

void clipAndDraw(const Vtx &a, const Vtx &b, const Vtx &c) {
    S->stats.tris_in++;
    // cull in screen space using unclipped verts when all are in front
    std::vector<Vtx> poly = {a, b, c}, out;
    // planes: near z >= -w, far z <= 0  (GX NDC z in [-1, 0])
    for (int plane = 0; plane < 3; plane++) {
        out.clear();
        for (size_t i = 0; i < poly.size(); i++) {
            const Vtx &p = poly[i], &q = poly[(i + 1) % poly.size()];
            f32 dp, dq;
            if (plane == 0) { dp = p.cz + p.cw; dq = q.cz + q.cw; }
            else if (plane == 1) { dp = -p.cz; dq = -q.cz; }
            else { dp = p.cw - 1e-5f; dq = q.cw - 1e-5f; }
            bool pin = dp >= 0, qin = dq >= 0;
            if (pin) out.push_back(p);
            if (pin != qin) out.push_back(lerpVtx(p, q, dp / (dp - dq)));
        }
        poly.swap(out);
        if (poly.size() < 3) return;
    }
    for (auto &v : poly) finishVertexScreen(v);
    // face culling based on the first triangle of the clipped polygon
    f32 area = (poly[1].sx - poly[0].sx) * (poly[2].sy - poly[0].sy) - (poly[2].sx - poly[0].sx) * (poly[1].sy - poly[0].sy);
    // screen y grows downwards: area > 0 means clockwise as seen by the viewer = front facing
    bool front = area > 0;
    if (S->cull == GX_CULL_ALL) return;
    if (S->cull == GX_CULL_BACK && !front) return;
    if (S->cull == GX_CULL_FRONT && front) return;
    for (size_t i = 1; i + 1 < poly.size(); i++) rasterTriangle(poly[0], poly[i], poly[i + 1]);
}

void drawPoint(const Vtx &v, f32 size) {
    if (v.cw <= 0) return;
    Vtx q = v;
    finishVertexScreen(q);
    int r = std::max(1, (int)(size * 0.5f));
    PixelIn pin;
    for (int c = 0; c < 2; c++)
        for (int k = 0; k < 4; k++) pin.col[c][k] = q.col[c][k] * 255.f;
    for (int i = 0; i < 8; i++) { pin.tex[i][0] = q.tex[i][0]; pin.tex[i][1] = q.tex[i][1]; pin.lod[i] = 0; }
    if (!S->raster) return;
    for (int y = (int)q.sy - r; y < (int)q.sy + r; y++)
        for (int x = (int)q.sx - r; x < (int)q.sx + r; x++)
            if (x >= S->scX0 && x < S->scX1 && y >= S->scY0 && y < S->scY1 && x < EFB_W && y < EFB_H)
                shadePixel(x, y, q.sz, 1.0f / q.invw, pin);
}

f32 g_pointSize = 1.0f;

void drawPrimitive(u8 prim, u8 fmt, u16 count, const u8 *data) {
    S->stats.prims++;
    S->stats.verts += count;
    std::vector<Vtx> vs(count);
    const u8 *p = data;
    for (u16 i = 0; i < count; i++) {
        RawVtx rv;
        decodeVertex(p, fmt, rv);
        transformVertex(rv, vs[i]);
    }
    switch (prim) {
        case GX_TRIANGLES:
            if (count % 3) gxerr("GX_TRIANGLES with %u vertices", count);
            for (u16 i = 0; i + 2 < count; i += 3) clipAndDraw(vs[i], vs[i + 1], vs[i + 2]);
            break;
        case GX_TRIANGLESTRIP:
            for (u16 i = 0; i + 2 < count; i++) {
                if (i & 1) clipAndDraw(vs[i + 1], vs[i], vs[i + 2]);
                else clipAndDraw(vs[i], vs[i + 1], vs[i + 2]);
            }
            break;
        case GX_TRIANGLEFAN:
            for (u16 i = 1; i + 1 < count; i++) clipAndDraw(vs[0], vs[i], vs[i + 1]);
            break;
        case GX_QUADS:
            if (count % 4) gxerr("GX_QUADS with %u vertices", count);
            for (u16 i = 0; i + 3 < count; i += 4) {
                clipAndDraw(vs[i], vs[i + 1], vs[i + 2]);
                clipAndDraw(vs[i], vs[i + 2], vs[i + 3]);
            }
            break;
        case GX_POINTS:
            for (u16 i = 0; i < count; i++) drawPoint(vs[i], g_pointSize);
            break;
        case GX_LINES:
        case GX_LINESTRIP:
            // rendered as points along the line (approximate)
            for (u16 i = 0; i + 1 < count; i += (prim == GX_LINES ? 2 : 1)) {
                for (int k = 0; k <= 16; k++) drawPoint(lerpVtx(vs[i], vs[i + 1], k / 16.0f), 2.0f);
            }
            break;
        default:
            gxerr("unknown primitive 0x%02x", prim);
    }
}

// immediate mode write handling
void pushBytes(u8 attrKind, const u8 *bytes, u32 n) {
    if (!S->inPrim) {
        gxerr("vertex data written outside GX_Begin");
        return;
    }
    // validate against expected layout
    if (S->layoutPos >= S->vtxLayout.size()) S->layoutPos = 0;
    auto exp = S->vtxLayout[S->layoutPos];
    bool kindOk;
    if (attrKind == 255) kindOk = (exp.first <= GX_VA_TEX7MTXIDX);
    else if (attrKind == GX_VA_TEX0) kindOk = (exp.first >= GX_VA_TEX0 && exp.first <= GX_VA_TEX7);
    else if (attrKind == GX_VA_CLR0) kindOk = (exp.first == GX_VA_CLR0 || exp.first == GX_VA_CLR1);
    else kindOk = (exp.first == attrKind);
    if (!kindOk || exp.second != n) {
        gxerr("vertex write mismatch: expected attr %d (%d bytes), got kind %d (%u bytes)", exp.first, exp.second, attrKind, n);
    }
    S->layoutPos++;
    if (S->recording) {
        if (S->dlLen + n > S->dlCap) { S->dlOverflow = true; }
        else { memcpy(S->dl + S->dlLen, bytes, n); S->dlLen += n; }
    } else {
        S->primData.insert(S->primData.end(), bytes, bytes + n);
    }
    S->primBytes += n;
    if (S->primBytes == (u32)S->primCount * S->primVtxSize) {
        S->inPrim = false;
        if (!S->recording) drawPrimitive(S->primType, S->primFmt, S->primCount, S->primData.data());
    } else if (S->primBytes > (u32)S->primCount * S->primVtxSize) {
        gxerr("too much vertex data for GX_Begin count");
    }
}

inline void w16(u8 *p, u16 v) { p[0] = v >> 8; p[1] = v & 255; }
inline void wf(u8 *p, f32 f) { u32 v; memcpy(&v, &f, 4); p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

}  // namespace

extern "C" {

void GX_Begin(u8 primitive, u8 vtxfmt, u16 vtxcnt) {
    if (S->inPrim) gxerr("GX_Begin while previous primitive incomplete (%u of %u bytes)", S->primBytes, S->primCount * S->primVtxSize);
    if (vtxfmt > 7) gxerr("bad vtxfmt");
    S->inPrim = true;
    S->primType = primitive;
    S->primFmt = vtxfmt;
    S->primCount = vtxcnt;
    S->primBytes = 0;
    S->primData.clear();
    S->layoutPos = 0;
    buildLayout(vtxfmt, S->vtxLayout, S->primVtxSize);
    if (S->recording) {
        if (S->dlLen + 3 > S->dlCap) S->dlOverflow = true;
        else {
            S->dl[S->dlLen++] = primitive | (vtxfmt & 7);
            S->dl[S->dlLen++] = vtxcnt >> 8;
            S->dl[S->dlLen++] = vtxcnt & 255;
        }
    }
    if (vtxcnt == 0) { S->inPrim = false; gxerr("GX_Begin with 0 vertices"); }
}

void GX_BeginDispList(void *list, u32 size) {
    checkNotInPrim("GX_BeginDispList");
    if (((uintptr_t)list) & 31) gxerr("display list buffer not 32-byte aligned");
    if (size & 31) gxerr("display list size not a multiple of 32");
    S->recording = true;
    S->dl = (u8 *)list;
    S->dlCap = size;
    S->dlLen = 0;
    S->dlOverflow = false;
}

u32 GX_EndDispList(void) {
    if (S->inPrim) gxerr("GX_EndDispList with incomplete primitive");
    S->recording = false;
    if (S->dlOverflow) return 0;   // libogc returns 0 on overflow
    while (S->dlLen & 31) {
        if (S->dlLen >= S->dlCap) return 0;
        S->dl[S->dlLen++] = 0;  // GX_NOP
    }
    return S->dlLen;
}

void GX_CallDispList(void *list, u32 nbytes) {
    checkNotInPrim("GX_CallDispList");
    S->stats.dl_calls++;
    if (((uintptr_t)list) & 31) gxerr("GX_CallDispList: list not 32-byte aligned");
    if (nbytes & 31) gxerr("GX_CallDispList: size %u not a multiple of 32", nbytes);
    const u8 *p = (const u8 *)list, *end = p + nbytes;
    std::vector<std::pair<u8, u8>> layout;
    while (p < end) {
        u8 op = *p++;
        if (op == 0) continue;
        if (op & 0x80) {
            if (p + 2 > end) { gxerr("display list truncated in draw header"); return; }
            u8 prim = op & 0xF8, fmt = op & 7;
            u16 cnt = be16(p);
            p += 2;
            u32 vsz;
            buildLayout(fmt, layout, vsz);
            if (p + (size_t)cnt * vsz > end) {
                gxerr("display list draw (%u verts x %u bytes) overruns list (format/descriptor mismatch?)", cnt, vsz);
                return;
            }
            drawPrimitive(prim, fmt, cnt, p);
            p += (size_t)cnt * vsz;
        } else if (op == 0x48) {
            // invalidate vertex cache
        } else {
            gxerr("display list opcode 0x%02x not supported by gxemu", op);
            return;
        }
    }
}

#define IMM(kind, n, fill) do { u8 b[16]; fill; pushBytes(kind, b, n); } while (0)

void GX_Position3f32(f32 x, f32 y, f32 z) { IMM(GX_VA_POS, 12, (wf(b, x), wf(b + 4, y), wf(b + 8, z))); }
void GX_Position3s16(s16 x, s16 y, s16 z) { IMM(GX_VA_POS, 6, (w16(b, x), w16(b + 2, y), w16(b + 4, z))); }
void GX_Position3u16(u16 x, u16 y, u16 z) { IMM(GX_VA_POS, 6, (w16(b, x), w16(b + 2, y), w16(b + 4, z))); }
void GX_Position3s8(s8 x, s8 y, s8 z) { IMM(GX_VA_POS, 3, (b[0] = x, b[1] = y, b[2] = z)); }
void GX_Position3u8(u8 x, u8 y, u8 z) { IMM(GX_VA_POS, 3, (b[0] = x, b[1] = y, b[2] = z)); }
void GX_Position2f32(f32 x, f32 y) { IMM(GX_VA_POS, 8, (wf(b, x), wf(b + 4, y))); }
void GX_Position2s16(s16 x, s16 y) { IMM(GX_VA_POS, 4, (w16(b, x), w16(b + 2, y))); }
void GX_Position2u16(u16 x, u16 y) { IMM(GX_VA_POS, 4, (w16(b, x), w16(b + 2, y))); }
void GX_Position1x16(u16 i) { IMM(GX_VA_POS, 2, w16(b, i)); }
void GX_Position1x8(u8 i) { IMM(GX_VA_POS, 1, b[0] = i); }
void GX_Normal3f32(f32 x, f32 y, f32 z) { IMM(GX_VA_NRM, 12, (wf(b, x), wf(b + 4, y), wf(b + 8, z))); }
void GX_Normal3s16(s16 x, s16 y, s16 z) { IMM(GX_VA_NRM, 6, (w16(b, x), w16(b + 2, y), w16(b + 4, z))); }
void GX_Normal3s8(s8 x, s8 y, s8 z) { IMM(GX_VA_NRM, 3, (b[0] = x, b[1] = y, b[2] = z)); }
void GX_Normal1x16(u16 i) { IMM(GX_VA_NRM, 2, w16(b, i)); }
void GX_Normal1x8(u8 i) { IMM(GX_VA_NRM, 1, b[0] = i); }
void GX_Color4u8(u8 r, u8 g, u8 bb, u8 a) { IMM(GX_VA_CLR0, 4, (b[0] = r, b[1] = g, b[2] = bb, b[3] = a)); }
void GX_Color3u8(u8 r, u8 g, u8 bb) { IMM(GX_VA_CLR0, 3, (b[0] = r, b[1] = g, b[2] = bb)); }
void GX_Color1u32(u32 c) { IMM(GX_VA_CLR0, 4, (b[0] = c >> 24, b[1] = c >> 16, b[2] = c >> 8, b[3] = c)); }
void GX_Color1u16(u16 c) { IMM(GX_VA_CLR0, 2, w16(b, c)); }
void GX_Color1x16(u16 i) { IMM(GX_VA_CLR0, 2, w16(b, i)); }
void GX_Color1x8(u8 i) { IMM(GX_VA_CLR0, 1, b[0] = i); }
void GX_TexCoord2f32(f32 s, f32 t) { IMM(GX_VA_TEX0, 8, (wf(b, s), wf(b + 4, t))); }
void GX_TexCoord2s16(s16 s, s16 t) { IMM(GX_VA_TEX0, 4, (w16(b, s), w16(b + 2, t))); }
void GX_TexCoord2u16(u16 s, u16 t) { IMM(GX_VA_TEX0, 4, (w16(b, s), w16(b + 2, t))); }
void GX_TexCoord2s8(s8 s, s8 t) { IMM(GX_VA_TEX0, 2, (b[0] = s, b[1] = t)); }
void GX_TexCoord2u8(u8 s, u8 t) { IMM(GX_VA_TEX0, 2, (b[0] = s, b[1] = t)); }
void GX_TexCoord1x16(u16 i) { IMM(GX_VA_TEX0, 2, w16(b, i)); }
void GX_TexCoord1x8(u8 i) { IMM(GX_VA_TEX0, 1, b[0] = i); }
void GX_MatrixIndex1x8(u8 i) { IMM(255, 1, b[0] = i); }

// ====================================================================
//                       API: transform & raster setup
// ====================================================================
void GX_LoadProjectionMtx(Mtx44 mt, u8 type) {
    STATE_CALL("GX_LoadProjectionMtx");
    S->projType = type;
    S->proj[0] = mt[0][0];
    S->proj[2] = mt[1][1];
    S->proj[4] = mt[2][2];
    S->proj[5] = mt[2][3];
    if (type == GX_PERSPECTIVE) {
        S->proj[1] = mt[0][2];
        S->proj[3] = mt[1][2];
    } else {
        S->proj[1] = mt[0][3];
        S->proj[3] = mt[1][3];
    }
}

void GX_LoadPosMtxImm(Mtx mt, u32 pnidx) {
    STATE_CALL("GX_LoadPosMtxImm");
    S->stats.mtx_loads++;
    if (pnidx > 61) { gxerr("GX_LoadPosMtxImm index %u out of range", pnidx); return; }
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) S->pos[pnidx + r][c] = mt[r][c];
}

void GX_LoadNrmMtxImm(Mtx mt, u32 pnidx) {
    STATE_CALL("GX_LoadNrmMtxImm");
    S->stats.mtx_loads++;
    if (pnidx % 3 || pnidx > 27) { gxerr("GX_LoadNrmMtxImm index %u invalid", pnidx); return; }
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) S->nrm[pnidx + r][c] = mt[r][c];
}

void GX_LoadNrmMtxImm3x3(Mtx33 mt, u32 pnidx) {
    STATE_CALL("GX_LoadNrmMtxImm3x3");
    if (pnidx % 3 || pnidx > 27) { gxerr("GX_LoadNrmMtxImm3x3 index invalid"); return; }
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) S->nrm[pnidx + r][c] = mt[r][c];
}

void GX_LoadTexMtxImm(Mtx mt, u32 texidx, u8 type) {
    STATE_CALL("GX_LoadTexMtxImm");
    int rows = (type == GX_MTX2x4) ? 2 : 3;
    if (texidx >= 64) { gxerr("dual texture matrices not supported"); return; }
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < 4; c++) S->pos[texidx + r][c] = mt[r][c];
}

void GX_SetCurrentMtx(u32 mtx) {
    STATE_CALL("GX_SetCurrentMtx");
    S->curPos = mtx;
}

void GX_SetViewport(f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ) {
    STATE_CALL("GX_SetViewport");
    S->vpX = xOrig; S->vpY = yOrig; S->vpW = wd; S->vpH = ht; S->vpN = nearZ; S->vpF = farZ;
}

void GX_SetScissor(u32 x, u32 y, u32 w, u32 h) {
    STATE_CALL("GX_SetScissor");
    S->scX0 = x; S->scY0 = y; S->scX1 = x + w; S->scY1 = y + h;
}

void GX_SetCullMode(u8 mode) { STATE_CALL("GX_SetCullMode"); S->cull = mode; }
void GX_SetClipMode(u8) {}
void GX_SetCoPlanar(u8) {}

void GX_SetNumTexGens(u32 nr) {
    STATE_CALL("GX_SetNumTexGens");
    if (nr > 8) gxerr("too many texgens");
    S->numTexGens = nr;
}

void GX_SetTexCoordGen2(u16 texcoord, u32 tgen_typ, u32 tgen_src, u32 mtxsrc, u32 normalize, u32 postmtx) {
    STATE_CALL("GX_SetTexCoordGen");
    if (texcoord >= 8) { gxerr("bad texcoord"); return; }
    TexGen &g = S->tg[texcoord];
    g.type = tgen_typ; g.src = tgen_src; g.mtx = mtxsrc; g.normalize = normalize; g.postmtx = postmtx;
    S->curTex[texcoord] = mtxsrc;
}

void GX_SetTexCoordGen(u16 texcoord, u32 tgen_typ, u32 tgen_src, u32 mtxsrc) {
    GX_SetTexCoordGen2(texcoord, tgen_typ, tgen_src, mtxsrc, GX_FALSE, 125);
}

// ====================================================================
//                            lighting
// ====================================================================
void GX_SetNumChans(u8 num) { STATE_CALL("GX_SetNumChans"); S->numChans = num; }

void GX_SetChanCtrl(s32 channel, u8 enable, u8 ambsrc, u8 matsrc, u8 litmask, u8 diff_fn, u8 attn_fn) {
    STATE_CALL("GX_SetChanCtrl");
    ChanCtrl c;
    c.enable = enable; c.ambsrc = ambsrc; c.matsrc = matsrc; c.litmask = litmask;
    c.diffn = (attn_fn == GX_AF_SPEC) ? GX_DF_NONE : diff_fn;
    c.attnfn = attn_fn;
    int reg = channel & 3;
    S->chan[reg] = c;
    // replicate libogc behaviour exactly: anything but COLOR0A0 also writes ALPHA1
    if (channel == GX_COLOR0A0) S->chan[2] = c;
    else S->chan[3] = c;
}

static void setChanColor(GXColor *dst, s32 channel, GXColor color) {
    switch (channel) {
        case GX_COLOR0: dst[0].r = color.r; dst[0].g = color.g; dst[0].b = color.b; break;
        case GX_COLOR1: dst[1].r = color.r; dst[1].g = color.g; dst[1].b = color.b; break;
        case GX_ALPHA0: dst[0].a = color.a; break;
        case GX_ALPHA1: dst[1].a = color.a; break;
        case GX_COLOR0A0: dst[0] = color; break;
        case GX_COLOR1A1: dst[1] = color; break;
    }
}
void GX_SetChanAmbColor(s32 channel, GXColor color) { STATE_CALL("GX_SetChanAmbColor"); setChanColor(S->amb, channel, color); }
void GX_SetChanMatColor(s32 channel, GXColor color) { STATE_CALL("GX_SetChanMatColor"); setChanColor(S->mat, channel, color); }

void GX_InitLightPos(GXLightObj *l, f32 x, f32 y, f32 z) { l->pos[0] = x; l->pos[1] = y; l->pos[2] = z; }
void GX_InitLightDir(GXLightObj *l, f32 x, f32 y, f32 z) { l->dir[0] = -x; l->dir[1] = -y; l->dir[2] = -z; }
void GX_InitLightColor(GXLightObj *l, GXColor c) { l->color = c; }
void GX_InitLightAttn(GXLightObj *l, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {
    l->a[0] = a0; l->a[1] = a1; l->a[2] = a2; l->k[0] = k0; l->k[1] = k1; l->k[2] = k2;
}
void GX_InitLightAttnA(GXLightObj *l, f32 a0, f32 a1, f32 a2) { l->a[0] = a0; l->a[1] = a1; l->a[2] = a2; }
void GX_InitLightAttnK(GXLightObj *l, f32 k0, f32 k1, f32 k2) { l->k[0] = k0; l->k[1] = k1; l->k[2] = k2; }
void GX_InitLightDistAttn(GXLightObj *l, f32 ref_dist, f32 ref_brite, u8 dist_fn) {
    // libogc formulas
    f32 k0 = 1, k1 = 0, k2 = 0;
    if (ref_dist < 0 || ref_brite <= 0 || ref_brite >= 1) dist_fn = 0;
    switch (dist_fn) {
        case 1: k0 = 1; k1 = (1 - ref_brite) / (ref_brite * ref_dist); k2 = 0; break;
        case 2: k0 = 1; k1 = 0.5f * (1 - ref_brite) / (ref_brite * ref_dist); k2 = 0.5f * (1 - ref_brite) / (ref_brite * ref_dist * ref_dist); break;
        case 3: k0 = 1; k1 = 0; k2 = (1 - ref_brite) / (ref_brite * ref_dist * ref_dist); break;
        default: break;
    }
    l->k[0] = k0; l->k[1] = k1; l->k[2] = k2;
}
void GX_InitLightSpot(GXLightObj *l, f32 cut_off, u8 spotfn) {
    f32 a0 = 1, a1 = 0, a2 = 0;
    if (cut_off <= 0 || cut_off > 90) spotfn = 0;
    f32 cr = cosf(cut_off * 3.14159265f / 180.0f);
    switch (spotfn) {
        case 1: a0 = -1000 * cr; a1 = 1000; a2 = 0; break;
        case 2: a0 = -cr / (1 - cr); a1 = 1 / (1 - cr); a2 = 0; break;
        case 3: a0 = 0; a1 = -cr / (1 - cr); a2 = 1 / (1 - cr); break;
        default: break;
    }
    l->a[0] = a0; l->a[1] = a1; l->a[2] = a2;
}
void GX_LoadLightObj(const GXLightObj *l, u8 lit_id) {
    STATE_CALL("GX_LoadLightObj");
    int i = 0;
    while (i < 8 && !(lit_id & (1 << i))) i++;
    if (i >= 8) { gxerr("GX_LoadLightObj bad id"); return; }
    S->light[i] = *l;
}

// ====================================================================
//                               TEV
// ====================================================================
void GX_SetNumTevStages(u8 num) {
    STATE_CALL("GX_SetNumTevStages");
    if (num < 1 || num > 16) gxerr("bad TEV stage count %d", num);
    S->numTev = num;
}
void GX_SetTevOrder(u8 st, u8 tc, u32 tm, u8 color) {
    STATE_CALL("GX_SetTevOrder");
    TevStage &t = S->tev[st];
    t.texcoord = tc; t.texmap = tm; t.color = color;
    if (tc != GX_TEXCOORDNULL && tc >= 8) gxerr("bad texcoord in TevOrder");
}
void GX_SetTevColorIn(u8 st, u8 a, u8 b, u8 c, u8 d) {
    STATE_CALL("GX_SetTevColorIn");
    TevStage &t = S->tev[st];
    t.ca = a; t.cb = b; t.cc = c; t.cd = d;
}
void GX_SetTevAlphaIn(u8 st, u8 a, u8 b, u8 c, u8 d) {
    STATE_CALL("GX_SetTevAlphaIn");
    TevStage &t = S->tev[st];
    t.aa = a; t.ab = b; t.ac = c; t.ad = d;
}
void GX_SetTevColorOp(u8 st, u8 op, u8 bias, u8 scale, u8 clamp, u8 reg) {
    STATE_CALL("GX_SetTevColorOp");
    TevStage &t = S->tev[st];
    t.cop = op; t.cbias = bias; t.cscale = scale; t.cclamp = clamp; t.creg = reg;
}
void GX_SetTevAlphaOp(u8 st, u8 op, u8 bias, u8 scale, u8 clamp, u8 reg) {
    STATE_CALL("GX_SetTevAlphaOp");
    TevStage &t = S->tev[st];
    t.aop = op; t.abias = bias; t.ascale = scale; t.aclamp = clamp; t.areg = reg;
}
void GX_SetTevOp(u8 st, u8 mode) {
    u8 dc = (st == GX_TEVSTAGE0) ? GX_CC_RASC : GX_CC_CPREV;
    u8 da = (st == GX_TEVSTAGE0) ? GX_CA_RASA : GX_CA_APREV;
    switch (mode) {
        case GX_MODULATE:
            GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_TEXC, dc, GX_CC_ZERO);
            GX_SetTevAlphaIn(st, GX_CA_ZERO, GX_CA_TEXA, da, GX_CA_ZERO);
            break;
        case GX_DECAL:
            GX_SetTevColorIn(st, dc, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
            GX_SetTevAlphaIn(st, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, da);
            break;
        case GX_BLEND:
            GX_SetTevColorIn(st, dc, GX_CC_ONE, GX_CC_TEXC, GX_CC_ZERO);
            GX_SetTevAlphaIn(st, GX_CA_ZERO, GX_CA_TEXA, da, GX_CA_ZERO);
            break;
        case GX_REPLACE:
            GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
            GX_SetTevAlphaIn(st, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
            break;
        case GX_PASSCLR:
            GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, dc);
            GX_SetTevAlphaIn(st, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, da);
            break;
    }
    GX_SetTevColorOp(st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp(st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}
void GX_SetTevColor(u8 reg, GXColor c) {
    STATE_CALL("GX_SetTevColor");
    S->reg[reg][0] = c.r; S->reg[reg][1] = c.g; S->reg[reg][2] = c.b; S->reg[reg][3] = c.a;
}
void GX_SetTevColorS10(u8 reg, GXColorS10 c) {
    STATE_CALL("GX_SetTevColorS10");
    S->reg[reg][0] = c.r; S->reg[reg][1] = c.g; S->reg[reg][2] = c.b; S->reg[reg][3] = c.a;
}
void GX_SetTevKColor(u8 sel, GXColor c) { STATE_CALL("GX_SetTevKColor"); S->kcol[sel & 3] = c; }
void GX_SetTevKColorSel(u8 st, u8 sel) { STATE_CALL("GX_SetTevKColorSel"); S->tev[st].ksel = sel; }
void GX_SetTevKAlphaSel(u8 st, u8 sel) { STATE_CALL("GX_SetTevKAlphaSel"); S->tev[st].kasel = sel; }
void GX_SetTevSwapMode(u8 st, u8 ras, u8 tex) { STATE_CALL("GX_SetTevSwapMode"); S->tev[st].rasswap = ras; S->tev[st].texswap = tex; }
void GX_SetTevSwapModeTable(u8 id, u8 r, u8 g, u8 b, u8 a) {
    STATE_CALL("GX_SetTevSwapModeTable");
    S->swap[id][0] = r; S->swap[id][1] = g; S->swap[id][2] = b; S->swap[id][3] = a;
}
void GX_SetTevDirect(u8) {}
void GX_SetNumIndStages(u8 n) { if (n) gxerr("indirect texturing not supported by gxemu"); }

// ====================================================================
//                             textures
// ====================================================================
static bool isPow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

void GX_InitTexObj(GXTexObj *obj, void *img, u16 wd, u16 ht, u8 fmt, u8 ws, u8 wt, u8 mip) {
    memset(obj, 0, sizeof(*obj));
    obj->data = img; obj->width = wd; obj->height = ht; obj->fmt = fmt;
    obj->wrap_s = ws; obj->wrap_t = wt; obj->mipmap = mip;
    obj->minfilt = mip ? GX_LIN_MIP_LIN : GX_LINEAR;
    obj->magfilt = GX_LINEAR;
    obj->minlod = 0;
    obj->maxlod = 0;
    if (mip) {
        int levels = 1, w = wd, h = ht;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); levels++; }
        obj->maxlod = (f32)(levels - 1);
    }
    if (((uintptr_t)img) & 31) gxerr("texture data not 32-byte aligned");
    if ((ws != GX_CLAMP && !isPow2(wd)) || (wt != GX_CLAMP && !isPow2(ht))) gxerr("repeat/mirror wrap needs power-of-two texture (%dx%d)", wd, ht);
    if (wd > 1024 || ht > 1024) gxerr("texture too large");
}
void GX_InitTexObjLOD(GXTexObj *obj, u8 minf, u8 magf, f32 minlod, f32 maxlod, f32 bias, u8, u8, u8) {
    obj->minfilt = minf; obj->magfilt = magf; obj->minlod = minlod; obj->maxlod = maxlod; obj->lodbias = bias;
}
void GX_InitTexObjFilterMode(GXTexObj *obj, u8 minf, u8 magf) { obj->minfilt = minf; obj->magfilt = magf; }
void GX_InitTexObjWrapMode(GXTexObj *obj, u8 ws, u8 wt) { obj->wrap_s = ws; obj->wrap_t = wt; }
void GX_InitTexObjMaxLOD(GXTexObj *obj, f32 v) { obj->maxlod = v; }
void GX_InitTexObjMinLOD(GXTexObj *obj, f32 v) { obj->minlod = v; }
void GX_InitTexObjLODBias(GXTexObj *obj, f32 v) { obj->lodbias = v; }
void GX_LoadTexObj(GXTexObj *obj, u8 mapid) {
    STATE_CALL("GX_LoadTexObj");
    if (mapid >= 8) { gxerr("bad texmap"); return; }
    S->tmap[mapid] = *obj;
    S->tmapValid[mapid] = true;
}
u32 GX_GetTexBufferSize(u16 wd, u16 ht, u32 fmt, u8 mipmap, u8 maxlod) {
    u32 total = 0;
    int w = wd, h = ht;
    int levels = mipmap ? maxlod : 1;
    if (levels < 1) levels = 1;
    for (int i = 0; i < levels; i++) {
        total += levelBytes(w, h, (u8)fmt);
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    return total;
}

// ====================================================================
//                           pixel engine
// ====================================================================
void GX_SetZMode(u8 en, u8 func, u8 upd) { STATE_CALL("GX_SetZMode"); S->zEnable = en; S->zFunc = func; S->zUpdate = upd; }
void GX_SetZCompLoc(u8 before) { STATE_CALL("GX_SetZCompLoc"); S->zBefore = before; }
void GX_SetBlendMode(u8 type, u8 src, u8 dst, u8 op) {
    STATE_CALL("GX_SetBlendMode");
    S->bmType = type; S->bmSrc = src; S->bmDst = dst; S->bmOp = op;
}
void GX_SetAlphaCompare(u8 c0, u8 r0, u8 op, u8 c1, u8 r1) {
    STATE_CALL("GX_SetAlphaCompare");
    S->acComp0 = c0; S->acRef0 = r0; S->acOp = op; S->acComp1 = c1; S->acRef1 = r1;
}
void GX_SetColorUpdate(u8 en) { STATE_CALL("GX_SetColorUpdate"); S->colorUpdate = en; }
void GX_SetAlphaUpdate(u8 en) { STATE_CALL("GX_SetAlphaUpdate"); S->alphaUpdate = en; }
void GX_SetDstAlpha(u8 en, u8 a) { STATE_CALL("GX_SetDstAlpha"); S->dstAlphaEn = en; S->dstAlpha = a; }
void GX_SetDither(u8) {}
void GX_SetPixelFmt(u8 pf, u8) { STATE_CALL("GX_SetPixelFmt"); S->pixFmt = pf; }
void GX_SetFog(u8 type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor col) {
    STATE_CALL("GX_SetFog");
    S->fogType = type; S->fogStart = startz; S->fogEnd = endz; S->fogNear = nearz; S->fogFar = farz; S->fogColor = col;
}
void GX_SetFogColor(GXColor c) { STATE_CALL("GX_SetFogColor"); S->fogColor = c; }
void GX_SetLineWidth(u8, u8) {}
void GX_SetPointSize(u8 w, u8) { g_pointSize = w / 6.0f; }

// ====================================================================
//                               copies
// ====================================================================
void GX_SetCopyClear(GXColor c, u32 z) { STATE_CALL("GX_SetCopyClear"); S->clearColor = c; S->clearZ = z; }
void GX_SetDispCopySrc(u16 l, u16 t, u16 w, u16 h) { S->dispSrc[0] = l; S->dispSrc[1] = t; S->dispSrc[2] = w; S->dispSrc[3] = h; }
void GX_SetDispCopyDst(u16 w, u16 h) { S->dispDst[0] = w; S->dispDst[1] = h; }
u32 GX_SetDispCopyYScale(f32 ys) { return (u32)(S->dispSrc[3] * ys); }
f32 GX_GetYScaleFactor(u16 efbHeight, u16 xfbHeight) { return (f32)xfbHeight / (f32)efbHeight; }
void GX_SetDispCopyGamma(u8) {}
void GX_SetCopyFilter(u8, u8[12][2], u8 vf, u8 vfilter[7]) {
    S->vfEnable = vf;
    if (vfilter) memcpy(S->vfilter, vfilter, 7);
}
void GX_SetFieldMode(u8, u8) {}

static void clearEfbRect(int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h && y < EFB_H; y++)
        for (int x = x0; x < x0 + w && x < EFB_W; x++) {
            u8 *p = &S->efb[((size_t)y * EFB_W + x) * 4];
            p[0] = S->clearColor.r; p[1] = S->clearColor.g; p[2] = S->clearColor.b; p[3] = S->clearColor.a;
            S->zbuf[(size_t)y * EFB_W + x] = (S->clearZ & 0xFFFFFF) / 16777215.0f;
        }
}

void GX_CopyDisp(void *dest, u8 clear) {
    checkNotInPrim("GX_CopyDisp");
    (void)dest;
    int w = S->dispSrc[2], h = S->dispSrc[3];
    int x0 = S->dispSrc[0], y0 = S->dispSrc[1];
    S->frameW = w;
    S->frameH = h;
    S->frame.assign((size_t)w * h * 3, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int k = 0; k < 3; k++) {
                int v;
                if (S->vfEnable) {
                    int acc = 0;
                    for (int t = 0; t < 7; t++) {
                        int yy = std::min(std::max(y0 + y + t - 3, 0), EFB_H - 1);
                        acc += S->vfilter[t] * S->efb[((size_t)yy * EFB_W + x0 + x) * 4 + k];
                    }
                    v = std::min(255, acc >> 6);
                } else {
                    v = S->efb[((size_t)(y0 + y) * EFB_W + x0 + x) * 4 + k];
                }
                S->frame[((size_t)y * w + x) * 3 + k] = (u8)v;
            }
        }
    }
    if (clear) clearEfbRect(x0, y0, w, h);
}

void GX_SetTexCopySrc(u16 l, u16 t, u16 w, u16 h) { S->texSrc[0] = l; S->texSrc[1] = t; S->texSrc[2] = w; S->texSrc[3] = h; }
void GX_SetTexCopyDst(u16 w, u16 h, u32 fmt, u8 mip) { S->texDst[0] = w; S->texDst[1] = h; S->texDstFmt = fmt; S->texDstMip = mip; }

void GX_CopyTex(void *dest, u8 clear) {
    checkNotInPrim("GX_CopyTex");
    if (((uintptr_t)dest) & 31) gxerr("GX_CopyTex destination not 32-byte aligned");
    int sw = S->texSrc[2], sh = S->texSrc[3];
    int dw = S->texDst[0], dh = S->texDst[1];
    int scale = S->texDstMip ? 2 : 1;
    if (dw * scale != sw || dh * scale != sh) gxerr("GX_CopyTex: dst size %dx%d does not match src %dx%d (mip=%d)", dw, dh, sw, sh, S->texDstMip);
    std::vector<u8> rgba((size_t)dw * dh * 4);
    for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++)
            for (int k = 0; k < 4; k++) {
                int acc = 0;
                for (int yy = 0; yy < scale; yy++)
                    for (int xx = 0; xx < scale; xx++)
                        acc += S->efb[((size_t)(S->texSrc[1] + y * scale + yy) * EFB_W + S->texSrc[0] + x * scale + xx) * 4 + k];
                rgba[((size_t)y * dw + x) * 4 + k] = (u8)(acc / (scale * scale));
            }
    u8 fmt = (u8)(S->texDstFmt & 0xF);
    if (S->texDstFmt > 0xF) gxerr("GX_CopyTex: only plain texture formats are emulated");
    int tw = texTileW(fmt), th = texTileH(fmt);
    u8 *o = (u8 *)dest;
    for (int ty = 0; ty < (dh + th - 1) / th; ty++)
        for (int tx = 0; tx < (dw + tw - 1) / tw; tx++) {
            if (fmt == GX_TF_RGBA8) {
                for (int i = 0; i < 16; i++) {
                    int x = std::min(tx * 4 + (i & 3), dw - 1), y = std::min(ty * 4 + (i >> 2), dh - 1);
                    const u8 *c = &rgba[((size_t)y * dw + x) * 4];
                    o[i * 2] = c[3]; o[i * 2 + 1] = c[0]; o[32 + i * 2] = c[1]; o[32 + i * 2 + 1] = c[2];
                }
                o += 64;
                continue;
            }
            for (int i = 0; i < tw * th; i++) {
                int x = std::min(tx * tw + (i % tw), dw - 1), y = std::min(ty * th + (i / tw), dh - 1);
                const u8 *c = &rgba[((size_t)y * dw + x) * 4];
                int lum = (c[0] * 77 + c[1] * 150 + c[2] * 29) >> 8;
                switch (fmt) {
                    case GX_TF_RGB565: { u16 v = ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3); w16(o + i * 2, v); break; }
                    case GX_TF_RGB5A3: { u16 v = 0x8000 | ((c[0] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[2] >> 3); w16(o + i * 2, v); break; }
                    case GX_TF_I8: o[i] = (u8)lum; break;
                    case GX_TF_IA8: o[i * 2] = c[3]; o[i * 2 + 1] = (u8)lum; break;
                    case GX_TF_I4: if (i & 1) o[i >> 1] |= (lum >> 4); else o[i >> 1] = (u8)(lum & 0xF0); break;
                    default: gxerr("GX_CopyTex format %d not emulated", fmt); return;
                }
            }
            o += tw * th * texBpp(fmt) / 8;
        }
    if (clear) clearEfbRect(S->texSrc[0], S->texSrc[1], sw, sh);
}

void GX_PixModeSync(void) {}

// ====================================================================
//                         emulator controls
// ====================================================================
void gxemu_set_raster(int enabled) {
    if (!S) initState();
    S->raster = enabled != 0;
}
const u8 *gxemu_last_frame(int *w, int *h) {
    *w = S->frameW;
    *h = S->frameH;
    return S->frame.data();
}
void gxemu_get_stats(GXEmuStats *out) { *out = S->stats; }
void gxemu_reset_stats(void) { u32 e = S->stats.errors; memset(&S->stats, 0, sizeof(S->stats)); S->stats.errors = e; }
int gxemu_error_count(void) { return S ? S->errors : 0; }

}  // extern "C"

#include "gfx/renderer.h"
#include "platform/platform.h"

namespace {
Environment g_env;
Camera g_cam;
u8 *g_ramp;
GXTexObj g_rampObj;
u32 g_shadeKey = 0xFFFFFFFFu;
int g_fogOn = -1;
int g_vcdMode = -1;   // cached vertex descriptor configuration
gfx::Stats g_stats;
const int RAMP_W = 32, RAMP_H = 32;
GXColor g_tint = {0, 0, 0, 0};
bool g_tintValid = false;

void writeRGBA8(u8 *dst, int w, int h, const u8 *rgba) {
    for (int ty = 0; ty < h / 4; ty++)
        for (int tx = 0; tx < w / 4; tx++) {
            u8 *blk = dst + (ty * (w / 4) + tx) * 64;
            for (int i = 0; i < 16; i++) {
                int x = tx * 4 + (i & 3), y = ty * 4 + (i >> 2);
                const u8 *c = rgba + (y * w + x) * 4;
                blk[i * 2] = c[3];
                blk[i * 2 + 1] = c[0];
                blk[32 + i * 2] = c[1];
                blk[32 + i * 2 + 1] = c[2];
            }
        }
}

void applyFog(bool on) {
    int v = on ? 1 : 0;
    if (v == g_fogOn) return;
    g_fogOn = v;
    if (on) GX_SetFog(GX_FOG_PERSP_LIN, g_env.fogStart, g_env.fogEnd, g_cam.nearZ, g_cam.farZ, g_env.fogColor);
    else GX_SetFog(GX_FOG_NONE, 0, 1, 0.1f, 1, g_env.fogColor);
}

inline f32 maxScale(const Mat34 &m) {
    f32 a = m.axisX().len2(), b = m.axisY().len2(), c = m.axisZ().len2();
    return sqrtf(hvMax(a, hvMax(b, c)));
}

enum { VCD_NONE = 0, VCD_MODEL = 1, VCD_SKIN = 2 };

void setupModelArrays(const Model *m, bool skinned) {
    int mode = (skinned ? 0x100 : 0) | (m->flags & (MF_COLORS | MF_UVS));
    if (mode != g_vcdMode) {
        g_vcdMode = mode;
        GX_ClearVtxDesc();
        if (skinned) GX_SetVtxDesc(GX_VA_PTNMTXIDX, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_POS, GX_INDEX16);
        GX_SetVtxDesc(GX_VA_NRM, GX_INDEX16);
        if (m->flags & MF_COLORS) GX_SetVtxDesc(GX_VA_CLR0, GX_INDEX16);
        if (m->flags & MF_UVS) GX_SetVtxDesc(GX_VA_TEX0, GX_INDEX16);
    }
    GX_SetVtxAttrFmt(VFMT_MODEL, GX_VA_POS, GX_POS_XYZ, GX_S16, m->posFrac);
    if (m->flags & MF_UVS) GX_SetVtxAttrFmt(VFMT_MODEL, GX_VA_TEX0, GX_TEX_ST, GX_S16, m->uvFrac);
    GX_SetArray(GX_VA_POS, m->pos, 6);
    GX_SetArray(GX_VA_NRM, m->nrm, 3);
    if (m->clr) GX_SetArray(GX_VA_CLR0, m->clr, 4);
    if (m->uv) GX_SetArray(GX_VA_TEX0, m->uv, 4);
}

u32 batchShade(const Model *m, const ModelBatch &b, u32 extra) {
    u32 f = SH_LIT | extra;
    if ((m->flags & MF_UVS) && b.texSlot != 0xFF && b.texSlot < m->numTex && m->tex[b.texSlot]) f |= SH_TEX;
    if (m->flags & MF_COLORS) f |= SH_VCOL;
    if (b.flags & BF_ALPHATEST) f |= SH_ALPHATEST;
    if (b.flags & BF_DOUBLESIDED) f |= SH_DOUBLESIDED;
    if (b.flags & BF_UNLIT) f &= ~(u32)(SH_LIT | SH_RIM);
    if (b.flags & BF_TRANSLUCENT) f |= SH_BLEND;
    return f;
}
}  // namespace

void Camera::update(f32 aspect) {
    view = lookAt(eye, target, Vec3(0, 1, 0));
    proj = perspectiveGX(fov, aspect, nearZ, farZ);
    tanY = tanf(fov * 0.5f * HV_PI / 180.0f);
    tanX = tanY * aspect;
}

namespace gfx {

Stats &stats() { return g_stats; }
const Environment &env() { return g_env; }
const Camera &camera() { return g_cam; }
const Mat34 &view() { return g_cam.view; }

void init() {
    // immediate-mode formats
    GX_SetVtxAttrFmt(VFMT_MODEL, GX_VA_NRM, GX_NRM_XYZ, GX_S8, 0);
    GX_SetVtxAttrFmt(VFMT_MODEL, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(VFMT_IMM, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(VFMT_IMM, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(VFMT_IMM, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(VFMT_IMM, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetVtxAttrFmt(VFMT_2D, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(VFMT_2D, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(VFMT_2D, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    g_ramp = (u8 *)hvAlignedAlloc(RAMP_W * RAMP_H * 4);
    GX_InitTexObj(&g_rampObj, g_ramp, RAMP_W, RAMP_H, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&g_rampObj, GX_LINEAR, GX_LINEAR);
    rebuildRamp();

    GX_SetCullMode(GX_CULL_BACK);
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_FALSE);
    GX_SetNumIndStages(0);
    for (int i = 0; i < 4; i++) GX_SetTevDirect((u8)i);
    invalidateState();
}

void vcdChanged() { g_vcdMode = -1; }

void invalidateState() {
    g_shadeKey = 0xFFFFFFFFu;
    g_fogOn = -1;
    g_vcdMode = -1;
    g_tintValid = false;
}

void setEnvironment(const Environment &e) {
    bool rampChanged = memcmp(&e.sunColor, &g_env.sunColor, sizeof(GXColor)) != 0 ||
                       memcmp(&e.shadowColor, &g_env.shadowColor, sizeof(GXColor)) != 0 ||
                       e.rimStrength != g_env.rimStrength || e.bandEdge != g_env.bandEdge || e.bandSoft != g_env.bandSoft;
    g_env = e;
    if (rampChanged) rebuildRamp();
}

void rebuildRamp() {
    static u8 rgba[RAMP_W * RAMP_H * 4];
    const Environment &e = g_env;
    for (int y = 0; y < RAMP_H; y++) {
        f32 t = (y + 0.5f) / RAMP_H;          // 0.5 + 0.5 * N.V
        for (int x = 0; x < RAMP_W; x++) {
            f32 s = (x + 0.5f) / RAMP_W;      // 0.5 + 0.5 * N.L
            // two-band cel ramp with a soft edge plus gentle gradients for volume
            f32 lit = hvSmooth((s - (e.bandEdge - e.bandSoft)) / (2.0f * e.bandSoft));
            f32 shade = 0.92f + 0.16f * hvSaturate((s - 0.5f) * 2.0f);   // lit side gradient
            f32 dark = 0.88f + 0.12f * hvSaturate(s * 2.2f);              // shadow side gradient
            f32 r = hvLerp(e.shadowColor.r * dark, e.sunColor.r * shade, lit);
            f32 g = hvLerp(e.shadowColor.g * dark, e.sunColor.g * shade, lit);
            f32 b = hvLerp(e.shadowColor.b * dark, e.sunColor.b * shade, lit);
            // rim: strongest at grazing angles (t near 0.5), mostly on the lit side
            f32 graze = 1.0f - hvSmooth((t - 0.5f) / 0.16f);
            f32 rim = graze * (0.35f + 0.65f * lit) * e.rimStrength;
            u8 *o = rgba + (y * RAMP_W + x) * 4;
            o[0] = (u8)hvClamp(r, 0.0f, 255.0f);
            o[1] = (u8)hvClamp(g, 0.0f, 255.0f);
            o[2] = (u8)hvClamp(b, 0.0f, 255.0f);
            o[3] = (u8)hvClamp(rim * 255.0f, 0.0f, 255.0f);
        }
    }
    writeRGBA8(g_ramp, RAMP_W, RAMP_H, rgba);
    DCFlushRange(g_ramp, RAMP_W * RAMP_H * 4);
    GX_InvalidateTexAll();
}

void beginScene(const Camera &cam) {
    g_cam = cam;
    g_stats = Stats();
    GX_SetViewport(0, 0, (f32)plat::screenW(), (f32)plat::screenH(), 0, 1);
    GX_SetScissor(0, 0, plat::screenW(), plat::screenH());
    GX_LoadProjectionMtx(g_cam.proj.m, GX_PERSPECTIVE);

    Vec3 toSun = normalize(-g_env.sunDir);
    Vec3 vs = g_cam.view.vector(toSun) * 100000.0f;
    GXLightObj sun;
    GX_InitLightPos(&sun, vs.x, vs.y, vs.z);
    GX_InitLightDir(&sun, 0, 0, -1);
    GX_InitLightAttn(&sun, 1, 0, 0, 1, 0, 0);
    GX_InitLightColor(&sun, gxc(127, 0, 0, 255));
    GX_LoadLightObj(&sun, GX_LIGHT0);
    GXLightObj eye;
    GX_InitLightPos(&eye, 0, 0, 0);
    GX_InitLightDir(&eye, 0, 0, -1);
    GX_InitLightAttn(&eye, 1, 0, 0, 1, 0, 0);
    GX_InitLightColor(&eye, gxc(0, 127, 0, 255));
    GX_LoadLightObj(&eye, GX_LIGHT1);

    GX_LoadTexObj(&g_rampObj, GX_TEXMAP7);
    GX_SetTevKColor(GX_KCOLOR0, g_env.rimColor);
    invalidateState();
}

void setTint(GXColor c) {
    if (g_tintValid && memcmp(&c, &g_tint, sizeof(c)) == 0) return;
    g_tint = c;
    g_tintValid = true;
    GX_SetChanMatColor(GX_COLOR0A0, c);
}

void setShade(u32 f) {
    if (f == g_shadeKey) return;
    g_shadeKey = f;
    bool tex = (f & SH_TEX) != 0;
    bool vcol = (f & SH_VCOL) != 0;
    bool lit = (f & SH_LIT) != 0;
    bool rim = lit && (f & SH_RIM);

    // colour channels
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, vcol ? GX_SRC_VTX : GX_SRC_REG, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    if (lit) {
        GX_SetNumChans(2);
        GX_SetChanCtrl(GX_COLOR1A1, GX_ENABLE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT0 | GX_LIGHT1, GX_DF_SIGNED, GX_AF_NONE);
        GX_SetChanAmbColor(GX_COLOR1A1, gxc(128, 128, 0, 0));
        GX_SetChanMatColor(GX_COLOR1A1, gxc(255, 255, 255, 255));
    } else {
        GX_SetNumChans(1);
    }

    // texture coordinate generation: regular texgens first, SRTG last
    u8 tcTex = GX_TEXCOORDNULL, tcRamp = GX_TEXCOORDNULL;
    u32 ntg = 0;
    if (tex) {
        tcTex = (u8)(GX_TEXCOORD0 + ntg);
        GX_SetTexCoordGen(tcTex, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
        ntg++;
    }
    if (lit) {
        tcRamp = (u8)(GX_TEXCOORD0 + ntg);
        GX_SetTexCoordGen(tcRamp, GX_TG_SRTG, GX_TG_COLOR1, GX_IDENTITY);
        ntg++;
    }
    GX_SetNumTexGens(ntg);

    u8 st = 0;
    if (tex) {
        GX_SetTevOrder(st, tcTex, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
        GX_SetTevAlphaIn(st, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
        GX_SetTevColorOp(st, GX_TEV_ADD, GX_TB_ZERO, (f & SH_DETAIL2X) ? GX_CS_SCALE_2 : GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        st++;
    }
    if (lit) {
        GX_SetTevOrder(st, tcRamp, GX_TEXMAP7, tex ? GX_COLORNULL : GX_COLOR0A0);
        GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_TEXC, tex ? GX_CC_CPREV : GX_CC_RASC, GX_CC_ZERO);
        GX_SetTevAlphaIn(st, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, tex ? GX_CA_APREV : GX_CA_RASA);
        GX_SetTevColorOp(st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        st++;
        if (rim) {
            GX_SetTevOrder(st, tcRamp, GX_TEXMAP7, GX_COLORNULL);
            GX_SetTevKColorSel(st, GX_TEV_KCSEL_K0);
            GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXA, GX_CC_CPREV);
            GX_SetTevAlphaIn(st, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
            GX_SetTevColorOp(st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            GX_SetTevAlphaOp(st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            st++;
        }
    } else if (!tex) {
        GX_SetTevOrder(st, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GX_SetTevColorIn(st, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
        GX_SetTevAlphaIn(st, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
        GX_SetTevColorOp(st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(st, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        st++;
    }
    GX_SetNumTevStages(st);

    // pixel engine
    if (f & SH_ALPHATEST) {
        GX_SetAlphaCompare(GX_GREATER, 110, GX_AOP_AND, GX_ALWAYS, 0);
        GX_SetZCompLoc(GX_FALSE);
    } else {
        GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        GX_SetZCompLoc(GX_TRUE);
    }
    if (f & SH_ADDITIVE) GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
    else if (f & SH_BLEND) GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    else GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    bool zwrite = !(f & (SH_BLEND | SH_ADDITIVE | SH_NOZWRITE));
    GX_SetZMode((f & SH_NOZTEST) ? GX_FALSE : GX_TRUE, GX_LEQUAL, zwrite ? GX_TRUE : GX_FALSE);
    GX_SetCullMode((f & SH_DOUBLESIDED) ? GX_CULL_NONE : GX_CULL_BACK);
    applyFog(!(f & SH_NOFOG));
}

bool visible(const Vec3 &c, f32 r) {
    Vec3 v = g_cam.view.point(c);
    f32 d = -v.z;
    if (d + r < g_cam.nearZ) return false;
    if (d - r > g_cam.farZ) return false;
    if (d - r > g_env.fogEnd + 10.0f) return false;   // fully fogged
    f32 kx = sqrtf(1.0f + g_cam.tanX * g_cam.tanX);
    f32 ky = sqrtf(1.0f + g_cam.tanY * g_cam.tanY);
    if (fabsf(v.x) > d * g_cam.tanX + r * kx) return false;
    if (fabsf(v.y) > d * g_cam.tanY + r * ky) return false;
    return true;
}

f32 viewDepth(const Vec3 &p) { return -g_cam.view.point(p).z; }

void loadWorld(const Mat34 &world) {
    Mat34 mv = g_cam.view * world;
    GX_LoadPosMtxImm(mv.m, GX_PNMTX0);
    f32 s = maxScale(world);
    if (fabsf(s - 1.0f) > 0.001f) {
        Mat34 n = mv;
        f32 is = 1.0f / s;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++) n.m[r][c] *= is;
        GX_LoadNrmMtxImm(n.m, GX_PNMTX0);
    } else {
        GX_LoadNrmMtxImm(mv.m, GX_PNMTX0);
    }
    GX_SetCurrentMtx(GX_PNMTX0);
}

void drawModel(const Model *m, const Mat34 &world, GXColor tint, u32 extra) {
    if (!m) return;
    f32 s = maxScale(world);
    if (!visible(world.point(m->center), m->radius * s)) {
        g_stats.culled++;
        return;
    }
    g_stats.models++;
    loadWorld(world);
    setupModelArrays(m, false);
    for (int i = 0; i < m->numBatches; i++) {
        const ModelBatch &b = m->batches[i];
        u32 f = batchShade(m, b, extra);
        setShade(f);
        if (!(f & SH_VCOL)) setTint(tint);
        if (f & SH_TEX) tex::bind(m->tex[b.texSlot], GX_TEXMAP0);
        GX_CallDispList(b.dl, b.dlSize);
        g_stats.batches++;
    }
}

void drawSkinned(const Model *m, const Mat34 &world, const Mat34 *drawMtx, GXColor tint, u32 extra) {
    if (!m || !(m->flags & MF_SKINNED)) return;
    f32 s = maxScale(world);
    if (!visible(world.point(m->center), m->radius * s * 1.4f)) {
        g_stats.culled++;
        return;
    }
    g_stats.skinned++;
    static Mat34 mvCache[160];
    static u8 mvValid[160];
    int n = hvMin<int>(m->numDrawMtx, 160);
    memset(mvValid, 0, (size_t)n);
    Mat34 vw = g_cam.view * world;
    setupModelArrays(m, true);
    for (int i = 0; i < m->numBatches; i++) {
        const ModelBatch &b = m->batches[i];
        for (int k = 0; k < b.numMtx; k++) {
            int di = b.mtx[k];
            if (di >= n) continue;
            if (!mvValid[di]) {
                mvCache[di] = vw * drawMtx[di];
                mvValid[di] = 1;
            }
            GX_LoadPosMtxImm(mvCache[di].m, GX_PNMTX0 + 3 * k);
            GX_LoadNrmMtxImm(mvCache[di].m, GX_PNMTX0 + 3 * k);
        }
        u32 f = batchShade(m, b, extra);
        setShade(f);
        if (!(f & SH_VCOL)) setTint(tint);
        if (f & SH_TEX) tex::bind(m->tex[b.texSlot], GX_TEXMAP0);
        GX_CallDispList(b.dl, b.dlSize);
        g_stats.batches++;
    }
    GX_SetCurrentMtx(GX_PNMTX0);
}

void beginImm(u8 prim, u16 count, bool withTex, bool withNormal) {
    int mode = 0x1000 | (withTex ? 1 : 0) | (withNormal ? 2 : 0);
    if (mode != g_vcdMode) {
        g_vcdMode = mode;
        GX_ClearVtxDesc();
        GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
        if (withNormal) GX_SetVtxDesc(GX_VA_NRM, GX_DIRECT);
        GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
        if (withTex) GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    }
    GX_Begin(prim, VFMT_IMM, count);
}

}  // namespace gfx

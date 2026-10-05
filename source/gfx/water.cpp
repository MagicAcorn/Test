#include "gfx/water.h"
#include "game/world.h"
#include "gfx/sky.h"
#include "gfx/texture.h"

namespace {
const int CH = 16;
const int POS_FRAC = 5;
struct WChunk {
    Vec3 center;
    f32 radius;
    void *dl;
    u32 size;
};
WChunk g_chunks[256];
int g_num = 0;
f32 g_level = 3;
const Texture *g_wave, *g_ring;

void vtxDesc() {
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    gfx::vcdChanged();
}

void setFmt() {
    GX_SetVtxAttrFmt(VFMT_PART, GX_VA_POS, GX_POS_XYZ, GX_S16, POS_FRAC);
    GX_SetVtxAttrFmt(VFMT_PART, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
}

void waterColor(f32 depth, u8 *c) {
    f32 t = hvSaturate(depth / 3.2f);
    f32 r = hvLerp(78, 22, t), g = hvLerp(178, 86, t), b = hvLerp(182, 132, t);
    f32 a = hvLerp(110, 225, hvSaturate(depth / 2.2f));
    if (depth < 0.45f) {
        f32 f = (0.45f - depth) / 0.45f * 0.75f;
        r = hvLerp(r, 236, f); g = hvLerp(g, 246, f); b = hvLerp(b, 246, f);
        a = hvLerp(a, 200, f);
    }
    if (depth < 0.0f) a = 0;
    c[0] = (u8)r; c[1] = (u8)g; c[2] = (u8)b; c[3] = (u8)a;
}
}  // namespace

namespace water {

void build(const World &w) {
    g_level = w.waterLevel();
    g_wave = tex::get("tx/water");
    g_ring = tex::get("tx/ring");
    int n = w.gridN();
    int nc = (n - 1) / CH;
    f32 cell = w.cell();
    const f32 S = (f32)(1 << POS_FRAC);
    setFmt();
    vtxDesc();
    for (int cz = 0; cz < nc; cz++)
        for (int cx = 0; cx < nc; cx++) {
            // collect wet cells
            static u8 wet[CH][CH];
            int count = 0;
            for (int z = 0; z < CH; z++)
                for (int x = 0; x < CH; x++) {
                    int ix = cx * CH + x, iz = cz * CH + z;
                    f32 m = hvMin(hvMin(w.vertexHeight(ix, iz), w.vertexHeight(ix + 1, iz)),
                                  hvMin(w.vertexHeight(ix, iz + 1), w.vertexHeight(ix + 1, iz + 1)));
                    wet[z][x] = m < g_level + 0.05f;
                    count += wet[z][x];
                }
            if (!count) continue;
            u32 cap = (u32)((count * 4 * 10 + 3 + 64 + 31) & ~31);
            void *buf = hvAlignedAlloc(cap);
            HV_BEGIN_DL(buf, cap);
            GX_Begin(GX_QUADS, VFMT_PART, (u16)(count * 4));
            for (int z = 0; z < CH; z++)
                for (int x = 0; x < CH; x++) {
                    if (!wet[z][x]) continue;
                    int ix = cx * CH + x, iz = cz * CH + z;
                    // clockwise from above
                    int vx[4] = {ix, ix + 1, ix + 1, ix};
                    int vz[4] = {iz, iz, iz + 1, iz + 1};
                    for (int k = 0; k < 4; k++) {
                        u8 c[4];
                        waterColor(g_level - w.vertexHeight(vx[k], vz[k]), c);
                        GX_Position3s16((s16)(vx[k] * cell * S), (s16)(g_level * S), (s16)(vz[k] * cell * S));
                        GX_Color4u8(c[0], c[1], c[2], c[3]);
                    }
                }
            GX_End();
            WChunk &wc = g_chunks[g_num++];
            wc.dl = buf;
            wc.size = hvEndDispList();
            wc.center = Vec3((cx + 0.5f) * CH * cell, g_level, (cz + 0.5f) * CH * cell);
            wc.radius = CH * cell * 0.72f;
        }
    hvLog("water: %d chunks", g_num);
}

void draw(f32 time) {
    if (!g_num || !g_wave) return;
    gfx::loadWorld(Mat34::identity());
    // two wave layers scrolled in different directions
    Mat34 a;
    memset(&a, 0, sizeof(a));
    a.m[0][0] = 1.0f / 9.0f; a.m[0][3] = time * 0.021f;
    a.m[1][2] = 1.0f / 9.0f; a.m[1][3] = time * 0.013f;
    GX_LoadTexMtxImm(a.m, GX_TEXMTX1, GX_MTX2x4);
    Mat34 b;
    memset(&b, 0, sizeof(b));
    b.m[0][0] = 0.062f; b.m[0][2] = 0.035f; b.m[0][3] = -time * 0.017f;
    b.m[1][0] = -0.035f; b.m[1][2] = 0.062f; b.m[1][3] = time * 0.024f;
    GX_LoadTexMtxImm(b.m, GX_TEXMTX2, GX_MTX2x4);

    gfx::invalidateState();
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(2);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_POS, GX_TEXMTX1);
    GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_POS, GX_TEXMTX2);
    f32 day = sky::dayFactor();
    const Environment &e = gfx::env();
    // base tint follows the light colour; highlights follow the sky
    GXColor tint = gxc((u8)hvMin(255.0f, e.sunColor.r * (0.55f + 0.45f * day)),
                       (u8)hvMin(255.0f, e.sunColor.g * (0.55f + 0.45f * day)),
                       (u8)hvMin(255.0f, e.sunColor.b * (0.6f + 0.4f * day)));
    GXColor hl = gxlerp(gxc(30, 40, 70), gxc(e.skyHorizon.r / 2 + 40, e.skyHorizon.g / 2 + 50, e.skyHorizon.b / 2 + 55), day);
    GX_SetTevKColor(GX_KCOLOR2, hl);
    GX_SetTevKColor(GX_KCOLOR3, tint);
    GX_SetNumTevStages(3);
    // stage 0: vertex colour * tint * 2
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K3);
    GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_RASC, GX_CC_KONST, GX_CC_ZERO);
    GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
    GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    // stage 1/2: + wave layers * highlight
    for (int s = 1; s <= 2; s++) {
        GX_SetTevOrder((u8)s, (u8)(GX_TEXCOORD0 + s - 1), GX_TEXMAP0, GX_COLORNULL);
        GX_SetTevKColorSel((u8)s, GX_TEV_KCSEL_K2);
        GX_SetTevColorIn((u8)s, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_CPREV);
        GX_SetTevAlphaIn((u8)s, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
        GX_SetTevColorOp((u8)s, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp((u8)s, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    }
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetZCompLoc(GX_TRUE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetFog(GX_FOG_PERSP_LIN, e.fogStart, e.fogEnd, gfx::camera().nearZ, gfx::camera().farZ, e.fogColor);
    tex::bind(g_wave, GX_TEXMAP0);
    setFmt();
    vtxDesc();
    for (int i = 0; i < g_num; i++) {
        WChunk &c = g_chunks[i];
        if (!gfx::visible(c.center, c.radius)) continue;
        if (c.size) GX_CallDispList(c.dl, c.size);
    }
    gfx::invalidateState();
}

void drawRipple(const Vec3 &p, f32 radius, f32 alpha) {
    if (!g_ring) return;
    gfx::setShade(SH_TEX | SH_VCOL | SH_BLEND | SH_DOUBLESIDED);
    tex::bind(g_ring, GX_TEXMAP0);
    gfx::loadWorld(Mat34::identity());
    f32 y = g_level + 0.04f;
    u8 a = (u8)(hvSaturate(alpha) * 255);
    gfx::beginImm(GX_QUADS, 4, true);
    GX_Position3f32(p.x - radius, y, p.z - radius); GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(0, 0);
    GX_Position3f32(p.x + radius, y, p.z - radius); GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(1, 0);
    GX_Position3f32(p.x + radius, y, p.z + radius); GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(1, 1);
    GX_Position3f32(p.x - radius, y, p.z + radius); GX_Color4u8(255, 255, 255, a); GX_TexCoord2f32(0, 1);
    GX_End();
}

}  // namespace water

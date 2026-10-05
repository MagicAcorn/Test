#include "gfx/terrain.h"
#include "game/world.h"
#include "gfx/texture.h"

namespace {

const int CH = 16;            // cells per chunk side
const int POS_FRAC = 5;       // s16 positions in 1/32 units
const int MAX_CHUNKS = 16 * 16;
const f32 LOD_DIST = 95.0f;
const f32 SKIRT = 2.5f;

struct Chunk {
    Vec3 center;
    f32 radius;
    void *dl[2];
    u32 dlSize[2];
};

const World *g_w;
int g_nc = 0;                  // chunks per side
int g_n = 0;                   // vertices per side
Chunk g_chunks[MAX_CHUNKS];
s16 *g_pos;                    // [n*n + skirts] * 3
s8 *g_nrm;                     // [n*n] * 3
const u8 *g_clr;               // world colour array (RGBA8)
u16 *g_skirtIndex;             // per vertex: index of skirt copy (0 if none)
int g_drawn = 0;
const Texture *g_detail;

// ------------------------------------------------------------- grass
const int GRASS_SLOTS = 24;
const f32 GRASS_DIST = 48.0f;
struct GrassChunk {
    int cx = -1, cz = -1;
    void *dl = nullptr;
    u32 size = 0;
    u32 lastUsed = 0;
};
GrassChunk g_grass[GRASS_SLOTS];
const u32 GRASS_DL_CAP = 40 * 1024;
u32 g_frame = 0;
const Texture *g_foliage;

inline int vid(int x, int z) { return z * g_n + x; }

void emit(int x, int z, bool skirt) {
    int v = vid(x, z);
    u16 pi = skirt ? g_skirtIndex[v] : (u16)v;
    GX_Position1x16(pi);
    GX_Normal1x16((u16)v);
    GX_Color1x16((u16)v);
}

void buildChunk(int cx, int cz) {
    Chunk &c = g_chunks[cz * g_nc + cx];
    f32 cell = g_w->cell();
    f32 x0 = cx * CH * cell, z0 = cz * CH * cell;
    f32 minY = 1e9f, maxY = -1e9f;
    for (int z = 0; z <= CH; z++)
        for (int x = 0; x <= CH; x++) {
            f32 h = g_w->vertexHeight(cx * CH + x, cz * CH + z);
            minY = hvMin(minY, h);
            maxY = hvMax(maxY, h);
        }
    c.center = Vec3(x0 + CH * cell * 0.5f, (minY + maxY) * 0.5f, z0 + CH * cell * 0.5f);
    f32 hy = (maxY - minY) * 0.5f + SKIRT;
    f32 hxz = CH * cell * 0.5f;
    c.radius = sqrtf(hxz * hxz * 2 + hy * hy);
    for (int lod = 0; lod < 2; lod++) {
        int step = lod == 0 ? 1 : 2;
        int cells = CH / step;
        u32 cap = (u32)(((cells + 4) * (3 + (cells + 1) * 2 * 6) + 128 + 31) & ~31);
        void *buf = hvAlignedAlloc(cap);
        HV_BEGIN_DL(buf, cap);
        int bx = cx * CH, bz = cz * CH;
        for (int r = 0; r < cells; r++) {
            int za = bz + r * step, zb = za + step;
            GX_Begin(GX_TRIANGLESTRIP, VFMT_TERRAIN, (u16)((cells + 1) * 2));
            for (int k = 0; k <= cells; k++) {
                int x = bx + k * step;
                emit(x, zb, false);
                emit(x, za, false);
            }
            GX_End();
        }
        // skirts: for each edge, points ordered left->right as seen from outside
        for (int e = 0; e < 4; e++) {
            GX_Begin(GX_TRIANGLESTRIP, VFMT_TERRAIN, (u16)((cells + 1) * 2));
            for (int k = 0; k <= cells; k++) {
                int x, z;
                switch (e) {
                    case 0: x = bx + CH - k * step; z = bz; break;          // north edge, x decreasing
                    case 1: x = bx + k * step; z = bz + CH; break;          // south edge, x increasing
                    case 2: x = bx; z = bz + k * step; break;               // west edge, z increasing
                    default: x = bx + CH; z = bz + CH - k * step; break;    // east edge, z decreasing
                }
                emit(x, z, true);
                emit(x, z, false);
            }
            GX_End();
        }
        c.dlSize[lod] = hvEndDispList();
        c.dl[lod] = buf;
        if (!c.dlSize[lod]) hvLog("terrain: display list overflow (chunk %d,%d lod %d)", cx, cz, lod);
    }
}

void setupVtx() {
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_INDEX16);
    GX_SetVtxDesc(GX_VA_NRM, GX_INDEX16);
    GX_SetVtxDesc(GX_VA_CLR0, GX_INDEX16);
    GX_SetArray(GX_VA_POS, g_pos, 6);
    GX_SetArray(GX_VA_NRM, g_nrm, 3);
    GX_SetArray(GX_VA_CLR0, (void *)g_clr, 4);
    gfx::vcdChanged();
}

// --------------------------------------------------------------- grass
inline u32 hashi(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

void grassVtxDesc() {
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_PTNMTXIDX, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_NRM, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    gfx::vcdChanged();
}

void buildGrass(GrassChunk &g, int cx, int cz) {
    if (!g.dl) g.dl = hvAlignedAlloc(GRASS_DL_CAP);
    g.cx = cx;
    g.cz = cz;
    f32 cell = g_w->cell();
    f32 x0 = cx * CH * cell, z0 = cz * CH * cell;
    f32 span = CH * cell;
    // collect tufts first so we know the vertex count
    struct Tuft { f32 x, y, z, w, h, a; u8 r, gg, b; u8 mtx; u8 kind; s8 nx, ny, nz; };
    static Tuft tufts[260];
    int nt = 0;
    u32 seed = hashi((u32)(cx * 7919 + cz * 104729));
    for (int i = 0; i < 520 && nt < 260; i++) {
        seed = hashi(seed + (u32)i);
        f32 fx = (seed & 0xFFFF) / 65535.0f, fz = (seed >> 16) / 65535.0f;
        f32 x = x0 + fx * span, z = z0 + fz * span;
        int ix = hvClamp((int)(x / cell + 0.5f), 0, g_n - 1), iz = hvClamp((int)(z / cell + 0.5f), 0, g_n - 1);
        const u8 *c = g_w->vertexColor(ix, iz);
        u32 r2 = hashi(seed ^ 0x9E3779B9u);
        if ((r2 & 255) >= c[3]) continue;   // density mask
        f32 y = g_w->terrainHeight(x, z);
        if (y < g_w->waterLevel() + 0.3f) continue;
        Tuft &t = tufts[nt++];
        t.x = x; t.y = y - 0.05f; t.z = z;
        f32 sz = 0.8f + ((r2 >> 8) & 255) / 255.0f * 0.7f;
        t.w = 1.5f * sz;
        t.h = 1.05f * sz;
        t.a = ((r2 >> 16) & 255) / 255.0f * HV_PI;
        u32 kindr = (r2 >> 24) & 255;
        t.kind = kindr < 18 ? 2 : (kindr < 30 ? 3 : (kindr & 1));
        // tint from the terrain, slightly brighter/yellower
        t.r = (u8)hvMin(255, c[0] + 18);
        t.gg = (u8)hvMin(255, c[1] + 22);
        t.b = (u8)hvMin(255, c[2] + 6);
        t.mtx = (u8)(1 + (r2 % 3));
        Vec3 n = g_w->terrainNormal(x, z);
        t.nx = (s8)(n.x * 64); t.ny = (s8)(n.y * 64); t.nz = (s8)(n.z * 64);
    }
    g.size = 0;
    if (nt == 0) return;
    grassVtxDesc();
    GX_SetVtxAttrFmt(VFMT_PART, GX_VA_POS, GX_POS_XYZ, GX_S16, POS_FRAC);
    GX_SetVtxAttrFmt(VFMT_PART, GX_VA_NRM, GX_NRM_XYZ, GX_S8, 0);
    GX_SetVtxAttrFmt(VFMT_PART, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(VFMT_PART, GX_VA_TEX0, GX_TEX_ST, GX_U8, 2);
    HV_BEGIN_DL(g.dl, GRASS_DL_CAP);
    GX_Begin(GX_QUADS, VFMT_PART, (u16)(nt * 8));
    const f32 S = (f32)(1 << POS_FRAC);
    for (int i = 0; i < nt; i++) {
        const Tuft &t = tufts[i];
        // atlas cell (2x2): u0,v0 in quarter units (frac 2 -> 4 = 1.0)
        u8 u0 = (t.kind & 1) ? 2 : 0, v0 = (t.kind & 2) ? 2 : 0;
        for (int q = 0; q < 2; q++) {
            f32 a = t.a + q * HV_PI * 0.5f;
            f32 dx = cosf(a) * t.w * 0.5f, dz = sinf(a) * t.w * 0.5f;
            f32 px[4] = {t.x - dx, t.x + dx, t.x + dx, t.x - dx};
            f32 pz[4] = {t.z - dz, t.z + dz, t.z + dz, t.z - dz};
            f32 py[4] = {t.y, t.y, t.y + t.h, t.y + t.h};
            u8 uu[4] = {u0, (u8)(u0 + 2), (u8)(u0 + 2), u0};
            u8 vv[4] = {(u8)(v0 + 2), (u8)(v0 + 2), v0, v0};
            for (int k = 0; k < 4; k++) {
                GX_MatrixIndex1x8(k >= 2 ? (u8)(t.mtx * 3) : 0);
                GX_Position3s16((s16)(px[k] * S), (s16)(py[k] * S), (s16)(pz[k] * S));
                GX_Normal3s8(t.nx, t.ny, t.nz);
                u8 sh = k >= 2 ? 255 : 200;   // darker at the roots
                GX_Color4u8((u8)(t.r * sh / 255), (u8)(t.gg * sh / 255), (u8)(t.b * sh / 255), 255);
                GX_TexCoord2u8(uu[k], vv[k]);
            }
        }
    }
    GX_End();
    g.size = hvEndDispList();
}

}  // namespace

namespace terrain {

int chunksDrawn() { return g_drawn; }

void build(const World &w) {
    g_w = &w;
    g_n = w.gridN();
    g_nc = (g_n - 1) / CH;
    int nv = g_n * g_n;
    // skirt copies for vertices on chunk border lines
    g_skirtIndex = new u16[nv];
    memset(g_skirtIndex, 0, sizeof(u16) * nv);
    int ns = 0;
    for (int z = 0; z < g_n; z++)
        for (int x = 0; x < g_n; x++)
            if (x % CH == 0 || z % CH == 0) g_skirtIndex[vid(x, z)] = (u16)(nv + ns++);
    g_pos = (s16 *)hvAlignedAlloc(sizeof(s16) * 3 * (nv + ns));
    g_nrm = (s8 *)hvAlignedAlloc(3 * nv + 32);
    f32 cell = w.cell();
    const f32 S = (f32)(1 << POS_FRAC);
    for (int z = 0; z < g_n; z++)
        for (int x = 0; x < g_n; x++) {
            int v = vid(x, z);
            f32 h = w.vertexHeight(x, z);
            s16 *p = g_pos + v * 3;
            wrU16(&p[0], (u16)(s16)(x * cell * S));
            wrU16(&p[1], (u16)(s16)(h * S));
            wrU16(&p[2], (u16)(s16)(z * cell * S));
            if (x % CH == 0 || z % CH == 0) {
                s16 *q = g_pos + g_skirtIndex[v] * 3;
                wrU16(&q[0], (u16)(s16)(x * cell * S));
                wrU16(&q[1], (u16)(s16)((h - SKIRT) * S));
                wrU16(&q[2], (u16)(s16)(z * cell * S));
            }
            f32 dx = w.vertexHeight(x + 1, z) - w.vertexHeight(x - 1, z);
            f32 dz = w.vertexHeight(x, z + 1) - w.vertexHeight(x, z - 1);
            Vec3 n = normalize(Vec3(-dx, 2 * cell, -dz));
            g_nrm[v * 3 + 0] = (s8)(n.x * 64);
            g_nrm[v * 3 + 1] = (s8)(n.y * 64);
            g_nrm[v * 3 + 2] = (s8)(n.z * 64);
        }
    g_clr = w.vertexColor(0, 0);
    DCFlushRange(g_pos, sizeof(s16) * 3 * (nv + ns));
    DCFlushRange(g_nrm, 3 * nv + 32);

    GX_SetVtxAttrFmt(VFMT_TERRAIN, GX_VA_POS, GX_POS_XYZ, GX_S16, POS_FRAC);
    GX_SetVtxAttrFmt(VFMT_TERRAIN, GX_VA_NRM, GX_NRM_XYZ, GX_S8, 0);
    GX_SetVtxAttrFmt(VFMT_TERRAIN, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    setupVtx();
    for (int cz = 0; cz < g_nc; cz++)
        for (int cx = 0; cx < g_nc; cx++) buildChunk(cx, cz);
    g_detail = tex::get("tx/detail");
    g_foliage = tex::get("tx/foliage");
    hvLog("terrain: %d chunks, %d vertices (+%d skirt)", g_nc * g_nc, nv, ns);
}

void draw() {
    const Camera &cam = gfx::camera();
    g_drawn = 0;
    setupVtx();
    GX_SetVtxAttrFmt(VFMT_TERRAIN, GX_VA_POS, GX_POS_XYZ, GX_S16, POS_FRAC);
    gfx::loadWorld(Mat34::identity());
    // detail texture tiling: world xz / 7
    Mat34 tm;
    memset(&tm, 0, sizeof(tm));
    tm.m[0][0] = 1.0f / 7.0f;
    tm.m[1][2] = 1.0f / 7.0f;
    GX_LoadTexMtxImm(tm.m, GX_TEXMTX0, GX_MTX2x4);
    gfx::setShade(SH_TEX | SH_TEXPOS | SH_VCOL | SH_LIT | SH_DETAIL2X);
    tex::bind(g_detail, GX_TEXMAP0);
    for (int i = 0; i < g_nc * g_nc; i++) {
        Chunk &c = g_chunks[i];
        if (!gfx::visible(c.center, c.radius)) continue;
        f32 d = distXZ(c.center, cam.eye);
        int lod = d > LOD_DIST ? 1 : 0;
        if (c.dlSize[lod]) GX_CallDispList(c.dl[lod], c.dlSize[lod]);
        g_drawn++;
    }
}

void drawGrass(f32 time) {
    if (!g_foliage) return;
    g_frame++;
    const Camera &cam = gfx::camera();
    f32 cell = g_w->cell();
    f32 span = CH * cell;
    int ccx = (int)(cam.target.x / span), ccz = (int)(cam.target.z / span);
    // wind matrices: tops of tufts are offset by a swaying translation
    Mat34 view = gfx::view();
    GX_LoadPosMtxImm(view.m, GX_PNMTX0);
    GX_LoadNrmMtxImm(view.m, GX_PNMTX0);
    for (int k = 1; k <= 3; k++) {
        f32 ph = time * (1.7f + k * 0.23f) + k * 2.1f;
        f32 sway = 0.16f + 0.08f * sinf(time * 0.37f);
        Vec3 off(sinf(ph) * sway, 0, cosf(ph * 0.8f) * sway * 0.6f);
        Mat34 w = view * Mat34::translation(off);
        GX_LoadPosMtxImm(w.m, GX_PNMTX0 + 3 * k);
        GX_LoadNrmMtxImm(view.m, GX_PNMTX0 + 3 * k);
    }
    gfx::setShade(SH_TEX | SH_VCOL | SH_LIT | SH_ALPHATEST | SH_DOUBLESIDED | SH_DETAIL2X);
    tex::bind(g_foliage, GX_TEXMAP0);
    GX_SetVtxAttrFmt(VFMT_PART, GX_VA_POS, GX_POS_XYZ, GX_S16, POS_FRAC);
    GX_SetVtxAttrFmt(VFMT_PART, GX_VA_NRM, GX_NRM_XYZ, GX_S8, 0);
    GX_SetVtxAttrFmt(VFMT_PART, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(VFMT_PART, GX_VA_TEX0, GX_TEX_ST, GX_U8, 2);
    grassVtxDesc();
    int built = 0;
    for (int dz = -2; dz <= 2; dz++)
        for (int dx = -2; dx <= 2; dx++) {
            int cx = ccx + dx, cz = ccz + dz;
            if (cx < 0 || cz < 0 || cx >= g_nc || cz >= g_nc) continue;
            Vec3 c((cx + 0.5f) * span, cam.target.y, (cz + 0.5f) * span);
            if (distXZ(c, cam.target) > GRASS_DIST + span * 0.71f) continue;
            if (!gfx::visible(c + Vec3(0, 2, 0), span * 0.75f)) continue;
            GrassChunk *slot = nullptr;
            for (auto &g : g_grass)
                if (g.cx == cx && g.cz == cz) slot = &g;
            if (!slot) {
                if (built >= 2) continue;   // spread rebuilds over frames
                slot = &g_grass[0];
                for (auto &g : g_grass)
                    if (g.lastUsed < slot->lastUsed) slot = &g;
                buildGrass(*slot, cx, cz);
                grassVtxDesc();
                built++;
            }
            slot->lastUsed = g_frame;
            if (slot->size) GX_CallDispList(slot->dl, slot->size);
        }
    GX_SetCurrentMtx(GX_PNMTX0);
}

}  // namespace terrain

#include "gfx/texture.h"
#include "core/pak.h"

namespace {
const int MAX_TEX = 256;
Texture g_tex[MAX_TEX];
int g_count = 0;
}  // namespace

namespace tex {

void initAll() {
    g_count = 0;
    for (u32 i = 0; i < g_pak.count(); i++) {
        u32 hash, type, size;
        const u8 *d;
        g_pak.entry(i, &hash, &type, &d, &size);
        if (type != ASSET_TEX || g_count >= MAX_TEX) continue;
        Texture &t = g_tex[g_count++];
        t.hash = hash;
        t.w = rdU16(d + 4);
        t.h = rdU16(d + 6);
        t.fmt = d[8];
        t.levels = d[9];
        u8 wrapS = d[10], wrapT = d[11], minF = d[12], magF = d[13];
        u32 dataOff = rdU32(d + 16);
        GX_InitTexObj(&t.obj, (void *)(d + dataOff), t.w, t.h, t.fmt, wrapS, wrapT, t.levels > 1 ? GX_TRUE : GX_FALSE);
        if (t.levels > 1) {
            GX_InitTexObjLOD(&t.obj, minF == 1 ? GX_LIN_MIP_LIN : minF, magF, 0.0f, (f32)(t.levels - 1), 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
        } else {
            GX_InitTexObjFilterMode(&t.obj, minF, magF);
        }
    }
    // the list is in pak (hash) order, so lookups can binary search
}

const Texture *get(u32 hash) {
    int lo = 0, hi = g_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (g_tex[mid].hash == hash) return &g_tex[mid];
        if (g_tex[mid].hash < hash) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

// Only tex::bind loads maps 0-6, so redundant loads can be skipped.
static const Texture *s_bound[8];
static u32 s_loads;

void bind(const Texture *t, u8 map) {
    if (!t) return;
    if (map < 8 && s_bound[map] == t) return;
    if (map < 8) s_bound[map] = t;
    GX_LoadTexObj((GXTexObj *)&t->obj, map);
    s_loads++;
}

void resetBindings() {
    for (auto &b : s_bound) b = nullptr;
}

u32 takeLoadCount() {
    u32 n = s_loads;
    s_loads = 0;
    return n;
}

}  // namespace tex

#include "game/scene.h"
#include <stdio.h>
#include <stdlib.h>
#include "game/world.h"
#include "gfx/sky.h"
#include "gfx/terrain.h"
#include "gfx/texture.h"
#include "gfx/water.h"

namespace {
u32 g_drawn = 0;
const Texture *g_glow;
}

namespace scene {

u32 objectsDrawn() { return g_drawn; }

void init() {
    terrain::build(g_world);
    water::build(g_world);
    sky::init();
    g_glow = tex::get("tx/glow");
}

void drawSkyAndTerrain(f32 time) {
    (void)time;
    sky::draw();
    terrain::draw();
}

#ifdef HV_PC
// HV_OBJSTATS=<frame>: list the models costing the most vertices on that frame
struct ObjStat {
    const Model *m;
    u32 verts, count;
};
static ObjStat s_objStats[400];
static int s_objStatN = 0;
#endif

void drawObjects() {
    const Camera &cam = gfx::camera();
    g_drawn = 0;
    f32 fogEnd = gfx::env().fogEnd;
#ifdef HV_PC
    static int statFrame = getenv("HV_OBJSTATS") ? atoi(getenv("HV_OBJSTATS")) : -1;
    static int frameNo = 0;
    bool stats = ++frameNo == statFrame;
    s_objStatN = 0;
#endif
    for (int i = 0; i < g_world.numObjects(); i++) {
        const WorldObject &o = g_world.object(i);
        if (!o.model) continue;
        f32 d = distXZ(o.cullCenter, cam.eye) - o.cullRadius;
        f32 maxD = o.small ? 75.0f : (o.foliage ? fogEnd - 10.0f : fogEnd + 10.0f);
        if (d > maxD) continue;
        if (!gfx::visible(o.cullCenter, o.cullRadius)) continue;
#ifdef HV_PC
        GXEmuStats st0, st1;
        if (stats) gxemu_get_stats(&st0);
#endif
        gfx::drawModel(o.model, o.xf, gxc(255, 255, 255), o.foliage ? SH_RIM : 0);
#ifdef HV_PC
        if (stats) {
            gxemu_get_stats(&st1);
            int k = 0;
            while (k < s_objStatN && s_objStats[k].m != o.model) k++;
            if (k == s_objStatN && k < 400) s_objStats[s_objStatN++] = {o.model, 0, 0};
            if (k < 400) {
                s_objStats[k].verts += st1.verts - st0.verts;
                s_objStats[k].count++;
            }
        }
#endif
        g_drawn++;
    }
#ifdef HV_PC
    if (stats) {
        for (int a = 0; a < s_objStatN; a++)
            for (int b = a + 1; b < s_objStatN; b++)
                if (s_objStats[b].verts > s_objStats[a].verts) {
                    ObjStat t = s_objStats[a];
                    s_objStats[a] = s_objStats[b];
                    s_objStats[b] = t;
                }
        for (int a = 0; a < s_objStatN && a < 25; a++)
            printf("[objstats] %08x x%-3u verts %6u (%u each)\n", s_objStats[a].m->hash, s_objStats[a].count, s_objStats[a].verts,
                   s_objStats[a].verts / s_objStats[a].count);
    }
#endif
}

void drawWaterAndGrass(f32 time) {
    terrain::drawGrass(time);
    water::draw(time);
}

void drawLightGlows(f32 time) {
    f32 night = gfx::env().nightness;
    if (night < 0.15f || !g_glow) return;
    const Camera &cam = gfx::camera();
    gfx::setShade(SH_TEX | SH_VCOL | SH_ADDITIVE | SH_DOUBLESIDED | SH_NOFOG);
    tex::bind(g_glow, GX_TEXMAP0);
    gfx::loadWorld(Mat34::identity());
    Vec3 right(cam.view.m[0][0], cam.view.m[0][1], cam.view.m[0][2]);
    Vec3 up(cam.view.m[1][0], cam.view.m[1][1], cam.view.m[1][2]);
    for (int i = 0; i < g_world.numMarkers(); i++) {
        const Marker &m = g_world.marker(i);
        if (m.kind != MK_LIGHT) continue;
        if (distXZ(m.pos, cam.eye) > 140) continue;
        if (!gfx::visible(m.pos, 3)) continue;
        f32 flick = 0.85f + 0.15f * sinf(time * 9.0f + i * 1.7f) * sinf(time * 5.3f + i);
        f32 size;
        GXColor c;
        switch (m.id) {
            case 3: size = 4.2f; c = gxc(255, 150, 60); break;      // fire
            case 5: size = 1.6f; c = gxc(90, 170, 255); break;      // glow mushrooms / crystals
            case 6: size = 1.4f; c = gxc(255, 200, 120); break;     // candles
            default: size = 2.6f; c = gxc(255, 196, 110); break;    // lanterns, torches
        }
        u8 a = (u8)(hvSaturate((night - 0.15f) * 1.6f) * 200 * flick);
        Vec3 r = right * size, u = up * size;
        Vec3 p = m.pos;
        gfx::beginImm(GX_QUADS, 4, true);
        Vec3 q0 = p - r + u, q1 = p + r + u, q2 = p + r - u, q3 = p - r - u;
        GX_Position3f32(q0.x, q0.y, q0.z); GX_Color4u8(c.r, c.g, c.b, a); GX_TexCoord2f32(0, 0);
        GX_Position3f32(q1.x, q1.y, q1.z); GX_Color4u8(c.r, c.g, c.b, a); GX_TexCoord2f32(1, 0);
        GX_Position3f32(q2.x, q2.y, q2.z); GX_Color4u8(c.r, c.g, c.b, a); GX_TexCoord2f32(1, 1);
        GX_Position3f32(q3.x, q3.y, q3.z); GX_Color4u8(c.r, c.g, c.b, a); GX_TexCoord2f32(0, 1);
        GX_End();
    }
}

}  // namespace scene

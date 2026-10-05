#include "game/scene.h"
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

void drawObjects() {
    const Camera &cam = gfx::camera();
    g_drawn = 0;
    f32 fogEnd = gfx::env().fogEnd;
    for (int i = 0; i < g_world.numObjects(); i++) {
        const WorldObject &o = g_world.object(i);
        if (!o.model) continue;
        f32 d = distXZ(o.cullCenter, cam.eye) - o.cullRadius;
        f32 maxD = o.small ? 75.0f : (o.foliage ? fogEnd - 10.0f : fogEnd + 10.0f);
        if (d > maxD) continue;
        if (!gfx::visible(o.cullCenter, o.cullRadius)) continue;
        gfx::drawModel(o.model, o.xf, gxc(255, 255, 255), o.foliage ? SH_RIM : 0);
        g_drawn++;
    }
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

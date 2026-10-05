// Particles (pooled billboards) and floating combat/XP text.
#include <stdio.h>
#include "game/game.h"
#include "game/world.h"
#include "gfx/texture.h"
#include "ui/ui.h"

namespace fx {

namespace {
struct Particle {
    Vec3 p, v;
    f32 life, maxLife;
    f32 size, grow;
    GXColor c;
    u8 tex;      // 0 dot, 1 spark, 2 leaf, 3 flame, 4 smoke(dot), 5 glow
    bool additive;
    f32 gravity;
    f32 spin;
};
const int MAX_P = 700;
Particle s_p[MAX_P];
int s_count = 0;

struct FloatText {
    Vec3 p;
    char text[32];
    GXColor c;
    f32 life;
    f32 scale;
};
FloatText s_ft[24];

const Texture *s_tex[6];
u32 s_rng = 0x2468ACEu;
inline f32 frand() {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return (s_rng & 0xFFFFFF) / 16777216.0f;
}
inline f32 srand1() { return frand() * 2 - 1; }

Particle *spawn() {
    if (s_count >= MAX_P) {
        // overwrite the oldest-ish particle
        int i = (int)(frand() * MAX_P) % MAX_P;
        return &s_p[i];
    }
    return &s_p[s_count++];
}

// ambient emitters (fires, chimneys, fireflies)
f32 s_ambientT = 0;
bool s_hearthLit = false;
}  // namespace

void init() {
    s_count = 0;
    s_tex[0] = tex::get("tx/dot");
    s_tex[1] = tex::get("tx/spark");
    s_tex[2] = tex::get("tx/leaf");
    s_tex[3] = tex::get("tx/flame");
    s_tex[4] = tex::get("tx/dot");
    s_tex[5] = tex::get("tx/glow");
    for (auto &f : s_ft) f.life = 0;
}

void hearthFire(bool lit) { s_hearthLit = lit; }

void burst(const Vec3 &pos, int kind, int count) {
    for (int i = 0; i < count; i++) {
        Particle &q = *spawn();
        q.p = pos;
        q.spin = srand1() * 3;
        q.grow = 0;
        q.gravity = 0;
        q.additive = false;
        switch (kind) {
            case FX_SPARK:
                q.v = Vec3(srand1() * 2.5f, frand() * 3 + 1, srand1() * 2.5f);
                q.life = q.maxLife = 0.5f + frand() * 0.5f;
                q.size = 0.25f + frand() * 0.2f;
                q.c = gxc(255, 230, 150, 255);
                q.tex = 1;
                q.additive = true;
                q.gravity = 3;
                break;
            case FX_LEAF:
                q.v = Vec3(srand1() * 2.5f, frand() * 2.5f + 0.5f, srand1() * 2.5f);
                q.life = q.maxLife = 1.2f + frand() * 0.8f;
                q.size = 0.22f + frand() * 0.12f;
                q.c = frand() < 0.5f ? gxc(120, 180, 60, 255) : gxc(170, 140, 70, 255);
                q.tex = 2;
                q.gravity = 2.5f;
                break;
            case FX_DUST:
                q.v = Vec3(srand1() * 1.5f, frand() * 1.2f, srand1() * 1.5f);
                q.life = q.maxLife = 0.8f + frand() * 0.6f;
                q.size = 0.5f + frand() * 0.4f;
                q.grow = 1.2f;
                q.c = gxc(190, 175, 150, 150);
                q.tex = 0;
                break;
            case FX_ROCK:
                q.v = Vec3(srand1() * 3, frand() * 3.5f + 1, srand1() * 3);
                q.life = q.maxLife = 0.7f + frand() * 0.4f;
                q.size = 0.18f + frand() * 0.12f;
                q.c = gxc(150, 140, 130, 255);
                q.tex = 0;
                q.gravity = 9;
                break;
            case FX_SPLASH:
                q.v = Vec3(srand1() * 1.8f, frand() * 4 + 1.5f, srand1() * 1.8f);
                q.life = q.maxLife = 0.6f + frand() * 0.4f;
                q.size = 0.25f + frand() * 0.15f;
                q.c = gxc(210, 240, 255, 220);
                q.tex = 0;
                q.gravity = 9;
                break;
            case FX_LEVEL: {
                f32 a = frand() * HV_TAU;
                f32 r = 0.8f + frand() * 0.4f;
                q.p = pos + Vec3(cosf(a) * r, -0.8f + frand() * 0.3f, sinf(a) * r);
                q.v = Vec3(-sinf(a) * 1.2f, 2.2f + frand() * 2.0f, cosf(a) * 1.2f);
                q.life = q.maxLife = 1.4f + frand() * 0.6f;
                q.size = 0.3f + frand() * 0.2f;
                q.c = gxc(255, 220, 120, 255);
                q.tex = 1;
                q.additive = true;
                break;
            }
            case FX_MAGIC:
                q.v = Vec3(srand1() * 1.5f, frand() * 2 + 0.5f, srand1() * 1.5f);
                q.life = q.maxLife = 0.7f + frand() * 0.5f;
                q.size = 0.3f + frand() * 0.25f;
                q.c = gxc(170, 130, 255, 255);
                q.tex = 1;
                q.additive = true;
                break;
            case FX_FIRE:
                q.p = pos + Vec3(srand1() * 0.3f, 0, srand1() * 0.3f);
                q.v = Vec3(srand1() * 0.4f, 1.5f + frand() * 1.5f, srand1() * 0.4f);
                q.life = q.maxLife = 0.5f + frand() * 0.4f;
                q.size = 0.5f + frand() * 0.4f;
                q.grow = -0.6f;
                q.c = gxc(255, 150 + (u8)(frand() * 60), 60, 255);
                q.tex = 3;
                q.additive = true;
                break;
            case FX_HEAL:
                q.p = pos + Vec3(srand1() * 0.7f, srand1() * 0.6f, srand1() * 0.7f);
                q.v = Vec3(0, 1.2f + frand(), 0);
                q.life = q.maxLife = 1.0f + frand() * 0.5f;
                q.size = 0.25f + frand() * 0.15f;
                q.c = gxc(130, 255, 150, 255);
                q.tex = 1;
                q.additive = true;
                break;
            case FX_HIT:
                q.v = Vec3(srand1() * 4, srand1() * 3 + 1, srand1() * 4);
                q.life = q.maxLife = 0.25f + frand() * 0.2f;
                q.size = 0.35f + frand() * 0.2f;
                q.c = gxc(255, 255, 255, 255);
                q.tex = 1;
                q.additive = true;
                break;
            case FX_GOLD:
                q.v = Vec3(srand1() * 2, frand() * 4 + 2, srand1() * 2);
                q.life = q.maxLife = 1.0f + frand() * 0.6f;
                q.size = 0.25f + frand() * 0.2f;
                q.c = gxc(255, 210, 90, 255);
                q.tex = 1;
                q.additive = true;
                q.gravity = 4;
                break;
            case FX_SMOKE:
                q.v = Vec3(srand1() * 0.3f + 0.4f, 1.0f + frand() * 0.6f, srand1() * 0.3f);
                q.life = q.maxLife = 3.0f + frand() * 1.5f;
                q.size = 0.6f + frand() * 0.4f;
                q.grow = 0.7f;
                q.c = gxc(200, 200, 205, 90);
                q.tex = 0;
                break;
            case FX_ICE:
                q.v = Vec3(srand1() * 2, srand1() * 2, srand1() * 2);
                q.life = q.maxLife = 0.5f + frand() * 0.4f;
                q.size = 0.3f + frand() * 0.2f;
                q.c = gxc(170, 230, 255, 255);
                q.tex = 1;
                q.additive = true;
                break;
            case FX_BOLT:
                q.v = Vec3(srand1() * 5, srand1() * 5, srand1() * 5);
                q.life = q.maxLife = 0.2f + frand() * 0.2f;
                q.size = 0.3f + frand() * 0.3f;
                q.c = gxc(255, 250, 160, 255);
                q.tex = 1;
                q.additive = true;
                break;
        }
    }
}

void floatText(const Vec3 &p, const char *text, GXColor c, f32 scale) {
    FloatText *slot = &s_ft[0];
    for (auto &f : s_ft)
        if (f.life <= 0) {
            slot = &f;
            break;
        }
    // lift above any fresh number at the same spot so they don't stack
    Vec3 at = p;
    for (const auto &f : s_ft)
        if (&f != slot && f.life > 1.0f && distXZ(f.p, at) < 1.2f && fabsf(f.p.y - at.y) < 0.4f) at.y = f.p.y + 0.45f;
    at.x += (frand() - 0.5f) * 0.4f;
    slot->p = at;
    snprintf(slot->text, sizeof(slot->text), "%s", text);
    slot->c = c;
    slot->life = 1.4f;
    slot->scale = scale;
}

void update(f32 dt) {
    for (int i = 0; i < s_count;) {
        Particle &q = s_p[i];
        q.life -= dt;
        if (q.life <= 0) {
            s_p[i] = s_p[--s_count];
            continue;
        }
        q.v.y -= q.gravity * dt;
        if (q.tex == 2) {
            // leaves flutter
            q.v.x += sinf(q.life * 5 + q.spin) * dt * 2;
            q.v = q.v * (1.0f - dt * 1.5f);
        }
        q.p += q.v * dt;
        q.size = hvMax(0.02f, q.size + q.grow * dt);
        i++;
    }
    for (auto &f : s_ft)
        if (f.life > 0) {
            f.life -= dt;
            f.p.y += dt * 1.1f;
        }
    // ambient emitters
    s_ambientT += dt;
    if (s_ambientT > 0.05f) {
        s_ambientT = 0;
        const Vec3 &cp = g.player.pos;
        f32 night = gfx::env().nightness;
        for (int i = 0; i < g_world.numMarkers(); i++) {
            const Marker &m = g_world.marker(i);
            if (m.kind == MK_LIGHT && m.id == 3) {
                f32 d = distXZ(m.pos, cp);
                if (d > 70) continue;
                bool hearth = distXZ(m.pos, Vec3(200, 0, 196)) < 1.0f;
                if (hearth && !s_hearthLit) continue;
                int n = hearth ? 3 : 1;
                for (int k = 0; k < n; k++) burst(m.pos + Vec3(0, hearth ? -0.4f : -0.3f, 0), FX_FIRE, 1);
                if (frand() < (hearth ? 0.35f : 0.12f)) burst(m.pos + Vec3(0, 1.5f, 0), FX_SMOKE, 1);
                if (hearth && frand() < 0.3f) burst(m.pos + Vec3(0, 0.6f, 0), FX_SPARK, 1);
            }
        }
        // forest leaves
        if (g.currentRegion == 2 && frand() < 0.5f) {
            Particle &q = *spawn();
            q.p = cp + Vec3(srand1() * 18, 8 + frand() * 4, srand1() * 18);
            q.v = Vec3(0.6f, -0.8f, 0.3f);
            q.life = q.maxLife = 6;
            q.size = 0.22f;
            q.grow = 0;
            q.gravity = 0;
            q.c = frand() < 0.5f ? gxc(150, 180, 70, 255) : gxc(200, 150, 60, 255);
            q.tex = 2;
            q.additive = false;
            q.spin = frand() * 6;
        }
        // fireflies at night near grass
        if (night > 0.5f && frand() < 0.6f && g.currentRegion != 4) {
            Particle &q = *spawn();
            f32 a = frand() * HV_TAU, r = 4 + frand() * 18;
            q.p = Vec3(cp.x + cosf(a) * r, 0, cp.z + sinf(a) * r);
            q.p.y = g_world.groundHeight(q.p.x, q.p.z) + 0.5f + frand() * 2;
            q.v = Vec3(srand1() * 0.4f, srand1() * 0.2f, srand1() * 0.4f);
            q.life = q.maxLife = 3 + frand() * 3;
            q.size = 0.18f;
            q.grow = 0;
            q.gravity = 0;
            q.c = gxc(200, 255, 120, 255);
            q.tex = 5;
            q.additive = true;
            q.spin = 0;
        }
        // will-o-wisps at the barrow
        if (g.currentRegion == 4 && frand() < 0.4f) {
            Particle &q = *spawn();
            f32 a = frand() * HV_TAU, r = 3 + frand() * 16;
            q.p = Vec3(cp.x + cosf(a) * r, 0, cp.z + sinf(a) * r);
            q.p.y = g_world.groundHeight(q.p.x, q.p.z) + 0.3f + frand();
            q.v = Vec3(srand1() * 0.3f, 0.3f, srand1() * 0.3f);
            q.life = q.maxLife = 2.5f;
            q.size = 0.3f;
            q.grow = 0;
            q.gravity = 0;
            q.c = gxc(120, 220, 255, 255);
            q.tex = 5;
            q.additive = true;
            q.spin = 0;
        }
    }
}

void draw() {
    if (!s_count) return;
    const Camera &cam = gfx::camera();
    Vec3 right(cam.view.m[0][0], cam.view.m[0][1], cam.view.m[0][2]);
    Vec3 up(cam.view.m[1][0], cam.view.m[1][1], cam.view.m[1][2]);
    gfx::loadWorld(Mat34::identity());
    // two passes: alpha blended then additive; each texture batched
    for (int pass = 0; pass < 2; pass++) {
        bool add = pass == 1;
        for (int t = 0; t < 6; t++) {
            int n = 0;
            for (int i = 0; i < s_count; i++)
                if (s_p[i].tex == t && s_p[i].additive == add) n++;
            if (!n || !s_tex[t]) continue;
            gfx::setShade(SH_TEX | SH_VCOL | SH_DOUBLESIDED | (add ? SH_ADDITIVE : SH_BLEND));
            tex::bind(s_tex[t], GX_TEXMAP0);
            n = hvMin(n, 16000);
            gfx::beginImm(GX_QUADS, (u16)(n * 4), true);
            int emitted = 0;
            for (int i = 0; i < s_count && emitted < n; i++) {
                const Particle &q = s_p[i];
                if (q.tex != t || q.additive != add) continue;
                emitted++;
                f32 lf = q.life / q.maxLife;
                u8 a = (u8)(q.c.a * hvSaturate(lf * 3.0f) * (t == 5 ? (0.6f + 0.4f * sinf(q.life * 8)) : 1.0f));
                f32 s = q.size;
                f32 rot = q.spin * q.life;
                f32 cr = cosf(rot) * s, sr = sinf(rot) * s;
                Vec3 r = right * cr + up * sr, u = up * cr - right * sr;
                Vec3 q0 = q.p - r + u, q1 = q.p + r + u, q2 = q.p + r - u, q3 = q.p - r - u;
                GX_Position3f32(q0.x, q0.y, q0.z); GX_Color4u8(q.c.r, q.c.g, q.c.b, a); GX_TexCoord2f32(0, 0);
                GX_Position3f32(q1.x, q1.y, q1.z); GX_Color4u8(q.c.r, q.c.g, q.c.b, a); GX_TexCoord2f32(1, 0);
                GX_Position3f32(q2.x, q2.y, q2.z); GX_Color4u8(q.c.r, q.c.g, q.c.b, a); GX_TexCoord2f32(1, 1);
                GX_Position3f32(q3.x, q3.y, q3.z); GX_Color4u8(q.c.r, q.c.g, q.c.b, a); GX_TexCoord2f32(0, 1);
            }
            GX_End();
        }
    }
}

void drawFloatTexts() {
    for (auto &f : s_ft) {
        if (f.life <= 0) continue;
        bool on;
        Vec3 s = screenProject(f.p, &on);
        if (!on || s.z > 60) continue;
        u8 a = (u8)(hvSaturate(f.life * 2.0f) * 255);
        GXColor c = f.c;
        c.a = a;
        f32 pop = f.life > 1.2f ? 1.0f + (f.life - 1.2f) * 2.0f : 1.0f;
        ui::text(FONT_UI, s.x, s.y, f.text, c, AL_CENTER, f.scale * pop);
    }
}

}  // namespace fx

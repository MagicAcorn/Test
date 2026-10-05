#include "gfx/sky.h"
#include "gfx/model.h"
#include "gfx/texture.h"

namespace {

struct Key {
    f32 hour;
    GXColor top, horizon, fog, sun, shadow, rim, disc;
    f32 rim_strength;
};

// Hand-tuned palette across the day (light multipliers: 128 = 1.0x).
const Key KEYS[] = {
    {0.0f, {12, 18, 48}, {34, 46, 90}, {30, 40, 78}, {78, 92, 140}, {44, 52, 92}, {60, 80, 140}, {220, 230, 255}, 0.55f},
    {4.5f, {20, 26, 64}, {58, 62, 108}, {54, 58, 100}, {86, 96, 146}, {48, 54, 98}, {70, 84, 150}, {220, 230, 255}, 0.55f},
    {6.0f, {78, 96, 160}, {246, 170, 120}, {226, 164, 130}, {170, 128, 104}, {84, 72, 104}, {160, 100, 70}, {255, 200, 140}, 0.75f},
    {8.0f, {74, 136, 210}, {196, 218, 232}, {178, 204, 222}, {152, 140, 120}, {84, 88, 116}, {130, 112, 86}, {255, 240, 200}, 0.6f},
    {13.0f, {66, 132, 214}, {184, 216, 236}, {170, 204, 228}, {156, 150, 132}, {86, 92, 118}, {120, 112, 92}, {255, 248, 225}, 0.55f},
    {17.0f, {72, 120, 196}, {214, 206, 196}, {196, 190, 186}, {160, 138, 112}, {84, 84, 112}, {150, 116, 80}, {255, 230, 180}, 0.65f},
    {19.0f, {66, 70, 140}, {250, 140, 90}, {214, 128, 104}, {170, 110, 88}, {80, 64, 100}, {180, 100, 70}, {255, 160, 100}, 0.85f},
    {20.5f, {26, 34, 82}, {92, 72, 120}, {72, 62, 104}, {100, 96, 140}, {52, 54, 96}, {90, 80, 150}, {230, 220, 255}, 0.6f},
    {24.0f, {12, 18, 48}, {34, 46, 90}, {30, 40, 78}, {78, 92, 140}, {44, 52, 92}, {60, 80, 140}, {220, 230, 255}, 0.55f},
};

f32 g_hour = 9.0f;
f32 g_cloudT = 0;
const Model *g_cloudBig, *g_cloudSmall;
const Texture *g_glow, *g_dot;
GXColor g_top, g_horizon;
Vec3 g_sunDirTo;   // direction towards the sun (or moon at night)
Vec3 g_sunPos;     // actual sun direction (for drawing)
f32 g_day = 1;

struct Cloud {
    Vec3 p;
    f32 yaw, scale, speed;
    bool big;
};
Cloud g_clouds[14];

struct Star {
    Vec3 d;
    u8 b;
};
Star g_stars[180];

GXColor lerpC(GXColor a, GXColor b, f32 t) { return gxlerp(a, b, t); }

}  // namespace

namespace sky {

f32 time() { return g_hour; }
f32 dayFactor() { return g_day; }

void init() {
    g_cloudBig = mdl::get("hx/cloud_big");
    g_cloudSmall = mdl::get("hx/cloud_small");
    g_glow = tex::get("tx/glow");
    g_dot = tex::get("tx/dot");
    u32 s = 12345;
    auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; };
    for (auto &c : g_clouds) {
        f32 a = rnd() * HV_TAU;
        // low and far, so they sit on the horizon in the normal play camera
        f32 r = 130 + rnd() * 120;
        c.p = Vec3(192 + cosf(a) * r, 46 + rnd() * 26, 192 + sinf(a) * r);
        c.yaw = rnd() * HV_TAU;
        c.big = rnd() < 0.6f;
        c.scale = (c.big ? 3.2f : 2.4f) * (0.8f + rnd() * 0.6f);   // hex clouds are pre-scaled x8
        c.speed = 1.2f + rnd() * 1.5f;
    }
    for (auto &st : g_stars) {
        f32 a = rnd() * HV_TAU, e = asinf(0.08f + rnd() * 0.92f);
        st.d = Vec3(cosf(a) * cosf(e), sinf(e), sinf(a) * cosf(e));
        st.b = (u8)(120 + rnd() * 135);
    }
}

void setTime(f32 hours) {
    while (hours >= 24.0f) hours -= 24.0f;
    while (hours < 0) hours += 24.0f;
    g_hour = hours;
    int i = 0;
    while (i < HV_ARRAY_COUNT(KEYS) - 2 && KEYS[i + 1].hour <= hours) i++;
    const Key &a = KEYS[i], &b = KEYS[i + 1];
    f32 t = hvSmooth((hours - a.hour) / (b.hour - a.hour));
    Environment e = gfx::env();
    g_top = lerpC(a.top, b.top, t);
    g_horizon = lerpC(a.horizon, b.horizon, t);
    e.skyTop = g_top;
    e.skyHorizon = g_horizon;
    e.fogColor = lerpC(a.fog, b.fog, t);
    e.sunColor = lerpC(a.sun, b.sun, t);
    e.shadowColor = lerpC(a.shadow, b.shadow, t);
    e.rimColor = lerpC(a.rim, b.rim, t);
    e.sunDisc = lerpC(a.disc, b.disc, t);
    e.rimStrength = hvLerp(a.rim_strength, b.rim_strength, t);
    // sun path: rises in the east (+x), sets in the west, tilted to the south (+z)
    f32 ang = (hours - 6.0f) / 12.0f * HV_PI;     // 0 at 6am, pi at 6pm
    g_sunPos = normalize(Vec3(cosf(ang), sinf(ang), 0.35f));
    g_day = hvSaturate(sinf(ang) * 3.0f + 0.2f);
    Vec3 lightTo = g_sunPos;
    if (g_sunPos.y < 0.08f) {
        // moonlight from the opposite side, keep it from going under the horizon
        Vec3 moon = normalize(Vec3(-g_sunPos.x, -g_sunPos.y, -0.25f));
        f32 w = hvSmooth((0.08f - g_sunPos.y) / 0.25f);
        lightTo = normalize(lerp(g_sunPos, moon, w));
        if (lightTo.y < 0.2f) lightTo = normalize(Vec3(lightTo.x, 0.2f, lightTo.z));
    }
    if (lightTo.y < 0.18f) lightTo = normalize(Vec3(lightTo.x, 0.18f, lightTo.z));
    g_sunDirTo = lightTo;
    e.sunDir = -lightTo;
    e.nightness = 1.0f - g_day;
    e.fogStart = hvLerp(40.0f, 70.0f, g_day);
    e.fogEnd = hvLerp(190.0f, 250.0f, g_day);
    gfx::setEnvironment(e);
}

void update(f32 dt) {
    g_cloudT += dt;
    for (auto &c : g_clouds) {
        c.p.x += c.speed * dt;
        if (c.p.x > 470) c.p.x -= 560;
    }
}

static void skyVertex(const Vec3 &dir, const Vec3 &eye, f32 radius) {
    f32 e = hvSaturate(dir.y);
    f32 t = powf(e, 0.55f);
    GXColor c = gxlerp(g_horizon, g_top, t);
    // warm glow around the sun
    f32 sd = dot(dir, g_sunPos);
    if (sd > 0) {
        f32 g = powf(sd, 6.0f) * 0.55f * hvSaturate(g_sunPos.y * 4 + 0.6f);
        const GXColor &disc = gfx::env().sunDisc;
        c.r = (u8)hvMin(255.0f, c.r + (disc.r - c.r) * g);
        c.g = (u8)hvMin(255.0f, c.g + (disc.g - c.g) * g * 0.85f);
        c.b = (u8)hvMin(255.0f, c.b + (disc.b - c.b) * g * 0.6f);
    }
    // below the horizon fade into fog colour
    if (dir.y < 0) c = gxlerp(c, gfx::env().fogColor, hvSaturate(-dir.y * 6));
    Vec3 p = eye + dir * radius;
    GX_Position3f32(p.x, p.y, p.z);
    GX_Color4u8(c.r, c.g, c.b, 255);
}

static void billboard(const Vec3 &center, f32 size, GXColor c) {
    const Camera &cam = gfx::camera();
    Vec3 right(cam.view.m[0][0], cam.view.m[0][1], cam.view.m[0][2]);
    Vec3 up(cam.view.m[1][0], cam.view.m[1][1], cam.view.m[1][2]);
    Vec3 r = right * size, u = up * size;
    Vec3 p0 = center - r + u, p1 = center + r + u, p2 = center + r - u, p3 = center - r - u;
    gfx::beginImm(GX_QUADS, 4, true);
    GX_Position3f32(p0.x, p0.y, p0.z); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(0, 0);
    GX_Position3f32(p1.x, p1.y, p1.z); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(1, 0);
    GX_Position3f32(p2.x, p2.y, p2.z); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(1, 1);
    GX_Position3f32(p3.x, p3.y, p3.z); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(0, 1);
    GX_End();
}

void draw() {
    const Camera &cam = gfx::camera();
    const f32 R = 300.0f;
    Vec3 eye = cam.eye;
    gfx::loadWorld(Mat34::identity());
    gfx::setShade(SH_VCOL | SH_NOFOG | SH_NOZWRITE | SH_DOUBLESIDED);
    const int SEG = 20;
    static const f32 ELEV[] = {-0.35f, -0.02f, 0.08f, 0.2f, 0.38f, 0.62f, 0.85f, 1.0f};
    const int RINGS = HV_ARRAY_COUNT(ELEV);
    for (int r = 0; r < RINGS - 1; r++) {
        gfx::beginImm(GX_TRIANGLESTRIP, (u16)((SEG + 1) * 2), false);
        for (int s = 0; s <= SEG; s++) {
            f32 a = s * HV_TAU / SEG;
            for (int k = 0; k < 2; k++) {
                // order: upper ring first then lower => clockwise from inside isn't needed (double sided)
                f32 el = ELEV[r + 1 - k];
                f32 ce = sqrtf(hvMax(0.0f, 1 - el * el));
                skyVertex(Vec3(cosf(a) * ce, el, sinf(a) * ce), eye, R);
            }
        }
        GX_End();
    }
    // stars
    f32 night = 1.0f - g_day;
    if (night > 0.05f && g_dot) {
        gfx::setShade(SH_TEX | SH_VCOL | SH_NOFOG | SH_ADDITIVE | SH_DOUBLESIDED);
        tex::bind(g_dot, GX_TEXMAP0);
        for (auto &st : g_stars) {
            u8 a = (u8)(st.b * hvSaturate(night * 1.4f - 0.2f));
            if (a < 8) continue;
            billboard(eye + st.d * (R * 0.9f), 0.9f, gxc(255, 255, 240, a));
        }
    }
    // sun & moon
    if (g_glow) {
        gfx::setShade(SH_TEX | SH_VCOL | SH_NOFOG | SH_ADDITIVE | SH_DOUBLESIDED);
        tex::bind(g_glow, GX_TEXMAP0);
        const GXColor &dc = gfx::env().sunDisc;
        if (g_sunPos.y > -0.1f) {
            Vec3 sp = eye + g_sunPos * (R * 0.85f);
            billboard(sp, 46.0f, gxc(dc.r, dc.g, dc.b, 70));
            billboard(sp, 15.0f, gxc(255, 252, 235, 255));
        }
        Vec3 moon = normalize(Vec3(-g_sunPos.x, -g_sunPos.y, -0.25f));
        if (moon.y > -0.1f) {
            Vec3 mp = eye + moon * (R * 0.85f);
            billboard(mp, 24.0f, gxc(160, 180, 255, (u8)(80 * night)));
            billboard(mp, 7.0f, gxc(235, 240, 255, (u8)(240 * night)));
        }
    }
}

void drawClouds() {
    if (!g_cloudBig) return;
    f32 lightK = 0.55f + 0.45f * g_day;
    GXColor tint = gxc((u8)(255 * lightK), (u8)(255 * lightK), (u8)(255 * hvMin(1.0f, lightK + 0.08f)));
    for (auto &c : g_clouds) {
        const Model *m = c.big ? g_cloudBig : g_cloudSmall;
        gfx::drawModel(m, Mat34::place(c.p, c.yaw, c.scale), tint, SH_NOFOG);
    }
}

}  // namespace sky

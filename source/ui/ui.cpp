#include "ui/ui.h"
#include <stdarg.h>
#include <stdio.h>
#include "core/pak.h"
#include "gfx/model.h"
#include "gfx/renderer.h"
#include "gfx/texture.h"
#include "platform/platform.h"

namespace {

struct GlyphInfo {
    u16 x, y;
    u8 w, h;
    s8 xo, yo;
    u8 adv;
    bool valid;
};

struct Font {
    const Texture *tex;
    f32 lineH, ascent;
    f32 tw, th;
    GlyphInfo g[128];
};

Font g_fonts[FONT_COUNT];
const Texture *g_panel, *g_panelSharp, *g_buttons, *g_bar, *g_circle, *g_minimap;
f32 g_time = 0;
f32 g_W = 640, g_H = 480;

void loadFont(Font &f, const char *name) {
    memset(&f, 0, sizeof(f));
    const u8 *d = g_pak.find(name, nullptr, ASSET_FONT);
    if (!d) {
        hvLog("ui: missing font %s", name);
        return;
    }
    f.tw = rdU16(d + 4);
    f.th = rdU16(d + 6);
    f.lineH = rdU16(d + 8);
    f.ascent = rdS16(d + 10);
    int n = rdU16(d + 12);
    f.tex = tex::get(rdU32(d + 16));
    for (int i = 0; i < n; i++) {
        const u8 *e = d + 20 + i * 14;
        u16 code = rdU16(e);
        if (code >= 128) continue;
        GlyphInfo &g = f.g[code];
        g.x = rdU16(e + 2);
        g.y = rdU16(e + 4);
        g.w = e[6];
        g.h = e[7];
        g.xo = (s8)e[8];
        g.yo = (s8)e[9];
        g.adv = e[10];
        g.valid = true;
    }
}

void setOrtho2D() {
    Mat44 p = orthoGX(0, g_H, 0, g_W, 0, 100);
    GX_LoadProjectionMtx(p.m, GX_ORTHOGRAPHIC);
    Mat34 id = Mat34::identity();
    GX_LoadPosMtxImm(id.m, GX_PNMTX0);
    GX_LoadNrmMtxImm(id.m, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
    GX_SetViewport(0, 0, g_W, g_H, 0, 1);
}

void vcd2D(bool tex) {
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    if (tex) GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    gfx::vcdChanged();
}

int g_vcd = -1;
void use2D(bool tex) {
    int m = tex ? 1 : 0;
    if (m != g_vcd) {
        vcd2D(tex);
        g_vcd = m;
    }
    gfx::setShade((tex ? SH_TEX : 0) | SH_VCOL | SH_BLEND | SH_NOFOG | SH_NOZTEST | SH_DOUBLESIDED);
}

inline void v2(f32 x, f32 y, GXColor c) {
    GX_Position2f32(x, y);
    GX_Color4u8(c.r, c.g, c.b, c.a);
}
inline void v2t(f32 x, f32 y, GXColor c, f32 u, f32 v) {
    GX_Position2f32(x, y);
    GX_Color4u8(c.r, c.g, c.b, c.a);
    GX_TexCoord2f32(u, v);
}

}  // namespace

namespace ui {

const GXColor WHITE = {255, 255, 255, 255};
const GXColor GOLD = {255, 214, 120, 255};
const GXColor PANEL = {34, 30, 44, 215};
const GXColor PANEL_DARK = {18, 16, 26, 230};
const GXColor TEXT_DIM = {190, 186, 200, 255};
const GXColor GREEN = {120, 230, 120, 255};
const GXColor RED = {240, 96, 90, 255};
const GXColor BLUE = {120, 180, 255, 255};

void init() {
    loadFont(g_fonts[FONT_UI], "font/ui");
    loadFont(g_fonts[FONT_SMALL], "font/small");
    loadFont(g_fonts[FONT_BIG], "font/big");
    loadFont(g_fonts[FONT_TITLE], "font/title");
    g_panel = tex::get("tx/ui_panel");
    g_panelSharp = tex::get("tx/ui_panel_sharp");
    g_buttons = tex::get("tx/ui_buttons");
    g_bar = tex::get("tx/ui_bar");
    g_circle = tex::get("tx/ui_circle");
    g_minimap = tex::get("tx/minimap");
}

void setTime(f32 t) { g_time = t; }

void begin() {
    g_W = (f32)plat::screenW();
    g_H = (f32)plat::screenH();
    gfx::invalidateState();
    g_vcd = -1;
    setOrtho2D();
}

void end() { gfx::invalidateState(); }

void rect(f32 x, f32 y, f32 w, f32 h, GXColor c) {
    use2D(false);
    GX_Begin(GX_QUADS, VFMT_2D, 4);
    v2(x, y, c);
    v2(x + w, y, c);
    v2(x + w, y + h, c);
    v2(x, y + h, c);
    GX_End();
}

void rectGradient(f32 x, f32 y, f32 w, f32 h, GXColor top, GXColor bottom) {
    use2D(false);
    GX_Begin(GX_QUADS, VFMT_2D, 4);
    v2(x, y, top);
    v2(x + w, y, top);
    v2(x + w, y + h, bottom);
    v2(x, y + h, bottom);
    GX_End();
}

static void panelLayer(const Texture *t, f32 x, f32 y, f32 w, f32 h, GXColor fill, f32 r) {
    r = hvMin(r, hvMin(w, h) * 0.5f);
    f32 xs[4] = {x, x + r, x + w - r, x + w};
    f32 ys[4] = {y, y + r, y + h - r, y + h};
    f32 us[4] = {0, 0.25f, 0.75f, 1};
    tex::bind(t, GX_TEXMAP0);
    GX_Begin(GX_QUADS, VFMT_2D, 36);
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            v2t(xs[i], ys[j], fill, us[i], us[j]);
            v2t(xs[i + 1], ys[j], fill, us[i + 1], us[j]);
            v2t(xs[i + 1], ys[j + 1], fill, us[i + 1], us[j + 1]);
            v2t(xs[i], ys[j + 1], fill, us[i], us[j + 1]);
        }
    GX_End();
}

void panel(f32 x, f32 y, f32 w, f32 h, GXColor fill, f32 r) {
    const Texture *t = r > 8 ? g_panel : g_panelSharp;
    if (!t) {
        rect(x, y, w, h, fill);
        return;
    }
    use2D(true);
    // light rim: a slightly larger, brighter layer behind the fill
    GXColor rim = {(u8)hvMin(255, fill.r + 70), (u8)hvMin(255, fill.g + 62), (u8)hvMin(255, fill.b + 50), (u8)hvMin(255, fill.a + 10)};
    if (fill.r + fill.g + fill.b > 520) rim = {(u8)(fill.r * 7 / 10), (u8)(fill.g * 6 / 10), (u8)(fill.b * 5 / 10), fill.a};
    panelLayer(t, x, y, w, h, rim, r);
    f32 b = r > 8 ? 2.5f : 1.5f;
    panelLayer(t, x + b, y + b, w - 2 * b, h - 2 * b, fill, hvMax(1.0f, r - b));
}

void bar(f32 x, f32 y, f32 w, f32 h, f32 frac, GXColor fill, GXColor back) {
    frac = hvSaturate(frac);
    panel(x - 2, y - 2, w + 4, h + 4, back, 5);
    if (frac <= 0 || !g_bar) return;
    use2D(true);
    tex::bind(g_bar, GX_TEXMAP0);
    f32 fw = w * frac;
    GX_Begin(GX_QUADS, VFMT_2D, 4);
    v2t(x, y, fill, 0, 0);
    v2t(x + fw, y, fill, frac, 0);
    v2t(x + fw, y + h, fill, frac, 1);
    v2t(x, y + h, fill, 0, 1);
    GX_End();
}

void glyph(Glyph g, f32 x, f32 y, f32 size, u8 alpha) {
    if (!g_buttons) return;
    use2D(true);
    tex::bind(g_buttons, GX_TEXMAP0);
    f32 u0 = (g % 8) / 8.0f, v0 = (g / 8) / 2.0f;
    f32 u1 = u0 + 1 / 8.0f, v1 = v0 + 0.5f;
    GXColor c = {255, 255, 255, alpha};
    GX_Begin(GX_QUADS, VFMT_2D, 4);
    v2t(x, y, c, u0, v0);
    v2t(x + size, y, c, u1, v0);
    v2t(x + size, y + size, c, u1, v1);
    v2t(x, y + size, c, u0, v1);
    GX_End();
}

void sprite(u32 texHash, f32 x, f32 y, f32 w, f32 h, GXColor c, f32 u0, f32 v0, f32 u1, f32 v1) {
    const Texture *t = tex::get(texHash);
    if (!t) return;
    use2D(true);
    tex::bind(t, GX_TEXMAP0);
    GX_Begin(GX_QUADS, VFMT_2D, 4);
    v2t(x, y, c, u0, v0);
    v2t(x + w, y, c, u1, v0);
    v2t(x + w, y + h, c, u1, v1);
    v2t(x, y + h, c, u0, v1);
    GX_End();
}

static inline int mapChar(FontId f, int ch) {
    if (ch < 0 || ch >= 128) return '?';
    const Font &fn = g_fonts[f];
    if (!fn.g[ch].valid && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';   // title font is caps only
    if (!fn.g[ch].valid) return ' ';
    return ch;
}

f32 lineHeight(FontId f, f32 scale) { return g_fonts[f].lineH * scale; }

f32 textWidth(FontId f, const char *s, f32 scale) {
    const Font &fn = g_fonts[f];
    f32 w = 0, best = 0;
    for (; *s; s++) {
        if (*s == '\n') {
            best = hvMax(best, w);
            w = 0;
            continue;
        }
        w += fn.g[mapChar(f, (u8)*s)].adv;
    }
    return hvMax(best, w) * scale;
}

f32 text(FontId f, f32 x, f32 y, const char *s, GXColor c, int align, f32 scale) {
    const Font &fn = g_fonts[f];
    if (!fn.tex || !s || !*s) return 0;
    f32 w = textWidth(f, s, scale);
    if (align == AL_CENTER) x -= w * 0.5f;
    else if (align == AL_RIGHT) x -= w;
    int n = 0;
    for (const char *p = s; *p; p++) {
        int ch = mapChar(f, (u8)*p);
        if (fn.g[ch].w) n++;
    }
    if (!n) return w;
    use2D(true);
    tex::bind(fn.tex, GX_TEXMAP0);
    GX_Begin(GX_QUADS, VFMT_2D, (u16)(n * 4));
    f32 cx = x, cy = y;
    for (const char *p = s; *p; p++) {
        if (*p == '\n') {
            cx = x;
            cy += fn.lineH * scale;
            continue;
        }
        int ch = mapChar(f, (u8)*p);
        const GlyphInfo &g = fn.g[ch];
        if (g.w) {
            f32 gx = cx + g.xo * scale, gy = cy + g.yo * scale;
            f32 gw = g.w * scale, gh = g.h * scale;
            f32 u0 = g.x / fn.tw, v0 = g.y / fn.th, u1 = (g.x + g.w) / fn.tw, v1 = (g.y + g.h) / fn.th;
            v2t(gx, gy, c, u0, v0);
            v2t(gx + gw, gy, c, u1, v0);
            v2t(gx + gw, gy + gh, c, u1, v1);
            v2t(gx, gy + gh, c, u0, v1);
        }
        cx += g.adv * scale;
    }
    GX_End();
    return w;
}

f32 textf(FontId f, f32 x, f32 y, GXColor c, int align, const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return text(f, x, y, buf, c, align);
}

f32 textWrap(FontId f, f32 x, f32 y, f32 width, const char *s, GXColor c, f32 scale, int maxChars) {
    const Font &fn = g_fonts[f];
    char line[256];
    f32 cy = y;
    const char *p = s;
    int shown = 0;
    while (*p) {
        // build a line word by word
        int len = 0;
        const char *lastBreak = nullptr;
        int lastLen = 0;
        f32 w = 0;
        const char *q = p;
        while (*q && *q != '\n') {
            f32 aw = fn.g[mapChar(f, (u8)*q)].adv * scale;
            if (w + aw > width && lastBreak) break;
            if (*q == ' ') {
                lastBreak = q;
                lastLen = len;
            }
            if (len < 255) line[len++] = *q;
            w += aw;
            q++;
        }
        if (*q && *q != '\n' && lastBreak) {
            len = lastLen;
            q = lastBreak + 1;
        } else if (*q == '\n') {
            q++;
        }
        line[len] = 0;
        if (maxChars >= 0) {
            int remain = maxChars - shown;
            if (remain <= 0) break;
            if (remain < len) line[remain] = 0;
        }
        shown += len;
        text(f, x, cy, line, c, AL_LEFT, scale);
        cy += fn.lineH * scale;
        p = q;
    }
    return cy - y;
}

f32 prompt(Glyph g, f32 x, f32 y, const char *label, u8 alpha) {
    glyph(g, x, y - 3, 26, alpha);
    GXColor c = {255, 255, 255, alpha};
    f32 w = text(FONT_UI, x + 28, y, label, c);
    return 28 + w + 14;
}

void iconModel(const Model *m, f32 x, f32 y, f32 size, f32 spin, GXColor tint) {
    if (!m) return;
    // orthographic y-up projection in pixels, depth squeezed so icons sit in front of the 3D scene
    Mat44 p = orthoGX(g_H, 0, 0, g_W, -500, 500);
    GX_LoadProjectionMtx(p.m, GX_ORTHOGRAPHIC);
    GX_SetViewport(0, 0, g_W, g_H, 0.0f, 0.001f);
    f32 r = hvMax(m->radius, 0.05f);
    f32 s = size * 0.5f / r;
    f32 a = g_time * spin + x * 0.013f;
    Mat34 w = Mat34::translation(Vec3(x, g_H - y, 0)) * Mat34::scale(Vec3(s, s, s)) * Mat34::rotX(0.45f) * Mat34::rotY(a) *
              Mat34::translation(-m->center);
    // lights for the icon space (view = identity)
    GXLightObj sun, eye;
    GX_InitLightPos(&sun, -0.45f * 1e5f, 0.8f * 1e5f, 0.6f * 1e5f);
    GX_InitLightAttn(&sun, 1, 0, 0, 1, 0, 0);
    GX_InitLightColor(&sun, gxc(127, 0, 0, 255));
    GX_LoadLightObj(&sun, GX_LIGHT0);
    GX_InitLightPos(&eye, 0, 0, 1e5f);
    GX_InitLightAttn(&eye, 1, 0, 0, 1, 0, 0);
    GX_InitLightColor(&eye, gxc(0, 127, 0, 255));
    GX_LoadLightObj(&eye, GX_LIGHT1);
    gfx::bindUiRamp();
    gfx::drawModelRaw(m, w, tint, SH_NOFOG | SH_RIM);
    g_vcd = -1;
    setOrtho2D();
}

void icon(const char *modelName, f32 x, f32 y, f32 size, f32 spin, GXColor tint) {
    iconModel(mdl::get(modelName), x, y, size, spin, tint);
}

static void minimapSetup() {
    gfx::invalidateState();
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(2);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY);
    GX_SetNumTevStages(2);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
    GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
    GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLORNULL);
    GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_TEXA, GX_CA_APREV, GX_CA_ZERO);
    GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetZCompLoc(GX_TRUE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetFog(GX_FOG_NONE, 0, 1, 0.1f, 1, gxc(0, 0, 0));
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX1, GX_DIRECT);
    GX_SetVtxAttrFmt(VFMT_2D, GX_VA_TEX1, GX_TEX_ST, GX_F32, 0);
    gfx::vcdChanged();
    g_vcd = -1;
}

void minimap(f32 wx, f32 wz, f32 yaw, f32 worldRadius, f32 sx, f32 sy, f32 srad) {
    if (!g_minimap || !g_circle) return;
    // frame
    panel(sx - srad - 6, sy - srad - 6, srad * 2 + 12, srad * 2 + 12, rgba(30, 26, 40, 220), srad + 6);
    minimapSetup();
    tex::bind(g_minimap, GX_TEXMAP0);
    tex::bind(g_circle, GX_TEXMAP1);
    const f32 worldSize = 384.0f;
    f32 cu = wx / worldSize, cv = wz / worldSize, ru = worldRadius / worldSize;
    // screen offset (lx right, ly down) -> world offset using the camera basis:
    // forward = (sin yaw, cos yaw), right = (-cos yaw, sin yaw)
    f32 c = cosf(yaw), s = sinf(yaw);
    f32 cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
    GX_Begin(GX_QUADS, VFMT_2D, 4);
    for (int k = 0; k < 4; k++) {
        f32 lx = cx[k], ly = cy[k];
        f32 dx = -c * lx - s * ly;
        f32 dz = s * lx - c * ly;
        GX_Position2f32(sx + lx * srad, sy + ly * srad);
        GX_Color4u8(255, 255, 255, 235);
        GX_TexCoord2f32(cu + dx * ru, cv + dz * ru);
        GX_TexCoord2f32((lx + 1) * 0.5f, (ly + 1) * 0.5f);
    }
    GX_End();
    // restore 2D format usage
    GX_ClearVtxDesc();
    gfx::vcdChanged();
    gfx::invalidateState();
    g_vcd = -1;
}

void minimapDot(f32 wx, f32 wz, f32 cx, f32 cz, f32 yaw, f32 worldRadius, f32 sx, f32 sy, f32 srad, GXColor col, f32 size) {
    f32 dx = wx - cx, dz = wz - cz;
    f32 c = cosf(yaw), s = sinf(yaw);
    f32 px = (-c * dx + s * dz) / worldRadius;      // along camera right
    f32 py = -(s * dx + c * dz) / worldRadius;      // screen down = -forward
    f32 len = sqrtf(px * px + py * py);
    if (len > 0.92f) {
        px = px / len * 0.92f;
        py = py / len * 0.92f;
    }
    f32 X = sx + px * srad, Y = sy + py * srad;
    sprite(hvHash("tx/ui_circle"), X - size * 0.5f, Y - size * 0.5f, size, size, col);
}

}  // namespace ui

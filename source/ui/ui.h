// 2D UI drawing on GX: fonts, panels, bars, controller glyphs, item icons.
#pragma once
#include "core/hmath.h"

struct Model;

enum FontId { FONT_UI, FONT_SMALL, FONT_BIG, FONT_TITLE, FONT_COUNT };
enum Align { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };
enum Glyph { GL_A, GL_B, GL_X, GL_Y, GL_Z, GL_L, GL_R, GL_START, GL_DPAD, GL_C, GL_STICK };

namespace ui {
void init();
// The UI is laid out in a virtual 640x480 (4:3) or 854x480 (16:9) space that
// is stretched over the framebuffer, so it stays correct on PAL and widescreen.
void setWidescreen(bool on);
bool widescreen();
f32 aspect();
f32 width();
f32 height();
void begin();              // switch to 2D (call after 3D scene)
void end();

// colours
inline GXColor rgba(u8 r, u8 g, u8 b, u8 a = 255) { GXColor c = {r, g, b, a}; return c; }
extern const GXColor SEL;   // highlight fill for the selected entry (white text on top)
extern const GXColor WHITE, GOLD, PANEL, PANEL_DARK, TEXT_DIM, GREEN, RED, BLUE;

void rect(f32 x, f32 y, f32 w, f32 h, GXColor c);
void rectGradient(f32 x, f32 y, f32 w, f32 h, GXColor top, GXColor bottom);
void panel(f32 x, f32 y, f32 w, f32 h, GXColor fill, f32 radius = 12.0f);
void bar(f32 x, f32 y, f32 w, f32 h, f32 frac, GXColor fill, GXColor back = rgba(20, 22, 30, 200));
void glyph(Glyph g, f32 x, f32 y, f32 size = 24.0f, u8 alpha = 255);
void sprite(u32 texHash, f32 x, f32 y, f32 w, f32 h, GXColor c, f32 u0 = 0, f32 v0 = 0, f32 u1 = 1, f32 v1 = 1);

// text
f32 textWidth(FontId f, const char *s, f32 scale = 1.0f);
f32 lineHeight(FontId f, f32 scale = 1.0f);
f32 text(FontId f, f32 x, f32 y, const char *s, GXColor c = WHITE, int align = AL_LEFT, f32 scale = 1.0f);
f32 textf(FontId f, f32 x, f32 y, GXColor c, int align, const char *fmt, ...) __attribute__((format(printf, 6, 7)));
// word-wrapped text; returns height used
f32 textWrap(FontId f, f32 x, f32 y, f32 width, const char *s, GXColor c = WHITE, f32 scale = 1.0f, int maxChars = -1);

// "A: Chop"-style prompt; returns width
f32 prompt(Glyph g, f32 x, f32 y, const char *label, u8 alpha = 255);

// 3D item icon (live model) centred at x,y with size in pixels
void icon(const char *modelName, f32 x, f32 y, f32 size, f32 spin = 0.6f, GXColor tint = rgba(255, 255, 255));
void iconModel(const Model *m, f32 x, f32 y, f32 size, f32 spin, GXColor tint);
void setTime(f32 t);

// minimap: world xz centre, yaw (camera), radius in world units, screen centre + radius
void minimap(f32 wx, f32 wz, f32 yaw, f32 worldRadius, f32 sx, f32 sy, f32 srad);
void minimapDot(f32 wx, f32 wz, f32 cx, f32 cz, f32 yaw, f32 worldRadius, f32 sx, f32 sy, f32 srad, GXColor c, f32 size = 5.0f);
}  // namespace ui

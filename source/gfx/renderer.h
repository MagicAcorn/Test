// Hearthvale renderer: stylised cel shading on the GX fixed-function pipeline.
//
// Lighting trick (as used by Wind Waker): hardware vertex lighting computes
// half-lambert N.L (red) and N.V (green) into colour channel 1. A texgen of
// type GX_TG_SRTG turns those two values into texture coordinates into a
// small 2D ramp texture, whose RGB is the toon-banded light colour and whose
// alpha is the rim-light strength. All shading is per vertex + one lookup.
#pragma once
#include "core/hmath.h"
#include "gfx/model.h"

struct Camera {
    Vec3 eye, target;
    f32 fov = 55.0f;
    f32 nearZ = 0.6f, farZ = 420.0f;
    Mat34 view;
    Mat44 proj;
    f32 tanX = 1, tanY = 1;
    void update(f32 aspect);
};

struct Environment {
    Vec3 sunDir = Vec3(-0.45f, -0.75f, -0.35f);   // direction the light travels
    // light multipliers, 128 = 1.0x (TEV scales by 2)
    GXColor sunColor = {150, 142, 126, 255};
    GXColor shadowColor = {82, 88, 112, 255};
    GXColor rimColor = {120, 110, 90, 255};
    GXColor fogColor = {176, 206, 226, 255};
    GXColor skyTop = {70, 130, 205, 255};
    GXColor skyHorizon = {190, 220, 235, 255};
    GXColor sunDisc = {255, 245, 215, 255};
    f32 fogStart = 70.0f, fogEnd = 260.0f;
    f32 rimStrength = 0.65f;
    f32 bandEdge = 0.47f, bandSoft = 0.05f;
    f32 nightness = 0.0f;   // 0 day .. 1 night (for stars, lamps)
};

enum ShadeFlags : u32 {
    SH_TEX = 1,          // modulate by texture in TEXMAP0
    SH_VCOL = 2,         // modulate by vertex colour (CLR0)
    SH_LIT = 4,          // cel lighting via ramp
    SH_RIM = 8,          // rim light (needs SH_LIT)
    SH_ALPHATEST = 16,   // cutout alpha
    SH_BLEND = 32,       // alpha blending, no z write
    SH_NOFOG = 64,
    SH_DOUBLESIDED = 128,
    SH_ADDITIVE = 256,   // additive blending (glows, sparks)
    SH_NOZTEST = 512,
    SH_DETAIL2X = 1024,  // texture is a grey detail map: tex*2*vcol
    SH_NOZWRITE = 2048,
    SH_TEXPOS = 4096,    // texcoord0 generated from position via GX_TEXMTX0
    SH_KONSTCOL = 8192,  // multiply by KONST colour K1 (fades, tints of vertex-coloured meshes)
};

namespace gfx {

void init();
void setEnvironment(const Environment &e);
const Environment &env();

void beginScene(const Camera &cam);
const Camera &camera();
const Mat34 &view();

// Configure the TEV/channels/texgens for a shading flag set. Cached.
void setShade(u32 flags);
void setTint(GXColor c);              // material colour for COLOR0 (when no vertex colour)
void setKonst(GXColor c);             // colour used by SH_KONSTCOL
void invalidateState();
// Call after changing the vertex descriptor outside the renderer.
void vcdChanged();

// Bounding sphere test against the current camera frustum (world space).
bool visible(const Vec3 &center, f32 radius);
f32 viewDepth(const Vec3 &p);

// Static model.
void drawModel(const Model *m, const Mat34 &world, GXColor tint = gxc(255, 255, 255), u32 extraFlags = 0);
// Skinned model; drawMtx are model-space matrices from anim::drawMatrices.
void drawSkinned(const Model *m, const Mat34 &world, const Mat34 *drawMtx, GXColor tint = gxc(255, 255, 255), u32 extraFlags = 0);

// Draws with mv used directly as the position matrix (no camera, no culling).
void drawModelRaw(const Model *m, const Mat34 &mv, GXColor tint, u32 extraFlags);
// Binds a fixed daylight ramp (for UI icons) to TEXMAP7.
void bindUiRamp();

// Loads view*world into PNMTX0 (and its normal matrix) and selects it.
void loadWorld(const Mat34 &world);

// Immediate mode helpers (VTXFMT2: pos f32, clr rgba8, tex f32).
void beginImm(u8 prim, u16 count, bool withTex, bool withNormal = false);
inline void immV(f32 x, f32 y, f32 z) { GX_Position3f32(x, y, z); }

// Stats for profiling overlay
struct Stats {
    u32 models, skinned, batches, culled;
};
Stats &stats();

// Ramp texture regenerates when environment lighting changes.
void rebuildRamp();

}  // namespace gfx

// Vertex formats
enum {
    VFMT_MODEL = GX_VTXFMT0,
    VFMT_TERRAIN = GX_VTXFMT1,
    VFMT_IMM = GX_VTXFMT2,
    VFMT_2D = GX_VTXFMT3,
    VFMT_PART = GX_VTXFMT4,
};

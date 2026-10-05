// gxemu: a software implementation of the subset of the GameCube GX API
// used by Hearthvale. It lets the exact same rendering code that runs on the
// console render on a PC, producing screenshots for visual QA, and it
// validates GX usage (vertex counts/formats, display list contents, ...)
// that would hang or corrupt real hardware.
//
// All memory handed to GX (vertex arrays, display lists, textures) is treated
// as big-endian GameCube memory, exactly like the real GPU sees it.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef float f32;
typedef double f64;
typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;

typedef f32 Mtx[3][4];
typedef f32 (*MtxP)[4];
typedef f32 Mtx33[3][3];
typedef f32 Mtx44[4][4];

typedef struct _vecf { f32 x, y, z; } guVector;

#include "gx_constants.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _gx_color { u8 r, g, b, a; } GXColor;
typedef struct _gx_colors10 { s16 r, g, b, a; } GXColorS10;

typedef struct _gx_texobj {
    void *data;
    u16 width, height;
    u8 fmt, wrap_s, wrap_t, mipmap;
    u8 minfilt, magfilt;
    f32 minlod, maxlod, lodbias;
    u32 pad[2];
} GXTexObj;

typedef struct _gx_litobj {
    f32 pos[3];
    f32 dir[3];
    GXColor color;
    f32 a[3], k[3];
} GXLightObj;

typedef struct { u32 val[16]; } GXFifoObj;

typedef struct _gx_rmodeobj {
    u32 viTVMode;
    u16 fbWidth;
    u16 efbHeight;
    u16 xfbHeight;
    u16 viXOrigin;
    u16 viYOrigin;
    u16 viWidth;
    u16 viHeight;
    u32 xfbMode;
    u8 field_rendering;
    u8 aa;
    u8 sample_pattern[12][2];
    u8 vfilter[7];
} GXRModeObj;

// ---------------------------------------------------------------- general
GXFifoObj *GX_Init(void *base, u32 size);
void GX_Flush(void);
void GX_DrawDone(void);
void GX_SetDrawDone(void);
void GX_WaitDrawDone(void);
void GX_InvVtxCache(void);
void GX_InvalidateTexAll(void);
void GX_SetMisc(u32 token, u32 value);

// ----------------------------------------------------------- vertex setup
void GX_ClearVtxDesc(void);
void GX_SetVtxDesc(u8 attr, u8 type);
void GX_SetVtxAttrFmt(u8 vtxfmt, u32 vtxattr, u32 comptype, u32 compsize, u32 frac);
void GX_SetArray(u32 attr, void *ptr, u8 stride);
void GX_Begin(u8 primitive, u8 vtxfmt, u16 vtxcnt);
static inline void GX_End(void) {}
void GX_BeginDispList(void *list, u32 size);
u32 GX_EndDispList(void);
void GX_CallDispList(void *list, u32 nbytes);

void GX_Position3f32(f32 x, f32 y, f32 z);
void GX_Position3s16(s16 x, s16 y, s16 z);
void GX_Position3u16(u16 x, u16 y, u16 z);
void GX_Position3s8(s8 x, s8 y, s8 z);
void GX_Position3u8(u8 x, u8 y, u8 z);
void GX_Position2f32(f32 x, f32 y);
void GX_Position2s16(s16 x, s16 y);
void GX_Position2u16(u16 x, u16 y);
void GX_Position1x16(u16 index);
void GX_Position1x8(u8 index);
void GX_Normal3f32(f32 nx, f32 ny, f32 nz);
void GX_Normal3s16(s16 nx, s16 ny, s16 nz);
void GX_Normal3s8(s8 nx, s8 ny, s8 nz);
void GX_Normal1x16(u16 index);
void GX_Normal1x8(u8 index);
void GX_Color4u8(u8 r, u8 g, u8 b, u8 a);
void GX_Color3u8(u8 r, u8 g, u8 b);
void GX_Color1u32(u32 clr);
void GX_Color1u16(u16 clr);
void GX_Color1x16(u16 index);
void GX_Color1x8(u8 index);
void GX_TexCoord2f32(f32 s, f32 t);
void GX_TexCoord2s16(s16 s, s16 t);
void GX_TexCoord2u16(u16 s, u16 t);
void GX_TexCoord2s8(s8 s, s8 t);
void GX_TexCoord2u8(u8 s, u8 t);
void GX_TexCoord1x16(u16 index);
void GX_TexCoord1x8(u8 index);
void GX_MatrixIndex1x8(u8 index);

// -------------------------------------------------------------- transform
void GX_LoadProjectionMtx(Mtx44 mt, u8 type);
void GX_LoadPosMtxImm(Mtx mt, u32 pnidx);
void GX_LoadNrmMtxImm(Mtx mt, u32 pnidx);
void GX_LoadNrmMtxImm3x3(Mtx33 mt, u32 pnidx);
void GX_LoadTexMtxImm(Mtx mt, u32 texidx, u8 type);
void GX_SetCurrentMtx(u32 mtx);
void GX_SetViewport(f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ);
void GX_SetScissor(u32 xOrigin, u32 yOrigin, u32 wd, u32 ht);
void GX_SetCullMode(u8 mode);
void GX_SetClipMode(u8 mode);
void GX_SetCoPlanar(u8 enable);

// ---------------------------------------------------------------- texgen
void GX_SetNumTexGens(u32 nr);
void GX_SetTexCoordGen(u16 texcoord, u32 tgen_typ, u32 tgen_src, u32 mtxsrc);
void GX_SetTexCoordGen2(u16 texcoord, u32 tgen_typ, u32 tgen_src, u32 mtxsrc, u32 normalize, u32 postmtx);

// -------------------------------------------------------------- lighting
void GX_SetNumChans(u8 num);
void GX_SetChanCtrl(s32 channel, u8 enable, u8 ambsrc, u8 matsrc, u8 litmask, u8 diff_fn, u8 attn_fn);
void GX_SetChanAmbColor(s32 channel, GXColor color);
void GX_SetChanMatColor(s32 channel, GXColor color);
void GX_InitLightPos(GXLightObj *lit_obj, f32 x, f32 y, f32 z);
void GX_InitLightDir(GXLightObj *lit_obj, f32 nx, f32 ny, f32 nz);
void GX_InitLightColor(GXLightObj *lit_obj, GXColor col);
void GX_InitLightAttn(GXLightObj *lit_obj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2);
void GX_InitLightAttnA(GXLightObj *lit_obj, f32 a0, f32 a1, f32 a2);
void GX_InitLightAttnK(GXLightObj *lit_obj, f32 k0, f32 k1, f32 k2);
void GX_InitLightDistAttn(GXLightObj *lit_obj, f32 ref_dist, f32 ref_brite, u8 dist_fn);
void GX_InitLightSpot(GXLightObj *lit_obj, f32 cut_off, u8 spotfn);
void GX_LoadLightObj(const GXLightObj *lit_obj, u8 lit_id);

// ------------------------------------------------------------------- TEV
void GX_SetNumTevStages(u8 num);
void GX_SetTevOrder(u8 tevstage, u8 texcoord, u32 texmap, u8 color);
void GX_SetTevOp(u8 tevstage, u8 mode);
void GX_SetTevColorIn(u8 tevstage, u8 a, u8 b, u8 c, u8 d);
void GX_SetTevAlphaIn(u8 tevstage, u8 a, u8 b, u8 c, u8 d);
void GX_SetTevColorOp(u8 tevstage, u8 tevop, u8 tevbias, u8 tevscale, u8 clamp, u8 tevregid);
void GX_SetTevAlphaOp(u8 tevstage, u8 tevop, u8 tevbias, u8 tevscale, u8 clamp, u8 tevregid);
void GX_SetTevColor(u8 tev_regid, GXColor color);
void GX_SetTevColorS10(u8 tev_regid, GXColorS10 color);
void GX_SetTevKColor(u8 sel, GXColor col);
void GX_SetTevKColorSel(u8 tevstage, u8 sel);
void GX_SetTevKAlphaSel(u8 tevstage, u8 sel);
void GX_SetTevSwapMode(u8 tevstage, u8 ras_sel, u8 tex_sel);
void GX_SetTevSwapModeTable(u8 swapid, u8 r, u8 g, u8 b, u8 a);
void GX_SetTevDirect(u8 tevstage);
void GX_SetNumIndStages(u8 nstages);

// ------------------------------------------------------------- textures
void GX_InitTexObj(GXTexObj *obj, void *img_ptr, u16 wd, u16 ht, u8 fmt, u8 wrap_s, u8 wrap_t, u8 mipmap);
void GX_InitTexObjLOD(GXTexObj *obj, u8 minfilt, u8 magfilt, f32 minlod, f32 maxlod, f32 lodbias, u8 biasclamp, u8 edgelod, u8 maxaniso);
void GX_InitTexObjFilterMode(GXTexObj *obj, u8 minfilt, u8 magfilt);
void GX_InitTexObjWrapMode(GXTexObj *obj, u8 wrap_s, u8 wrap_t);
void GX_InitTexObjMaxLOD(GXTexObj *obj, f32 maxlod);
void GX_InitTexObjMinLOD(GXTexObj *obj, f32 minlod);
void GX_InitTexObjLODBias(GXTexObj *obj, f32 lodbias);
void GX_LoadTexObj(GXTexObj *obj, u8 mapid);
u32 GX_GetTexBufferSize(u16 wd, u16 ht, u32 fmt, u8 mipmap, u8 maxlod);

// --------------------------------------------------------- pixel engine
void GX_SetZMode(u8 enable, u8 func, u8 update_enable);
void GX_SetZCompLoc(u8 before_tex);
void GX_SetBlendMode(u8 type, u8 src_fact, u8 dst_fact, u8 op);
void GX_SetAlphaCompare(u8 comp0, u8 ref0, u8 aop, u8 comp1, u8 ref1);
void GX_SetColorUpdate(u8 enable);
void GX_SetAlphaUpdate(u8 enable);
void GX_SetDstAlpha(u8 enable, u8 a);
void GX_SetDither(u8 dither);
void GX_SetPixelFmt(u8 pix_fmt, u8 z_fmt);
void GX_SetFog(u8 type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor col);
void GX_SetFogColor(GXColor color);
void GX_SetLineWidth(u8 width, u8 fmt);
void GX_SetPointSize(u8 width, u8 fmt);

// ----------------------------------------------------------------- copies
void GX_SetCopyClear(GXColor color, u32 zvalue);
void GX_SetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht);
void GX_SetDispCopyDst(u16 wd, u16 ht);
u32 GX_SetDispCopyYScale(f32 yscale);
f32 GX_GetYScaleFactor(u16 efbHeight, u16 xfbHeight);
void GX_SetDispCopyGamma(u8 gamma);
void GX_SetCopyFilter(u8 aa, u8 sample_pattern[12][2], u8 vf, u8 vfilter[7]);
void GX_SetFieldMode(u8 field_mode, u8 half_aspect_ratio);
void GX_CopyDisp(void *dest, u8 clear);
void GX_SetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht);
void GX_SetTexCopyDst(u16 wd, u16 ht, u32 fmt, u8 mipmap);
void GX_CopyTex(void *dest, u8 clear);
void GX_PixModeSync(void);

// --------------------------------------------------------- cache helpers
void DCFlushRange(void *startaddress, u32 len);
void DCInvalidateRange(void *startaddress, u32 len);
void DCStoreRange(void *startaddress, u32 len);

// ---------------------------------------------------- emulator controls
typedef struct {
    u32 tris_in, tris_drawn, pixels, prims, dl_calls, mtx_loads, verts;
    u32 errors;
} GXEmuStats;

void gxemu_set_raster(int enabled);       // 0 = validate only (fast)
const u8 *gxemu_last_frame(int *w, int *h); // RGB888 of the last GX_CopyDisp
void gxemu_get_stats(GXEmuStats *out);
void gxemu_reset_stats(void);
int gxemu_error_count(void);

#ifdef __cplusplus
}
#endif

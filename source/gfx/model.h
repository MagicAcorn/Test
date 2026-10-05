// Runtime view of MDL1 assets (see tools/meshbuild.py).
#pragma once
#include "core/hmath.h"
#include "gfx/texture.h"

enum ModelFlags { MF_SKINNED = 1, MF_COLORS = 2, MF_UVS = 4, MF_NORMALS = 8 };
enum BatchFlags { BF_ALPHATEST = 1, BF_DOUBLESIDED = 2, BF_UNLIT = 4, BF_TRANSLUCENT = 8, BF_FOLIAGE = 16 };

struct ModelBatch {
    void *dl;
    u32 dlSize;
    u8 texSlot, flags, numMtx;
    u16 mtx[10];
};

struct Envelope {
    u8 joint[4];
    f32 weight[4];
    u8 count;
};

struct Model {
    u32 hash;
    u16 flags;
    u8 posFrac, uvFrac;
    void *pos, *nrm, *uv, *clr;
    u16 numBatches, numDrawMtx, numJoints, numEnvelopes;
    ModelBatch *batches;
    Envelope *envelopes;
    Vec3 bmin, bmax, center;
    f32 radius;
    const Texture *tex[4];
    u8 numTex;
    u32 skelHash;
    const Model *lod;   // coarser version used beyond lodDist (scaled by the model's world scale)
    f32 lodDist;
};

namespace mdl {
const Model *get(u32 hash);   // loads/caches on first use, null if missing
inline const Model *get(const char *name) { return get(hvHash(name)); }
}  // namespace mdl

#include "gfx/model.h"
#include <stdlib.h>
#include "core/pak.h"

namespace {
const int TABLE = 1024;
Model *g_table[TABLE];

Model *load(u32 hash) {
    const u8 *d = g_pak.find(hash, nullptr, ASSET_MDL);
    if (!d) return nullptr;
    if (memcmp(d, "MDL1", 4) != 0) return nullptr;
    Model *m = new Model();   // value-initialised (zeroed)
    m->hash = hash;
    m->flags = rdU16(d + 4);
    m->posFrac = d[6];
    m->uvFrac = d[7];
    m->numBatches = rdU16(d + 16);
    m->numDrawMtx = rdU16(d + 18);
    m->numJoints = rdU16(d + 20);
    m->numEnvelopes = rdU16(d + 22);
    m->bmin = Vec3(rdF32(d + 24), rdF32(d + 28), rdF32(d + 32));
    m->bmax = Vec3(rdF32(d + 36), rdF32(d + 40), rdF32(d + 44));
    m->center = Vec3(rdF32(d + 48), rdF32(d + 52), rdF32(d + 56));
    m->radius = rdF32(d + 60);
    u32 posOff = rdU32(d + 64), nrmOff = rdU32(d + 68), uvOff = rdU32(d + 72), clrOff = rdU32(d + 76);
    u32 batchOff = rdU32(d + 80), envOff = rdU32(d + 84);
    m->skelHash = rdU32(d + 88);
    m->numTex = d[108];
    for (int i = 0; i < m->numTex && i < 4; i++) m->tex[i] = tex::get(rdU32(d + 92 + i * 4));
    m->pos = (void *)(d + posOff);
    m->nrm = (void *)(d + nrmOff);
    m->uv = uvOff ? (void *)(d + uvOff) : nullptr;
    m->clr = clrOff ? (void *)(d + clrOff) : nullptr;
    m->batches = new ModelBatch[m->numBatches];
    for (int i = 0; i < m->numBatches; i++) {
        const u8 *b = d + batchOff + i * 32;
        ModelBatch &mb = m->batches[i];
        mb.dl = (void *)(d + rdU32(b));
        mb.dlSize = rdU32(b + 4);
        mb.texSlot = b[8];
        mb.flags = b[9];
        mb.numMtx = b[10];
        for (int k = 0; k < 10; k++) mb.mtx[k] = rdU16(b + 12 + k * 2);
    }
    if (m->numEnvelopes) {
        m->envelopes = new Envelope[m->numEnvelopes];
        for (int i = 0; i < m->numEnvelopes; i++) {
            const u8 *e = d + envOff + i * 8;
            Envelope &ev = m->envelopes[i];
            ev.count = 0;
            for (int k = 0; k < 4; k++) {
                ev.joint[k] = e[k];
                ev.weight[k] = e[4 + k] / 255.0f;
                if (e[4 + k]) ev.count = (u8)(k + 1);
            }
        }
    }
    return m;
}
}  // namespace

namespace mdl {

const Model *get(u32 hash) {
    u32 i = hash & (TABLE - 1);
    for (int probe = 0; probe < TABLE; probe++) {
        Model *m = g_table[i];
        if (!m) {
            m = load(hash);
            if (!m) return nullptr;
            g_table[i] = m;
            return m;
        }
        if (m->hash == hash) return m;
        i = (i + 1) & (TABLE - 1);
    }
    return nullptr;
}

}  // namespace mdl

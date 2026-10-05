#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "gfx/anim.h"
#include "core/pak.h"

namespace {
const int MAX_SKEL = 8;
Skeleton g_skel[MAX_SKEL];
int g_numSkel = 0;

const int CLIP_TABLE = 256;
AnimClip *g_clips[CLIP_TABLE];

Mat34 readMat34(const u8 *p) {
    Mat34 m;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) m.m[r][c] = rdF32(p + (r * 4 + c) * 4);
    return m;
}
}  // namespace

int Skeleton::findJoint(u32 h) const {
    for (int i = 0; i < numJoints; i++)
        if (nameHash[i] == h) return i;
    return -1;
}

static const f32 ARM_DROP = 0.32f;

namespace anim {

const Skeleton *skeleton(u32 hash) {
    for (int i = 0; i < g_numSkel; i++)
        if (g_skel[i].hash == hash) return &g_skel[i];
    const u8 *d = g_pak.find(hash, nullptr, ASSET_SKEL);
    if (!d || g_numSkel >= MAX_SKEL) return nullptr;
    Skeleton &s = g_skel[g_numSkel++];
    s.hash = hash;
    s.numJoints = rdU16(d + 4);
    if (s.numJoints > MAX_JOINTS) s.numJoints = MAX_JOINTS;
    const u8 *j = d + 8;
    for (int i = 0; i < s.numJoints; i++, j += 48) {
        s.nameHash[i] = rdU32(j);
        s.parent[i] = rdS16(j + 4);
        s.restT[i] = Vec3(rdF32(j + 8), rdF32(j + 12), rdF32(j + 16));
        s.restR[i] = Quat(rdF32(j + 20), rdF32(j + 24), rdF32(j + 28), rdF32(j + 32));
        s.restS[i] = Vec3(rdF32(j + 36), rdF32(j + 40), rdF32(j + 44));
    }
    const u8 *ib = d + 8 + rdU16(d + 4) * 48;
    for (int i = 0; i < s.numJoints; i++) s.invBind[i] = readMat34(ib + i * 48);
    s.rootParent = readMat34(ib + rdU16(d + 4) * 48);
    // athletic proportions for the KayKit humanoid rig: longer legs and arms,
    // a slightly longer torso and a smaller head
    for (int i = 0; i < MAX_JOINTS; i++) s.stretch[i] = s.uscale[i] = 1.0f;
    s.hips = -1;
    s.hipLift = 0;
    struct Tweak {
        const char *name;
        f32 stretch, scale;
    };
    static const Tweak TW[] = {
        // must match tools/rigpose.py (the human models are authored in this pose)
        {"upperleg.l", 1.75f, 1}, {"upperleg.r", 1.75f, 1}, {"lowerleg.l", 1.75f, 1}, {"lowerleg.r", 1.75f, 1},
        {"upperarm.l", 1.25f, 1}, {"upperarm.r", 1.25f, 1}, {"lowerarm.l", 1.25f, 1}, {"lowerarm.r", 1.25f, 1},
        {"spine", 1.0f, 1},       {"chest", 1.0f, 1},       {"head", 1.0f, 0.74f},
    };
    // only skeletons flagged as "athletic" (header flags bit 0) get the tweaks
    int found = (rdU16(d + 6) & 1) ? 0 : -100;
    for (const Tweak &t : TW) {
        int j = s.findJoint(hvHash(t.name));
        if (j < 0) continue;
        s.stretch[j] = t.stretch;
        s.uscale[j] = t.scale;
        found++;
    }
    s.hips = (s16)s.findJoint(hvHash("hips"));
    if (found >= 8 && s.hips >= 0) {
        // raise the hips by however much longer the left leg became
        int ul = s.findJoint(hvHash("upperleg.l")), ll = s.findJoint(hvHash("lowerleg.l")), ft = s.findJoint(hvHash("foot.l"));
        if (ul >= 0 && ll >= 0 && ft >= 0)
            s.hipLift = (s.stretch[ul] - 1.0f) * fabsf(s.restT[ll].y) + (s.stretch[ll] - 1.0f) * fabsf(s.restT[ft].y);
    } else {
        for (int i = 0; i < MAX_JOINTS; i++) s.stretch[i] = s.uscale[i] = 1.0f;
    }
    s.armL = s.armR = -1;
    s.athletic = found >= 8;
    for (int i = 0; i < s.numJoints; i++) {
        int par = s.parent[i];
        s.restWR[i] = par >= 0 ? qnormalize(s.restWR[par] * s.restR[i]) : s.restR[i];
    }
    if (s.athletic) {
        static const char *const SIDE[2] = {"l", "r"};
        char b[32];
        s.jSpine = (s16)s.findJoint(hvHash("spine"));
        s.jChest = (s16)s.findJoint(hvHash("chest"));
        s.jHead = (s16)s.findJoint(hvHash("head"));
        for (int k = 0; k < 2; k++) {
            snprintf(b, sizeof(b), "upperarm.%s", SIDE[k]); s.jUA[k] = (s16)s.findJoint(hvHash(b));
            snprintf(b, sizeof(b), "lowerarm.%s", SIDE[k]); s.jLA[k] = (s16)s.findJoint(hvHash(b));
            snprintf(b, sizeof(b), "upperleg.%s", SIDE[k]); s.jUL[k] = (s16)s.findJoint(hvHash(b));
            snprintf(b, sizeof(b), "lowerleg.%s", SIDE[k]); s.jLL[k] = (s16)s.findJoint(hvHash(b));
            snprintf(b, sizeof(b), "foot.%s", SIDE[k]); s.jFoot[k] = (s16)s.findJoint(hvHash(b));
        }
        s.legLen = fabsf(s.restT[s.jLL[0]].y) * s.stretch[s.jUL[0]] + fabsf(s.restT[s.jFoot[0]].y) * s.stretch[s.jLL[0]] + 0.15f;
    }
    if (found >= 8) {
        s.armL = (s16)s.findJoint(hvHash("upperarm.l"));
        s.armR = (s16)s.findJoint(hvHash("upperarm.r"));
        s.armFixL = Quat::axisAngle(Vec3(0, 0, 1), ARM_DROP);
        s.armFixR = Quat::axisAngle(Vec3(0, 0, 1), -ARM_DROP);
    }
    return &s;
}

const AnimClip *clip(u32 hash) {
    u32 i = hash & (CLIP_TABLE - 1);
    for (int probe = 0; probe < CLIP_TABLE; probe++) {
        AnimClip *c = g_clips[i];
        if (!c) {
            const u8 *d = g_pak.find(hash, nullptr, ASSET_ANIM);
            if (!d) return nullptr;
            c = new AnimClip();
            c->hash = hash;
            c->numJoints = rdU16(d + 4);
            c->numFrames = rdU16(d + 6);
            c->fps = rdF32(d + 8);
            c->duration = rdF32(d + 12);
            c->loop = (rdU32(d + 16) & 1) != 0;
            c->tscale = rdF32(d + 20);
            c->numAnimT = rdU16(d + 24);
            c->tmap = (const s8 *)(d + 28);
            u32 off = 28 + ((c->numJoints + 3) & ~3u);
            c->rot = d + off;
            c->trans = d + off + (u32)c->numFrames * c->numJoints * 8;
            g_clips[i] = c;
            return c;
        }
        if (c->hash == hash) return c;
        i = (i + 1) & (CLIP_TABLE - 1);
    }
    return nullptr;
}

void restPose(const Skeleton *s, Pose &out) {
    for (int j = 0; j < s->numJoints; j++) {
        out.t[j] = s->restT[j];
        out.r[j] = s->restR[j];
    }
}

static inline Quat readQ(const u8 *p) {
    const f32 k = 1.0f / 32767.0f;
    return Quat(rdS16(p) * k, rdS16(p + 2) * k, rdS16(p + 4) * k, rdS16(p + 6) * k);
}

void sample(const AnimClip *c, f32 time, const Skeleton *s, Pose &out) {
    if (!c) {
        restPose(s, out);
        return;
    }
    f32 f = time * c->fps;
    f32 maxf = (f32)(c->numFrames - 1);
    if (c->loop) {
        f32 period = maxf;
        if (period > 0) {
            f = fmodf(f, period);
            if (f < 0) f += period;
        }
    } else if (f > maxf) {
        f = maxf;
    }
    if (f < 0) f = 0;
    int i0 = (int)f;
    int i1 = i0 + 1;
    if (i1 > c->numFrames - 1) i1 = c->numFrames - 1;
    f32 a = f - i0;
    int nj = hvMin<int>(c->numJoints, s->numJoints);
    const u8 *r0 = c->rot + (u32)i0 * c->numJoints * 8;
    const u8 *r1 = c->rot + (u32)i1 * c->numJoints * 8;
    for (int j = 0; j < nj; j++) {
        Quat q0 = readQ(r0 + j * 8), q1 = readQ(r1 + j * 8);
        out.r[j] = qnlerp(q0, q1, a);
        int ti = c->tmap[j];
        if (ti >= 0) {
            const u8 *t0 = c->trans + ((u32)i0 * c->numAnimT + ti) * 6;
            const u8 *t1 = c->trans + ((u32)i1 * c->numAnimT + ti) * 6;
            Vec3 v0(rdS16(t0) * c->tscale, rdS16(t0 + 2) * c->tscale, rdS16(t0 + 4) * c->tscale);
            Vec3 v1(rdS16(t1) * c->tscale, rdS16(t1 + 2) * c->tscale, rdS16(t1 + 4) * c->tscale);
            out.t[j] = lerp(v0, v1, a);
        } else {
            out.t[j] = s->restT[j];
        }
    }
    for (int j = nj; j < s->numJoints; j++) {
        out.t[j] = s->restT[j];
        out.r[j] = s->restR[j];
    }
}

void blend(Pose &a, const Pose &b, f32 w) {
    for (int j = 0; j < MAX_JOINTS; j++) {
        a.t[j] = lerp(a.t[j], b.t[j], w);
        a.r[j] = qnlerp(a.r[j], b.r[j], w);
    }
}

// Apply a body-space rotation D (x right, y up, z forward) to joint j,
// expressed relative to its parent's rest orientation.
static inline void bodyRot(const Skeleton *s, Pose &p, int j, const Quat &D) {
    if (j < 0) return;
    int par = s->parent[j];
    Quat pw = par >= 0 ? s->restWR[par] : Quat();
    Quat pwInv(-pw.x, -pw.y, -pw.z, pw.w);
    p.r[j] = qnormalize(pwInv * D * pw * s->restR[j]);
}

void locomotion(const Skeleton *s, Pose &p, f32 ph, f32 run, f32 act, f32 time, bool combat) {
    if (!s->athletic) return;
    const Vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
    f32 sn = sinf(ph);
    // torso: forward lean, counter-twist, breathing
    f32 lean = (0.04f + 0.14f * run) * act + (combat ? 0.06f : 0.0f);
    f32 breathe = 0.018f * sinf(time * 1.7f) * (1.0f - act);
    bodyRot(s, p, s->jSpine, Quat::axisAngle(X, lean * 0.6f + breathe) * Quat::axisAngle(Y, 0.1f * sn * act));
    bodyRot(s, p, s->jChest, Quat::axisAngle(X, lean * 0.4f + breathe) * Quat::axisAngle(Y, -0.22f * sn * act * (0.6f + run * 0.4f)));
    bodyRot(s, p, s->jHead, Quat::axisAngle(X, -lean * 0.8f) * Quat::axisAngle(Y, 0.12f * sn * act));
    // hips bob twice per cycle and settle lower when running
    if (s->hips >= 0) {
        f32 bob = (0.5f - 0.5f * cosf(2.0f * ph)) * act * (0.03f + 0.03f * run);
        p.t[s->hips] = s->restT[s->hips] + Vec3(0, -bob * 0.6f - (combat ? 0.03f : 0.0f), 0);
        bodyRot(s, p, s->hips, Quat::axisAngle(Y, -0.12f * sn * act));
    }
    // legs: swing, knee bend through the swing phase, flat feet
    f32 A = (0.42f + 0.16f * run) * act;
    for (int k = 0; k < 2; k++) {
        f32 lp = ph + (k ? HV_PI : 0.0f);
        f32 th = A * sinf(lp) + (combat && k ? 0.12f : 0.0f) - (combat && !k ? 0.12f : 0.0f);
        f32 kn = (0.05f + (0.5f + 0.55f * run) * hvMax(0.0f, sinf(lp + 1.1f)) * hvMax(0.0f, cosf(lp) + 0.3f)) * act + (combat ? 0.18f : 0.0f) + 0.03f;
        bodyRot(s, p, s->jUL[k], Quat::axisAngle(X, -th - (combat ? 0.08f : 0.0f)));
        bodyRot(s, p, s->jLL[k], Quat::axisAngle(X, kn));
        bodyRot(s, p, s->jFoot[k], Quat::axisAngle(X, th - kn * 0.8f));
    }
    // arms hang by the sides and swing against the legs; bent when running
    for (int k = 0; k < 2; k++) {
        f32 sgn = k ? 1.0f : -1.0f;      // left arm points +x, rotates down with -z
        f32 down = 1.02f - 0.12f * run - (combat ? 0.15f : 0.0f) + 0.02f * sinf(time * 1.7f) * (1.0f - act);
        f32 swing = -(0.32f + 0.3f * run) * act * sinf(ph + (k ? HV_PI : 0.0f));
        f32 elbow = 0.18f + 1.1f * run * act + (combat ? 0.7f : 0.0f);
        bodyRot(s, p, s->jUA[k], Quat::axisAngle(X, -swing) * Quat::axisAngle(Z, sgn * down));
        bodyRot(s, p, s->jLA[k], Quat::axisAngle(Y, sgn * elbow));
    }
}

void toModel(const Skeleton *s, const Pose &p, Mat34 *jm) {
    // base[] is the shear-free hierarchy; a bone's stretch only scales its own
    // vertices (jm) and pushes its children further along the bone axis
    static Mat34 base[MAX_JOINTS];
    for (int j = 0; j < s->numJoints; j++) {
        int par = s->parent[j];
        Vec3 t = p.t[j];
        if (par >= 0) t.y *= s->stretch[par];
        if (j == s->hips) t.y += s->hipLift;
        Quat r = p.r[j];
        if (j == s->armL) r = r * s->armFixL;
        else if (j == s->armR) r = r * s->armFixR;
        Mat34 local = Mat34::trs(t, r, s->restS[j] * s->uscale[j]);
        base[j] = (par >= 0) ? base[par] * local : s->rootParent * local;
        jm[j] = base[j];
        if (s->stretch[j] != 1.0f) {
            for (int r = 0; r < 3; r++) jm[j].m[r][1] *= s->stretch[j];
        }
    }
}

void drawMatrices(const Model *m, const Skeleton *s, const Mat34 *jm, Mat34 *out) {
    int nj = hvMin<int>(m->numJoints, s->numJoints);
    for (int j = 0; j < nj; j++) out[j] = jm[j] * s->invBind[j];
    for (int e = 0; e < m->numEnvelopes; e++) {
        const Envelope &ev = m->envelopes[e];
        Mat34 &o = out[m->numJoints + e];
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 4; c++) o.m[r][c] = 0;
        for (int k = 0; k < ev.count; k++) {
            const Mat34 &src = out[ev.joint[k]];
            f32 w = ev.weight[k];
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 4; c++) o.m[r][c] += src.m[r][c] * w;
        }
    }
}

}  // namespace anim

// ------------------------------------------------------------- Animator
void Animator::play(const AnimClip *c, f32 fadeTime, bool restart, f32 spd) {
    if (c == cur && !restart) {
        speed = spd;
        return;
    }
    if (cur && fadeTime > 0.0f) {
        prev = cur;
        prevT = t;
        prevSpeed = speed;
        blend = 0.0f;
        blendRate = 1.0f / fadeTime;
    } else {
        prev = nullptr;
        blend = 1.0f;
    }
    cur = c;
    t = 0.0f;
    speed = spd;
}

void Animator::update(f32 dt) {
    t += dt * speed;
    if (cur && !cur->loop && t > cur->duration) t = cur->duration;
    if (prev) {
        prevT += dt * prevSpeed;
        if (!prev->loop && prevT > prev->duration) prevT = prev->duration;
        blend += dt * blendRate;
        if (blend >= 1.0f) {
            blend = 1.0f;
            prev = nullptr;
        }
    }
}

bool Animator::finished() const { return cur && !cur->loop && t >= cur->duration; }

f32 Animator::progress() const {
    if (!cur || cur->duration <= 0) return 1.0f;
    f32 p = t / cur->duration;
    if (cur->loop) p = p - floorf(p);
    return hvMin(p, 1.0f);
}

void Animator::evaluate(const Skeleton *s, Pose &out) const {
    anim::sample(cur, t, s, out);
    if (prev && blend < 1.0f) {
        Pose p;
        anim::sample(prev, prevT, s, p);
        // out = lerp(prev, cur, smooth(blend))
        anim::blend(p, out, hvSmooth(blend));
        out = p;
    }
}

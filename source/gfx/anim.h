// Skeletons, animation clips, sampling/blending and skin matrix palettes.
#pragma once
#include "core/hmath.h"
#include "gfx/model.h"

const int MAX_JOINTS = 32;

struct Skeleton {
    u32 hash;
    int numJoints;
    s16 parent[MAX_JOINTS];
    u32 nameHash[MAX_JOINTS];
    Vec3 restT[MAX_JOINTS];
    Quat restR[MAX_JOINTS];
    Vec3 restS[MAX_JOINTS];
    Mat34 invBind[MAX_JOINTS];
    Mat34 rootParent;
    // Proportion tweaks applied at pose time (leaner, more athletic humanoids):
    // stretch lengthens a bone along its axis, headScale shrinks the head.
    f32 stretch[MAX_JOINTS];
    f32 uscale[MAX_JOINTS];
    s16 hips;
    f32 hipLift;
    int findJoint(u32 nameHash) const;
};

struct AnimClip {
    u32 hash;
    u16 numJoints, numFrames;
    f32 fps, duration;
    bool loop;
    f32 tscale;
    u16 numAnimT;
    const s8 *tmap;
    const u8 *rot;    // s16 x4 per joint per frame (BE)
    const u8 *trans;  // s16 x3 per animated joint per frame (BE)
};

struct Pose {
    Vec3 t[MAX_JOINTS];
    Quat r[MAX_JOINTS];
};

namespace anim {
const Skeleton *skeleton(u32 hash);
const AnimClip *clip(u32 hash);
inline const AnimClip *clip(const char *name) { return clip(hvHash(name)); }

void restPose(const Skeleton *s, Pose &out);
void sample(const AnimClip *c, f32 time, const Skeleton *s, Pose &out);
void blend(Pose &a, const Pose &b, f32 w);   // a = lerp(a, b, w)
void toModel(const Skeleton *s, const Pose &p, Mat34 *jointModel);
// Draw matrices (joint skin matrices followed by envelope blends), model space.
void drawMatrices(const Model *m, const Skeleton *s, const Mat34 *jointModel, Mat34 *out);
}  // namespace anim

struct Animator {
    const AnimClip *cur = nullptr;
    const AnimClip *prev = nullptr;
    f32 t = 0, prevT = 0;
    f32 speed = 1.0f, prevSpeed = 1.0f;
    f32 blend = 1.0f, blendRate = 8.0f;
    bool holdLast = true;   // non-looping clips hold their final frame

    void play(const AnimClip *c, f32 fadeTime = 0.18f, bool restart = false, f32 spd = 1.0f);
    void update(f32 dt);
    bool finished() const;
    f32 progress() const;   // 0..1 through current clip
    void evaluate(const Skeleton *s, Pose &out) const;
};

// Characters in the world: player, villagers, simulated adventurers, enemies.
#pragma once
#include "core/hmath.h"
#include "gfx/anim.h"

enum ActorKind : u8 { AK_PLAYER, AK_NPC, AK_ADVENTURER, AK_ENEMY };

struct Actor {
    bool active = false;
    ActorKind kind = AK_NPC;
    char name[32] = {0};
    const char *title = nullptr;
    Vec3 pos;
    f32 yaw = 0;
    Vec3 vel;
    f32 vy = 0;
    bool grounded = true;
    f32 radius = 0.55f;
    f32 scale = 1.0f;
    const Model *model = nullptr;
    const Skeleton *skel = nullptr;
    Animator anim;
    GXColor tint = {255, 255, 255, 255};
    const Model *heldR = nullptr;
    const Model *heldL = nullptr;
    int jHandR = -1, jHandL = -1, jHead = -1;
    Mat34 world;
    Mat34 jm[MAX_JOINTS];
    Mat34 *dm = nullptr;
    int dmCap = 0;
    bool poseValid = false;
    // stats
    s32 hp = 100, maxHp = 100, mp = 0, maxMp = 0;
    int level = 1;
    f32 hitFlash = 0;
    bool dead = false;
    f32 deadTimer = 0;
    // misc
    f32 squash = 0;        // slime hop phase
    f32 gait = 0, locoW = 0, locoAct = 0;   // procedural locomotion state
    char chat[72] = {0};
    f32 chatTimer = 0;
    u16 npcId = 0;
    u8 enemyType = 0;
    f32 speedMul = 1.0f;

    void setModel(const char *modelName);
    void setHeld(const char *right, const char *left = nullptr);
    void play(const char *clip, f32 fade = 0.18f, bool restart = false, f32 speed = 1.0f);
    bool isPlaying(const char *clip) const;
    void say(const char *text, f32 seconds = 4.0f);
    Vec3 headPos() const;
    Vec3 handPos() const;
};

namespace actors {
void init();
// Animation + matrices (only does skinning work if visible/near camera).
void update(Actor &a, f32 dt, bool nearCamera);
void draw(Actor &a);
void drawShadow(const Actor &a);
// ground snapping + simple physics; returns true if moved
void move(Actor &a, const Vec3 &desired, f32 dt);
}  // namespace actors

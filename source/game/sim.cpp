// The simulated MMO population: adventurers with routines, chatter and world events.
#include <stdio.h>
#include "game/game.h"
#include "game/world.h"
#include "ui/ui.h"

namespace sim {

namespace {
enum AdvState : u8 { AV_TRAVEL, AV_GATHER, AV_FIGHT, AV_REST, AV_TOWN, AV_WAIT };
struct Adventurer {
    int actor;
    AdvState state;
    AdvState next;
    Vec3 goal;
    f32 timer;
    f32 stuck;
    Vec3 lastPos;
    int node;
    int job;
    f32 chatCd;
    f32 gratsTimer;
};
const int NUM_ADV = 10;
Adventurer s_adv[NUM_ADV];
int s_num = 0;
u32 s_rng = 0xA11CE5u;
f32 frand() {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return (s_rng & 0xFFFFFF) / 16777216.0f;
}
int irand(int n) { return (int)(frand() * n) % n; }

// world events
struct WorldEvent {
    const char *title;
    const char *desc;
    int skill;    // skill with bonus xp
    f32 duration;
};
const WorldEvent EVENTS[] = {
    {"Bountiful Boughs", "Woodcutting XP +50% for a while!", SK_WOODCUTTING, 180},
    {"Glittering Seams", "Mining XP +50% for a while!", SK_MINING, 180},
    {"Golden Hour", "Fishing XP +50% at Mirror Lake!", SK_FISHING, 180},
    {"Herb Bloom", "Herbalism XP +50% for a while!", SK_HERBALISM, 180},
    {"Forge Fever", "Smithing XP +50% for a while!", SK_SMITHING, 180},
    {"Feast Day", "Cooking XP +50% for a while!", SK_COOKING, 180},
};
int s_event = -1;
f32 s_eventTimer = 0;
f32 s_nextEvent = 120;

Vec3 townPos() { return Vec3(200 + (frand() - 0.5f) * 24, 0, 196 + (frand() - 0.5f) * 24); }

void pickGoal(Adventurer &ad) {
    Actor &a = g.actors[ad.actor];
    bool night = isNight();
    f32 r = frand();
    ad.node = -1;
    if (night && a.level >= 10 && r < 0.45f) {
        // barrow raid
        ad.goal = Vec3(300 + (frand() - 0.5f) * 20, 0, 80 + frand() * 14);
        ad.next = AV_FIGHT;
    } else if (r < 0.42f) {
        // gather at a random land node
        for (int tries = 0; tries < 12; tries++) {
            int n = irand(hvMax(1, g.numNodes));
            if (n < g.numNodes && !g.nodes[n].fishing) {
                ad.node = n;
                Vec3 off = normalize(Vec3(frand() - 0.5f, 0, frand() - 0.5f)) * 2.0f;
                ad.goal = g.nodes[n].pos + off;
                break;
            }
        }
        ad.next = AV_GATHER;
        if (ad.node < 0) {
            ad.goal = townPos();
            ad.next = AV_TOWN;
        }
    } else if (r < 0.62f) {
        // hunt slimes in a meadow
        static const Vec3 MEADOWS[] = {Vec3(160, 0, 170), Vec3(240, 0, 160), Vec3(150, 0, 240), Vec3(120, 0, 300)};
        ad.goal = MEADOWS[irand(4)] + Vec3((frand() - 0.5f) * 16, 0, (frand() - 0.5f) * 16);
        ad.next = AV_FIGHT;
    } else if (r < 0.8f) {
        ad.goal = townPos();
        ad.next = AV_TOWN;
    } else {
        ad.goal = Vec3(150 + (frand() - 0.5f) * 8, 0, 138 + (frand() - 0.5f) * 8);   // adventurers' camp
        ad.next = AV_REST;
    }
    ad.state = AV_TRAVEL;
    ad.stuck = 0;
}

void spawnAdventurer(int idx) {
    int ai = -1;
    for (int i = 0; i < MAX_ACTORS; i++)
        if (!g.actors[i].active) {
            ai = i;
            break;
        }
    if (ai < 0) return;
    Actor &a = g.actors[ai];
    a = Actor();
    a.active = true;
    a.kind = AK_ADVENTURER;
    snprintf(a.name, sizeof(a.name), "%s", ADVENTURER_NAMES[idx % NUM_ADVENTURER_NAMES]);
    a.setModel(lookModel(irand(5)));
    a.level = 3 + irand(22);
    Adventurer &ad = s_adv[s_num++];
    ad.actor = ai;
    ad.job = frand() < 0.5f ? SK_WARRIOR : SK_MAGE;
    static const char *const W[] = {"itm/sword", "itm/axe", "itm/greatsword"};
    static const char *const M[] = {"itm/staff", "itm/wand"};
    if (ad.job == SK_WARRIOR) a.setHeld(W[irand(3)], frand() < 0.5f ? "itm/shield_round" : nullptr);
    else a.setHeld(M[irand(2)], frand() < 0.4f ? "itm/spellbook" : nullptr);
    // start somewhere reasonable
    a.pos = townPos() + Vec3((frand() - 0.5f) * 60, 0, (frand() - 0.5f) * 60);
    a.pos.y = g_world.groundHeight(a.pos.x, a.pos.z);
    a.yaw = frand() * HV_TAU;
    a.play("idle", 0);
    ad.chatCd = 10 + frand() * 40;
    ad.gratsTimer = 0;
    ad.lastPos = a.pos;
    pickGoal(ad);
}
}  // namespace

void init() {
    s_num = 0;
    for (int i = 0; i < NUM_ADV; i++) spawnAdventurer(i);
    hvLog("sim: %d adventurers", s_num);
}

void onPlayerLevelUp() {
    for (int i = 0; i < s_num; i++) {
        Adventurer &ad = s_adv[i];
        if (distXZ(g.actors[ad.actor].pos, g.player.pos) < 35 && frand() < 0.8f) ad.gratsTimer = 0.6f + frand() * 2.5f;
    }
}

int eventSkill() { return s_event >= 0 ? EVENTS[s_event].skill : -1; }

void update(f32 dt) {
    // world events
    if (s_event >= 0) {
        s_eventTimer -= dt;
        if (s_eventTimer <= 0) {
            s_event = -1;
            toast("The world event has ended.", ui::TEXT_DIM);
        }
    } else if (g.mode == MODE_PLAY) {
        s_nextEvent -= dt;
        if (s_nextEvent <= 0) {
            s_nextEvent = 300 + frand() * 240;
            s_event = irand(HV_ARRAY_COUNT(EVENTS));
            s_eventTimer = EVENTS[s_event].duration;
            showBanner(EVENTS[s_event].title, EVENTS[s_event].desc, 4.5f);
            audio::sfx(SFX_QUEST);
        }
    }
    for (int i = 0; i < s_num; i++) {
        Adventurer &ad = s_adv[i];
        Actor &a = g.actors[ad.actor];
        bool near = distXZ(a.pos, g.cam.eye) < 70;
        ad.timer -= dt;
        switch (ad.state) {
            case AV_TRAVEL: {
                Vec3 to = ad.goal - a.pos;
                to.y = 0;
                f32 d = to.lenXZ();
                if (d < 1.2f) {
                    ad.state = ad.next;
                    a.vel = Vec3();
                    ad.timer = 8 + frand() * 14;
                    if (ad.state == AV_REST) a.play("sit_down", 0.2f, true);
                    break;
                }
                f32 spd = d > 12 ? 6.8f : 3.4f;
                Vec3 dir = to / d;
                a.vel = dir * spd;
                a.yaw = hvApproachAngle(a.yaw, atan2f(dir.x, dir.z), dt * 6);
                if (!a.anim.cur || a.anim.cur->loop) a.play(spd > 5 ? "run" : "walk", 0.2f);
                // stuck detection
                if (distXZ(a.pos, ad.lastPos) < 0.05f * spd) ad.stuck += dt;
                else ad.stuck = 0;
                if (ad.stuck > 2.5f) {
                    // sidestep, then pick a new goal
                    a.pos += Vec3(-dir.z, 0, dir.x) * 1.5f;
                    pickGoal(ad);
                }
                break;
            }
            case AV_GATHER: {
                a.vel = Vec3();
                if (ad.node >= 0) a.yaw = hvApproachAngle(a.yaw, yawTo(a.pos, g.nodes[ad.node].pos), dt * 5);
                const NodeDef *nd = ad.node >= 0 ? nodeDef(g.nodes[ad.node].type) : nullptr;
                const char *anim = !nd ? "interact" : (nd->skill == SK_WOODCUTTING ? "chop" : (nd->skill == SK_MINING ? "heavy" : "pickup"));
                if (!a.anim.cur || a.anim.cur->loop || a.anim.finished()) {
                    a.play(anim, 0.12f, true, 1.0f + frand() * 0.2f);
                    if (near && nd) fx::burst(g.nodes[ad.node].pos + Vec3(0, 1.0f, 0), nd->skill == SK_MINING ? FX_ROCK : FX_LEAF, 4);
                }
                if (ad.timer <= 0) pickGoal(ad);
                break;
            }
            case AV_FIGHT: {
                // find an enemy nearby and swing at it (visual only)
                int e = combat::nearestEnemy(a.pos, 14.0f, -1);
                if (e >= 0) {
                    Actor &en = g.actors[e];
                    Vec3 to = en.pos - a.pos;
                    to.y = 0;
                    f32 d = to.lenXZ();
                    f32 range = ad.job == SK_MAGE ? 9.0f : 2.6f;
                    a.yaw = hvApproachAngle(a.yaw, atan2f(to.x, to.z), dt * 6);
                    if (d > range) {
                        a.vel = to / d * 5.0f;
                        if (!a.anim.cur || a.anim.cur->loop) a.play("run", 0.2f);
                    } else {
                        a.vel = Vec3();
                        if (!a.anim.cur || a.anim.cur->loop || a.anim.finished()) {
                            a.play(ad.job == SK_MAGE ? "cast" : (frand() < 0.5f ? "slash" : "slash_h"), 0.1f, true);
                            if (near) fx::burst(en.pos + Vec3(0, 1, 0), ad.job == SK_MAGE ? FX_MAGIC : FX_HIT, 5);
                        }
                    }
                } else {
                    a.vel = Vec3();
                    if (a.anim.finished() || !a.anim.cur || !a.anim.cur->loop) a.play("idle_2h", 0.3f);
                }
                if (ad.timer <= 0) pickGoal(ad);
                break;
            }
            case AV_REST:
                a.vel = Vec3();
                if (a.anim.finished()) a.play("sit", 0.3f);
                if (ad.timer <= 0) {
                    a.play("sit_up", 0.2f, true);
                    ad.state = AV_WAIT;
                    ad.timer = 1.2f;
                }
                break;
            case AV_TOWN:
                a.vel = Vec3();
                if (a.anim.finished() || !a.anim.cur) a.play("idle", 0.3f);
                if (frand() < dt * 0.1f) a.play(frand() < 0.5f ? "cheer" : "interact", 0.2f, true);
                if (ad.timer <= 0) pickGoal(ad);
                break;
            case AV_WAIT:
                a.vel = Vec3();
                if (ad.timer <= 0) pickGoal(ad);
                break;
        }
        ad.lastPos = a.pos;
        {
            // step around the player rather than through them
            Vec3 d = a.pos - g.player.pos;
            d.y = 0;
            f32 dist = d.lenXZ(), minD = a.radius + g.player.radius + 0.15f;
            if (dist < minD + 1.2f && dist > 0.01f && a.vel.lenXZ() > 0.1f) {
                Vec3 side(-d.z / dist, 0, d.x / dist);
                if (dot(side, a.vel) < 0) side = side * -1.0f;
                a.vel = a.vel + side * (1.0f - (dist - minD) / 1.2f) * a.vel.lenXZ();
            }
            if (dist < minD && dist > 0.01f) a.pos = a.pos + d * ((minD - dist) / dist);
        }
        actors::move(a, a.vel, dt);
        // chatter
        ad.chatCd -= dt;
        if (ad.chatCd <= 0) {
            ad.chatCd = 25 + frand() * 50;
            if (distXZ(a.pos, g.player.pos) < 30) a.say(CHAT_LINES[irand(NUM_CHAT_LINES)], 4.5f);
        }
        if (ad.gratsTimer > 0) {
            ad.gratsTimer -= dt;
            if (ad.gratsTimer <= 0) {
                static const char *const G[] = {"grats!", "Congrats!!", "gz o/", "Nice one!", "grats :)"};
                a.say(G[irand(5)], 3.0f);
            }
        }
        actors::update(a, dt, near);
    }
}

void drawUi() {
    if (s_event >= 0) {
        char b[64];
        snprintf(b, sizeof(b), "%s  %d:%02d", EVENTS[s_event].title, (int)s_eventTimer / 60, (int)s_eventTimer % 60);
        f32 W = ui::width();
        ui::panel(W - 250, 266, 232, 26, ui::rgba(90, 50, 20, 190), 9);
        ui::text(FONT_SMALL, W - 134, 270, b, ui::GOLD, AL_CENTER);
    }
}

}  // namespace sim

// world-event XP bonus hook used by addXp
int simEventSkill() { return sim::eventSkill(); }

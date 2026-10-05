// Combat: enemy spawning/AI, player job abilities, projectiles, telegraphed AoEs.
#include <stdio.h>
#include "game/game.h"
#include "game/world.h"
#include "gfx/texture.h"
#include "ui/ui.h"

namespace combat {

void projectile(const Vec3 &from, int target, int kind, int damage, bool fromPlayer);
void aoe(const Vec3 &p, f32 radius, f32 delay, int damage, int kind, bool hitsPlayer);

namespace {

u32 s_rng = 0xBADC0DEu;
f32 frand() {
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return (s_rng & 0xFFFFFF) / 16777216.0f;
}

struct Spawn {
    u8 type;
    Vec3 pos;
    f32 radius;
    int count;
    int actor[8];
    f32 respawn[8];
    u8 spawnedOnce;   // bit per slot: first spawn ignores the player-distance rule
};
Spawn s_spawns[48];
int s_numSpawns = 0;

enum EState : u8 { ES_IDLE, ES_CHASE, ES_ATTACK, ES_TELEGRAPH, ES_RETURN, ES_DEAD, ES_RISE };
struct EnemyAI {
    int spawn = -1, slot = 0;
    u8 state = ES_IDLE;
    f32 timer = 0;
    Vec3 home;
    Vec3 wander;
    f32 attackCd = 0;
    f32 windup = 0;
    int attackCount = 0;
    f32 teleTimer = 0;
    Vec3 telePos;
    f32 teleRadius = 0;
    u8 teleKind = 0;
    f32 slow = 0;
    f32 dotTimer = 0, dotTick = 0;
    int dotDmg = 0;
    bool aggro = false;
    bool summoned = false;
    f32 hpBarTimer = 0;
    f32 stagger = 0;   // stunned: no moving or attacking
};
EnemyAI s_ai[MAX_ACTORS];

// player action-combat state
f32 s_atkTimer = 0;        // time until the next basic attack may start
int s_atkStep = 0;         // 0..2 within the combo
f32 s_atkWindow = 0;       // combo continues while > 0
bool s_atkQueued = false;  // B pressed during the current swing
bool s_wheelOpen = false;
int s_wheelSel = -1;
f32 s_wheelAnim = 0;
int s_quickSlot = 0;       // Y item: 0 heal, 1 tonic, 2 draught
const int WHEEL[6] = {1, 2, 4, 5, 6, 7};

struct Projectile {
    bool active;
    Vec3 pos;
    int target;     // actor index, -2 = player
    int damage;
    int kind;       // 0 fire, 1 ice, 2 bolt, 3 bone, 4 axe
    f32 life;
    bool fromPlayer;
};
Projectile s_proj[24];

struct Aoe {
    bool active;
    Vec3 pos;
    f32 radius;
    f32 timer, total;
    int damage;
    bool hitsPlayer;
    int kind;
};
Aoe s_aoe[16];

const Texture *s_aoeTex, *s_targetTex;


int allocActor() {
    for (int i = 0; i < MAX_ACTORS; i++)
        if (!g.actors[i].active) return i;
    return -1;
}

bool nightType(int type) { return ENEMIES[type].nightOnly; }

int playerDamage(f32 potency) {
    int wp = g.pd.weapon ? ITEMS[g.pd.weapon].power : 8;
    f32 lvl = (f32)skillLevel(g.pd.job);
    f32 d = wp * (1.0f + lvl * 0.09f) * potency * (0.9f + frand() * 0.2f);
    if (g.buffDamage > 0) d *= 1.2f;
    return (int)d;
}

void hitEnemy(int ai, int dmg, bool crit, int fxKind);

void killEnemy(int ai) {
    Actor &a = g.actors[ai];
    EnemyAI &e = s_ai[ai];
    const EnemyDef &d = ENEMIES[a.enemyType];
    a.dead = true;
    a.deadTimer = 0;
    e.state = ES_DEAD;
    e.timer = 0;
    if (a.skel) a.play(a.enemyType >= EN_SK_MINION && a.enemyType <= EN_BARROW_KING ? "sk_death" : "death", 0.1f, true);
    audio::sfx(d.slime ? SFX_SLIME : SFX_BONES);
    fx::burst(a.pos + Vec3(0, 0.8f, 0), d.slime ? FX_SPLASH : FX_DUST, 16);
    // rewards (auto-loot)
    int jl = skillLevel(g.pd.job);
    u32 xp = d.xp;
    if (d.level + 5 < jl) xp = xp / 3;
    addXp(g.pd.job, xp);
    g.pd.coins += d.coins;
    if (d.coins) {
        char b[32];
        snprintf(b, sizeof(b), "+%u coins", d.coins);
        fx::floatText(a.pos + Vec3(0, 2.4f, 0), b, ui::GOLD, 0.8f);
    }
    for (const LootDrop &l : d.loot)
        if (l.item && frand() * 100 < l.chance) giveItem(l.item, l.count);
    g.pd.kills++;
    quests::onKill(a.enemyType);
    if (g.target == ai) g.target = -1;
}

void hitEnemy(int ai, int dmg, bool crit, int fxKind) {
    Actor &a = g.actors[ai];
    if (!a.active || a.dead) return;
    EnemyAI &e = s_ai[ai];
    a.hp -= dmg;
    a.hitFlash = 1.0f;
    e.hpBarTimer = 6.0f;
    e.aggro = true;
    if (e.state == ES_IDLE || e.state == ES_RETURN) e.state = ES_CHASE;
    g.combatTimer = 6.0f;
    char b[16];
    snprintf(b, sizeof(b), crit ? "%d!" : "%d", dmg);
    fx::floatText(a.pos + Vec3((frand() - 0.5f) * 0.8f, 2.0f * a.scale + 0.3f, 0), b, crit ? ui::GOLD : ui::WHITE, crit ? 1.3f : 1.0f);
    fx::burst(a.pos + Vec3(0, 1.0f * a.scale, 0), fxKind, 8);
    audio::sfx(SFX_HIT, 1.0f, 0.9f + frand() * 0.2f);
    if (a.skel && !a.dead && frand() < 0.35f && e.state != ES_TELEGRAPH && a.enemyType != EN_BARROW_KING) a.play("hit", 0.08f, true);
    if (a.hp <= 0) {
        a.hp = 0;
        killEnemy(ai);
    }
}

void dealPlayerHit(int ai, f32 potency, int fxKind) {
    bool crit = frand() < 0.12f;
    int dmg = playerDamage(potency * (crit ? 1.6f : 1.0f));
    hitEnemy(ai, dmg, crit, fxKind);
}

void spawnAt(Spawn &sp, int slot) {
    int ai = allocActor();
    if (ai < 0) return;
    Actor &a = g.actors[ai];
    a = Actor();
    const EnemyDef &d = ENEMIES[sp.type];
    a.active = true;
    a.kind = AK_ENEMY;
    a.enemyType = sp.type;
    snprintf(a.name, sizeof(a.name), "%s", d.name);
    a.setModel(d.model);
    a.scale = d.scale;
    a.radius = d.slime ? 0.7f : 0.55f;
    a.maxHp = a.hp = d.hp;
    a.level = d.level;
    a.tint = gxc(d.r, d.g, d.b);
    f32 ang = frand() * HV_TAU, r = sqrtf(frand()) * sp.radius;
    a.pos = sp.pos + Vec3(cosf(ang) * r, 0, sinf(ang) * r);
    a.pos.y = g_world.groundHeight(a.pos.x, a.pos.z);
    a.yaw = frand() * HV_TAU;
    if (sp.type == EN_SK_WARRIOR || sp.type == EN_BARROW_KING) a.setHeld("itm/greataxe", nullptr);
    else if (sp.type == EN_SK_MINION) a.setHeld("itm/sword", "itm/shield_round");
    else if (sp.type == EN_SK_ROGUE) a.setHeld("itm/knife", "itm/knife");
    else if (sp.type == EN_SK_MAGE) a.setHeld("itm/staff");
    EnemyAI &e = s_ai[ai];
    e = EnemyAI();
    e.spawn = (int)(&sp - s_spawns);
    e.slot = slot;
    e.home = sp.pos;
    e.wander = a.pos;
    e.timer = frand() * 3;
    if (a.skel) {
        // skeletons claw their way out of the ground
        a.play(sp.type == EN_BARROW_KING ? "sk_awaken" : "sk_rise", 0);
        e.state = ES_RISE;
        e.timer = sp.type == EN_BARROW_KING ? 3.0f : 1.8f;
    } else {
        e.state = ES_IDLE;
    }
    sp.actor[slot] = ai;
}

void updateEnemy(int ai, f32 dt) {
    Actor &a = g.actors[ai];
    EnemyAI &e = s_ai[ai];
    const EnemyDef &d = ENEMIES[a.enemyType];
    Actor &p = g.player;
    f32 distP = distXZ(a.pos, p.pos);
    bool near = distXZ(a.pos, g.cam.eye) < 75;
    if (e.hpBarTimer > 0) e.hpBarTimer -= dt;
    if (e.slow > 0) e.slow -= dt;
    // DoT
    if (e.dotTimer > 0 && !a.dead) {
        e.dotTimer -= dt;
        e.dotTick -= dt;
        if (e.dotTick <= 0) {
            e.dotTick = 3.0f;
            hitEnemy(ai, e.dotDmg, false, FX_BOLT);
            if (a.dead) return;
        }
    }
    if (e.stagger > 0 && !a.dead) {
        e.stagger -= dt;
        a.vel = Vec3();
        return;
    }
    f32 speed = d.speed * (e.slow > 0 ? 0.55f : 1.0f);
    switch (e.state) {
        case ES_RISE:
            e.timer -= dt;
            if (e.timer <= 0) {
                e.state = ES_IDLE;
                a.play("sk_idle", 0.3f);
            }
            break;
        case ES_IDLE: {
            e.timer -= dt;
            if (e.timer <= 0) {
                e.timer = 3.0f + frand() * 5.0f;
                Spawn &sp = s_spawns[e.spawn];
                f32 ang = frand() * HV_TAU, r = sqrtf(frand()) * sp.radius;
                e.wander = sp.pos + Vec3(cosf(ang) * r, 0, sinf(ang) * r);
            }
            Vec3 to = e.wander - a.pos;
            to.y = 0;
            if (to.lenXZ() > 0.8f) {
                Vec3 v = normalize(to) * speed * 0.35f;
                a.vel = v;
                a.yaw = hvApproachAngle(a.yaw, atan2f(to.x, to.z), dt * 4);
            } else {
                a.vel = Vec3();
            }
            if (d.aggroRange > 0 && distP < d.aggroRange && !p.dead && g.mode != MODE_CUTSCENE) {
                e.state = ES_CHASE;
                e.aggro = true;
                fx::floatText(a.pos + Vec3(0, 2.4f * a.scale, 0), "!", ui::RED, 1.2f);
            }
            break;
        }
        case ES_CHASE: {
            if (p.dead) {
                e.state = ES_RETURN;
                break;
            }
            g.combatTimer = 6.0f;
            Vec3 to = p.pos - a.pos;
            to.y = 0;
            a.yaw = hvApproachAngle(a.yaw, atan2f(to.x, to.z), dt * 6);
            f32 range = d.attackRange + p.radius;
            if (distP > range * 0.9f) a.vel = normalize(to) * speed;
            else a.vel = Vec3();
            if (distXZ(a.pos, e.home) > 42) {
                e.state = ES_RETURN;
                break;
            }
            e.attackCd -= dt;
            if (e.attackCd <= 0 && distP <= range) {
                e.attackCount++;
                bool special = false;
                if (a.enemyType == EN_SK_WARRIOR && e.attackCount % 3 == 0) special = true;
                if (a.enemyType == EN_SK_MAGE && e.attackCount % 3 == 0) special = true;
                if (a.enemyType == EN_BARROW_KING && e.attackCount % 3 == 0) special = true;
                if (a.enemyType == EN_SLIME_RED && e.attackCount % 4 == 0) special = true;
                if (special) {
                    e.state = ES_TELEGRAPH;
                    if (a.enemyType == EN_SK_MAGE) {
                        e.telePos = p.pos;
                        e.teleRadius = 4.5f;
                        e.teleKind = 1;
                        fx::aoe(e.telePos, e.teleRadius, 1.8f, d.damage * 2, FX_MAGIC, true);
                    } else if (a.enemyType == EN_BARROW_KING) {
                        // grave eruption: circles around the player
                        for (int k = 0; k < 4; k++) {
                            f32 ang = frand() * HV_TAU, r = k == 0 ? 0 : 3 + frand() * 5;
                            fx::aoe(p.pos + Vec3(cosf(ang) * r, 0, sinf(ang) * r), 3.5f, 1.8f + k * 0.25f, d.damage * 2, FX_DUST, true);
                        }
                        e.telePos = a.pos;
                        e.teleRadius = 0;
                    } else {
                        e.telePos = a.pos + Vec3(sinf(a.yaw), 0, cosf(a.yaw)) * 2.4f * a.scale;
                        e.teleRadius = 3.2f * a.scale;
                        fx::aoe(e.telePos, e.teleRadius, 1.5f, (int)(d.damage * 1.8f), FX_HIT, true);
                    }
                    e.teleTimer = 1.6f;
                    a.vel = Vec3();
                    a.play(a.enemyType == EN_SK_MAGE ? "sk_cast" : "heavy", 0.15f, true, 0.7f);
                    char b[48];
                    snprintf(b, sizeof(b), "%s readies a %s!", d.name, a.enemyType == EN_SK_MAGE ? "Bone Storm" : (a.enemyType == EN_BARROW_KING ? "Grave Eruption" : "Cleave"));
                    toast(b, ui::rgba(255, 160, 90));
                } else {
                    e.state = ES_ATTACK;
                    e.windup = a.enemyType == EN_SK_MAGE ? 0.7f : 0.45f;
                    if (a.skel) a.play(a.enemyType == EN_SK_MAGE ? "cast" : (a.enemyType == EN_SK_ROGUE ? "stab" : "slash"), 0.1f, true, 1.1f);
                    else a.squash = 0;
                }
                e.attackCd = d.attackDelay * (0.85f + frand() * 0.3f);
            }
            // boss summons once at half health
            if (a.enemyType == EN_BARROW_KING && !e.summoned && a.hp < a.maxHp / 2) {
                e.summoned = true;
                toast("The Barrow King calls his guard!", ui::RED);
                for (int k = 0; k < s_numSpawns; k++) {
                    Spawn &sp = s_spawns[k];
                    if (sp.type == EN_SK_MINION && distXZ(sp.pos, a.pos) < 40) {
                        for (int sl = 0; sl < sp.count; sl++)
                            if (sp.actor[sl] < 0 || !g.actors[sp.actor[sl]].active) spawnAt(sp, sl);
                        break;
                    }
                }
            }
            break;
        }
        case ES_ATTACK:
            e.windup -= dt;
            a.vel = Vec3();
            if (e.windup <= 0) {
                if (a.enemyType == EN_SK_MAGE) {
                    projectile(a.handPos(), -2, 3, d.damage, false);
                } else if (distP <= d.attackRange + p.radius + 0.8f) {
                    damagePlayer((int)(d.damage * (0.9f + frand() * 0.2f)), ai);
                }
                e.state = ES_CHASE;
            }
            break;
        case ES_TELEGRAPH:
            e.teleTimer -= dt;
            a.vel = Vec3();
            if (e.teleTimer <= 0) e.state = ES_CHASE;
            break;
        case ES_RETURN: {
            Vec3 to = e.home - a.pos;
            to.y = 0;
            if (to.lenXZ() < 2.0f) {
                e.state = ES_IDLE;
                a.hp = a.maxHp;
                e.aggro = false;
                a.vel = Vec3();
            } else {
                a.vel = normalize(to) * speed;
                a.yaw = hvApproachAngle(a.yaw, atan2f(to.x, to.z), dt * 6);
            }
            break;
        }
        case ES_DEAD:
            a.vel = Vec3();
            a.deadTimer += dt;
            if (a.deadTimer > 3.0f) {
                a.pos.y -= dt * 0.8f;
                if (a.deadTimer > 5.0f) {
                    a.active = false;
                    Spawn &sp = s_spawns[e.spawn];
                    sp.actor[e.slot] = -1;
                    sp.respawn[e.slot] = a.enemyType == EN_BARROW_KING ? 300.0f : 25.0f + frand() * 20.0f;
                }
            }
            break;
    }
    if (!a.dead) {
        actors::move(a, a.vel, dt);
        // locomotion anim for skeletons
        if (a.skel && (e.state == ES_IDLE || e.state == ES_CHASE || e.state == ES_RETURN) && (!a.anim.cur || a.anim.cur->loop || a.anim.finished())) {
            f32 v = a.vel.lenXZ();
            if (v > 2.5f) a.play("sk_run", 0.2f);
            else if (v > 0.3f) a.play("walk", 0.2f);
            else a.play("sk_idle", 0.3f);
        }
    }
    actors::update(a, dt, near);
}

void updateProjectiles(f32 dt) {
    for (Projectile &pr : s_proj) {
        if (!pr.active) continue;
        pr.life -= dt;
        Vec3 tgt;
        if (pr.target == -2) tgt = g.player.pos + Vec3(0, 1.1f, 0);
        else if (pr.target >= 0 && g.actors[pr.target].active && !g.actors[pr.target].dead) tgt = g.actors[pr.target].pos + Vec3(0, 1.0f, 0);
        else {
            pr.active = false;
            continue;
        }
        Vec3 d = tgt - pr.pos;
        f32 len = d.len();
        f32 step = 22.0f * dt;
        static const int TRAIL[5] = {FX_FIRE, FX_ICE, FX_BOLT, FX_MAGIC, FX_DUST};
        if ((g.frame & 1) == 0) fx::burst(pr.pos, TRAIL[pr.kind % 5], 1);
        if (len < step + 0.4f || pr.life <= 0) {
            pr.active = false;
            if (pr.target == -2) damagePlayer(pr.damage, -1);
            else {
                bool crit = frand() < 0.12f;
                hitEnemy(pr.target, crit ? pr.damage * 3 / 2 : pr.damage, crit, TRAIL[pr.kind % 5]);
                if (pr.kind == 1) s_ai[pr.target].slow = 6.0f;
            }
            continue;
        }
        pr.pos += d / len * step;
    }
}

void updateAoes(f32 dt) {
    for (Aoe &ao : s_aoe) {
        if (!ao.active) continue;
        ao.timer -= dt;
        if (ao.timer <= 0) {
            ao.active = false;
            fx::burst(ao.pos + Vec3(0, 0.3f, 0), ao.kind, 18);
            if (ao.hitsPlayer) {
                if (distXZ(ao.pos, g.player.pos) < ao.radius + 0.3f && g.dodgeTimer <= 0) damagePlayer(ao.damage, -1);
            } else {
                for (int i = 0; i < MAX_ACTORS; i++) {
                    Actor &a = g.actors[i];
                    if (a.active && a.kind == AK_ENEMY && !a.dead && distXZ(a.pos, ao.pos) < ao.radius + a.radius) {
                        bool crit = frand() < 0.12f;
                        hitEnemy(i, crit ? ao.damage * 3 / 2 : ao.damage, crit, ao.kind);
                    }
                }
            }
        }
    }
}

// ------------------------------------------------------- player abilities
struct Ability {
    const char *name;
    u8 level;
    u8 mp;
    f32 cast;
    f32 cooldown;      // 0 = uses GCD
    f32 range;
    bool needsTarget;
};
// Slot 0 is the basic attack (B); 1,2,4,5,6,7 sit on the skill wheel (hold R);
// slot 3 is the evade on X.
const Ability WAR[8] = {
    {"Strike", 1, 0, 0, 0, 3.4f, false},
    {"Skull Sunder", 1, 0, 0, 5, 3.6f, true},
    {"Butcher's Block", 2, 0, 0, 9, 3.6f, true},
    {"Roll", 1, 0, 0, 0, 0, false},
    {"Overpower", 3, 0, 0, 10, 0, false},
    {"Rampart", 4, 0, 0, 40, 0, false},
    {"Second Wind", 1, 0, 0, 45, 0, false},
    {"Tomahawk", 5, 0, 0, 7, 20, true},
};
const Ability MAG[8] = {
    {"Spark", 1, 0, 0, 0, 22, false},
    {"Fire", 1, 24, 1.0f, 2.5f, 24, true},
    {"Blizzard", 1, 16, 0, 3.0f, 24, true},
    {"Blink", 1, 0, 0, 0, 0, false},
    {"Thunder", 3, 28, 0, 8, 24, true},
    {"Firestorm", 5, 50, 1.6f, 10, 24, true},
    {"Cure", 1, 30, 1.0f, 6, 0, false},
    {"Aether Ward", 4, 40, 0, 35, 0, false},
};

const Ability &abilityDef(int i) { return g.pd.job == SK_MAGE ? MAG[i] : WAR[i]; }

void executeAbility(int i) {
    Actor &p = g.player;
    bool mage = g.pd.job == SK_MAGE;
    const Ability &ab = abilityDef(i);
    int t = g.target;
    Actor *tgt = (t >= 0 && g.actors[t].active && !g.actors[t].dead) ? &g.actors[t] : nullptr;
    if (tgt) p.yaw = yawTo(p.pos, tgt->pos);
    if (ab.mp) p.mp -= ab.mp;
    if (ab.cooldown > 0) g.cooldowns[i] = ab.cooldown;
    else if (i != 3) g.gcd = 0.8f;
    g.combatTimer = 6.0f;
    if (!mage) {
        switch (i) {
            case 1:   // Skull Sunder: heavy blow that staggers
                p.play("slash_h", 0.06f, true, 1.3f);
                dealPlayerHit(t, 1.9f, FX_HIT);
                if (tgt) s_ai[t].stagger = 1.2f;
                break;
            case 2:   // Butcher's Block: big hit that heals
                p.play("heavy", 0.06f, true, 1.35f);
                dealPlayerHit(t, 2.4f, FX_SPARK);
                p.hp = hvMin(p.maxHp, p.hp + p.maxHp / 12);
                fx::burst(p.pos + Vec3(0, 1, 0), FX_HEAL, 10);
                break;
            case 4:
                p.play("spin", 0.08f, true, 1.3f);
                for (int k = 0; k < MAX_ACTORS; k++) {
                    Actor &a = g.actors[k];
                    if (a.active && a.kind == AK_ENEMY && !a.dead && distXZ(a.pos, p.pos) < 5.0f) dealPlayerHit(k, 1.2f, FX_HIT);
                }
                break;
            case 5:
                g.buffRampart = 12.0f;
                p.play("block", 0.1f, true);
                fx::burst(p.pos + Vec3(0, 1, 0), FX_SPARK, 14);
                toast("Rampart: damage taken -40%", ui::GOLD);
                break;
            case 6:
                p.hp = hvMin(p.maxHp, p.hp + p.maxHp * 3 / 10);
                fx::burst(p.pos + Vec3(0, 1, 0), FX_HEAL, 20);
                audio::sfx(SFX_HEAL);
                break;
            case 7:
                p.play("throw", 0.08f, true, 1.2f);
                projectile(p.handPos(), t, 4, playerDamage(1.0f), true);
                break;
        }
        audio::sfx(SFX_SWING, 1.0f, 0.9f + frand() * 0.2f);
    } else {
        switch (i) {
            case 1:   // Fire
                p.play("cast", 0.06f, true);
                projectile(p.handPos(), t, 0, playerDamage(2.0f), true);
                audio::sfx(SFX_FIRE);
                break;
            case 2:   // Blizzard: freezes in place
                p.play("cast", 0.06f, true, 1.3f);
                projectile(p.handPos(), t, 1, playerDamage(1.3f), true);
                if (tgt) s_ai[t].stagger = 2.0f;
                audio::sfx(SFX_ICE);
                break;
            case 4:   // Thunder: damage over time
                p.play("cast_raise", 0.06f, true, 1.4f);
                if (tgt) {
                    dealPlayerHit(t, 0.8f, FX_BOLT);
                    s_ai[t].dotTimer = 15.0f;
                    s_ai[t].dotTick = 3.0f;
                    s_ai[t].dotDmg = playerDamage(0.35f);
                }
                audio::sfx(SFX_FIRE, 0.8f, 1.4f);
                break;
            case 5:   // Firestorm
                p.play("cast_long", 0.06f, true, 1.4f);
                if (tgt) fx::aoe(tgt->pos, 6.0f, 0.6f, playerDamage(1.7f), FX_FIRE, false);
                audio::sfx(SFX_FIRE);
                break;
            case 6:   // Cure
                p.play("cast_raise", 0.06f, true);
                p.hp = hvMin(p.maxHp, p.hp + p.maxHp * 30 / 100);
                fx::burst(p.pos + Vec3(0, 1, 0), FX_HEAL, 24);
                audio::sfx(SFX_HEAL);
                break;
            case 7:   // Aether Ward
                g.wardShield = (f32)(p.maxHp / 5);
                fx::burst(p.pos + Vec3(0, 1, 0), FX_MAGIC, 24);
                toast("Aether Ward", ui::BLUE);
                break;
        }
    }
}

void useConsumable(int k) {
    // L+A heal, L+X mana, L+Y buff
    static const u16 HEAL[] = {IT_MOON_ELIXIR, IT_GOLDEN_FEAST, IT_EMBER_TONIC, IT_PIKE_PIE, IT_HERB_SALMON, IT_LAVENDER_ELIXIR,
                               IT_PERCH_STEW, IT_POTION_MINOR, IT_GRILLED_TROUT, IT_MINT_TEA};
    Actor &p = g.player;
    if (g.cooldowns[7] > 0 && k != 3) {
        toast("Item cooldown...", ui::TEXT_DIM);
        return;
    }
    u16 use = 0;
    if (k == 0) {
        if (p.hp >= p.maxHp) { toast("You're already at full health.", ui::TEXT_DIM); return; }
        // the smallest heal that covers what's missing, else the biggest there is
        int missing = p.maxHp - p.hp;
        for (u16 it : HEAL) {
            if (!g.pd.inv.count(it)) continue;
            if (!use || ITEMS[it].power >= missing) use = it;
        }
        if (!use) { toast("No food or potions!", ui::RED); return; }
        p.hp = hvMin(p.maxHp, p.hp + ITEMS[use].power);
        fx::burst(p.pos + Vec3(0, 1, 0), FX_HEAL, 16);
    } else if (k == 1) {
        use = g.pd.inv.count(IT_SAGE_TONIC) ? IT_SAGE_TONIC : (g.pd.inv.count(IT_MINT_TEA) ? IT_MINT_TEA : 0);
        if (!use) { toast("No tonics!", ui::RED); return; }
        p.mp = hvMin(p.maxMp, p.mp + 150);
        g.gp = hvMin(g.maxGp, g.gp + 150);
        fx::burst(p.pos + Vec3(0, 1, 0), FX_MAGIC, 12);
    } else if (k == 2) {
        use = g.pd.inv.count(IT_SUNPETAL_DRAUGHT) ? IT_SUNPETAL_DRAUGHT : 0;
        if (!use) { toast("No draughts!", ui::RED); return; }
        g.buffDamage = 60.0f;
        fx::burst(p.pos + Vec3(0, 1, 0), FX_GOLD, 16);
    } else {
        return;
    }
    g.pd.inv.remove(use, 1);
    g.cooldowns[7] = 4.0f;
    p.play("use_item", 0.1f, true, 1.4f);
    audio::sfx(SFX_HEAL);
    char b[48];
    snprintf(b, sizeof(b), "Used %s", ITEMS[use].name);
    toast(b, ui::GREEN, use);
}

}  // namespace

void init() {
    s_numSpawns = 0;
    s_aoeTex = tex::get("tx/aoe");
    s_targetTex = tex::get("tx/ui_target");
    for (int i = 0; i < g_world.numMarkers() && s_numSpawns < 48; i++) {
        const Marker &m = g_world.marker(i);
        if (m.kind != MK_SPAWN) continue;
        Spawn &sp = s_spawns[s_numSpawns++];
        sp.type = (u8)m.id;
        sp.pos = m.pos;
        sp.radius = m.radius;
        sp.count = hvClamp<int>((int)m.extra, 1, 8);
        sp.spawnedOnce = 0;
        for (int k = 0; k < 8; k++) {
            sp.actor[k] = -1;
            sp.respawn[k] = 0.5f + k * 0.3f;
        }
    }
    for (Projectile &p : s_proj) p.active = false;
    for (Aoe &a : s_aoe) a.active = false;
}

bool inCombat() { return g.combatTimer > 0; }

int spawnEnemy(int type, const Vec3 &pos, int spawnIndex) {
    (void)type; (void)pos; (void)spawnIndex;
    return -1;
}

void clearTarget() { g.target = -1; }

int nearestEnemy(const Vec3 &p, f32 maxDist, int skip) {
    int best = -1;
    f32 bd = maxDist;
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor &a = g.actors[i];
        if (!a.active || a.kind != AK_ENEMY || a.dead || i == skip) continue;
        f32 d = distXZ(a.pos, p);
        // prefer enemies in front of the camera
        Vec3 fwd(sinf(g.camYaw), 0, cosf(g.camYaw));
        Vec3 to = a.pos - p;
        if (d > 3 && dot(normalize(Vec3(to.x, 0, to.z)), fwd) < -0.2f) d *= 1.6f;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

void damagePlayer(int amount, int source) {
    if (g.guarding) {
        amount = amount * 35 / 100;
        g.player.play("block_hit", 0.05f, true, 1.4f);
        fx::burst(g.player.pos + Vec3(0, 1.2f, 0), FX_SPARK, 6);
    }
    Actor &p = g.player;
    if (p.dead || g.mode == MODE_CUTSCENE) return;
    if (g.dodgeTimer > 0) {
        fx::floatText(p.pos + Vec3(0, 2.4f, 0), "Dodge", ui::TEXT_DIM, 0.9f);
        return;
    }
    int def = (g.pd.armor ? ITEMS[g.pd.armor].power : 0) + (g.pd.accessory ? ITEMS[g.pd.accessory].power : 0);
    f32 red = (f32)def / (def + 120.0f);
    f32 dmg = amount * (1.0f - red);
    if (g.buffRampart > 0) dmg *= 0.6f;
    int d = hvMax(1, (int)dmg);
    if (g.wardShield > 0) {
        f32 absorb = hvMin(g.wardShield, (f32)d);
        g.wardShield -= absorb;
        d -= (int)absorb;
    }
    p.hp -= d;
    p.hitFlash = 1.0f;
    g.combatTimer = 6.0f;
    char b[16];
    snprintf(b, sizeof(b), "-%d", d);
    fx::floatText(p.pos + Vec3(0, 2.3f, 0), b, ui::RED);
    audio::sfx(SFX_HURT);
    plat::rumble(0.18f);
    if (source >= 0 && g.target < 0) g.target = source;   // auto-target attackers
    // an attack pulls you out of whatever you were doing
    if (g.mode == MODE_GATHER || g.mode == MODE_FISH) gather::interrupt();
    else if (g.mode == MODE_CRAFT || g.mode == MODE_CRAFT_SELECT) craft::interrupt();
    else if (g.mode == MODE_SHOP || g.mode == MODE_BOARD || g.mode == MODE_REST) g.mode = MODE_PLAY;
    if (p.hp <= 0) {
        p.hp = 0;
        p.dead = true;
        p.play("death", 0.1f, true);
        g.mode = MODE_DEAD;
        g.target = -1;
        showBanner("Defeated", "Press A to return to the Hearth", 999);
    }
}

void update(f32 dt) {
    // spawns
    bool night = isNight();
    for (int i = 0; i < s_numSpawns; i++) {
        Spawn &sp = s_spawns[i];
        bool allowed = !nightType(sp.type) || night;
        if (sp.type == EN_BARROW_KING && !(quests::isActive(7) || quests::isDone(7))) allowed = false;
        for (int k = 0; k < sp.count; k++) {
            int ai = sp.actor[k];
            if (ai >= 0 && g.actors[ai].active) {
                // night-only enemies sink away at dawn when idle
                if (!allowed && s_ai[ai].state == ES_IDLE && distXZ(g.actors[ai].pos, g.player.pos) > 20) {
                    g.actors[ai].active = false;
                    sp.actor[k] = -1;
                }
                continue;
            }
            if (!allowed) continue;
            sp.respawn[k] -= dt;
            bool first = !(sp.spawnedOnce & (1 << k));
            if (sp.respawn[k] <= 0 && (first || distXZ(sp.pos, g.player.pos) > 18)) {
                spawnAt(sp, k);
                sp.spawnedOnce |= (u8)(1 << k);
            }
        }
    }
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor &a = g.actors[i];
        if (a.active && a.kind == AK_ENEMY) updateEnemy(i, dt);
    }
    updateProjectiles(dt);
    updateAoes(dt);
    if (g.combatTimer > 0) g.combatTimer -= dt;
    if (g.gcd > 0) g.gcd -= dt;
    for (f32 &c : g.cooldowns)
        if (c > 0) c -= dt;
    if (g.comboTimer > 0) {
        g.comboTimer -= dt;
        if (g.comboTimer <= 0) g.combo = 0;
    }
    if (g.buffRampart > 0) g.buffRampart -= dt;
    if (g.buffDamage > 0) g.buffDamage -= dt;
    if (g.target >= 0) {
        Actor &t = g.actors[g.target];
        if (!t.active || t.dead || distXZ(t.pos, g.player.pos) > 45) g.target = -1;
    }
    if (g.mode == MODE_DEAD) {
        actors::update(g.player, 0, true);
        if (g.pad.pressed & BTN_A) {
            // return to the Hearth
            Actor &p = g.player;
            p.dead = false;
            p.hp = p.maxHp;
            p.mp = p.maxMp;
            p.pos = Vec3(202, 0, 205);
            p.pos.y = g_world.groundHeight(p.pos.x, p.pos.z);
            p.play("idle", 0);
            g.mode = MODE_PLAY;
            g.bannerTimer = 0;
            g.fadeIn = 1.0f;
            for (int i = 0; i < MAX_ACTORS; i++)
                if (g.actors[i].active && g.actors[i].kind == AK_ENEMY && s_ai[i].state != ES_DEAD) s_ai[i].state = ES_RETURN;
        }
    }
}

// Best enemy to swing or cast at: the locked target if close enough, else the
// nearest enemy roughly in front of the player (or anywhere very close).
static int autoAim(f32 range) {
    Actor &p = g.player;
    if (g.target >= 0) {
        Actor &t = g.actors[g.target];
        if (t.active && !t.dead && distXZ(t.pos, p.pos) <= range + t.radius) return g.target;
    }
    Vec3 fwd(sinf(p.yaw), 0, cosf(p.yaw));
    int best = -1;
    f32 bs = 1e9f;
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor &a = g.actors[i];
        if (!a.active || a.kind != AK_ENEMY || a.dead) continue;
        Vec3 d = a.pos - p.pos;
        d.y = 0;
        f32 dist = d.lenXZ();
        if (dist > range + a.radius) continue;
        f32 facing = dist > 0.01f ? dot(d, fwd) / dist : 1.0f;
        if (facing < -0.2f && dist > 2.5f) continue;
        f32 score = dist * (2.0f - facing);
        if (score < bs) bs = score, best = i;
    }
    return best;
}

static bool tryAbility(int i) {
    Actor &p = g.player;
    const Ability &ab = abilityDef(i);
    if (skillLevel(g.pd.job) < ab.level) {
        toast("Not learned yet", ui::RED);
        audio::sfx(SFX_FAIL);
        return false;
    }
    if (ab.cooldown > 0 && g.cooldowns[i] > 0) {
        audio::sfx(SFX_FAIL, 0.5f);
        return false;
    }
    if (p.mp < ab.mp) {
        toast("Not enough MP", ui::RED);
        audio::sfx(SFX_FAIL);
        return false;
    }
    if (ab.needsTarget) {
        int t = autoAim(ab.range);
        if (t < 0) {
            toast("No enemy in reach", ui::TEXT_DIM);
            return false;
        }
        g.target = t;
    }
    if (ab.cast > 0) {
        g.castTimer = g.castTotal = ab.cast;
        g.castAbility = (u8)i;
        p.play("casting", 0.1f);
        if (ab.cooldown > 0) g.cooldowns[i] = ab.cooldown;
        return true;
    }
    executeAbility(i);
    return true;
}

// melee damage lands when the swing connects, not on the button press
static f32 s_hitPending = -1.0f;
static int s_hitStep = 0;
static f32 s_hitStop = 0;

static void meleeImpact(int step) {
    static const f32 POT_W[3] = {0.8f, 0.9f, 1.6f};
    Actor &p = g.player;
    Vec3 fwd(sinf(p.yaw), 0, cosf(p.yaw));
    f32 reach = step == 2 ? 3.8f : 3.2f;
    bool any = false;
    for (int k = 0; k < MAX_ACTORS; k++) {
        Actor &a = g.actors[k];
        if (!a.active || a.kind != AK_ENEMY || a.dead) continue;
        Vec3 d = a.pos - p.pos;
        d.y = 0;
        f32 dist = d.lenXZ();
        if (dist > reach + a.radius) continue;
        if (dist > 0.8f && dot(d, fwd) / dist < 0.25f) continue;
        dealPlayerHit(k, POT_W[step], step == 2 ? FX_SPARK : FX_HIT);
        if (step == 2) s_ai[k].stagger = hvMax(s_ai[k].stagger, 0.45f);
        // knockback (bosses barely budge)
        f32 push = (step == 2 ? 0.7f : 0.22f) * (a.enemyType == EN_BARROW_KING ? 0.2f : 1.0f);
        Vec3 np = a.pos + (dist > 0.01f ? d * (1.0f / dist) : fwd) * push;
        if (g_world.walkable(np.x, np.z, a.pos.y)) {
            a.pos = np;
            g_world.resolveCircle(a.pos, a.radius);
        }
        any = true;
    }
    if (any) s_hitStop = step == 2 ? 0.1f : 0.055f;
}

static void basicAttack() {
    Actor &p = g.player;
    bool mage = g.pd.job == SK_MAGE;
    int t = autoAim(mage ? 22.0f : 4.5f);
    if (t >= 0) p.yaw = yawTo(p.pos, g.actors[t].pos);
    int step = s_atkWindow > 0 ? s_atkStep : 0;
    static const char *const ANIM_W[3] = {"slash", "slash_h", "heavy"};
    g.combatTimer = 6.0f;
    if (!mage) {
        p.play(ANIM_W[step], 0.05f, true, step == 2 ? 1.45f : 1.7f);
        // lunge a little and hit everything in a frontal arc
        Vec3 fwd(sinf(p.yaw), 0, cosf(p.yaw));
        p.vel = fwd * (step == 2 ? 6.0f : 3.5f);
        s_hitPending = step == 2 ? 0.2f : 0.11f;
        s_hitStep = step;
        audio::sfx(SFX_SWING, 1.0f, 0.9f + step * 0.08f);
        s_atkTimer = step == 2 ? 0.62f : 0.36f;
    } else {
        p.play("cast", 0.05f, true, 1.8f);
        if (t >= 0) projectile(p.handPos(), t, step == 2 ? 3 : 2, playerDamage(step == 2 ? 1.3f : 0.6f), true);
        else fx::burst(p.handPos(), FX_MAGIC, 6);
        audio::sfx(SFX_ICE, 0.6f, 1.6f + step * 0.1f);
        s_atkTimer = step == 2 ? 0.55f : 0.32f;
    }
    s_atkStep = (step + 1) % 3;
    s_atkWindow = s_atkTimer + 0.55f;
}

static void evade() {
    Actor &p = g.player;
    if (g.pd.job == SK_MAGE) {
        // Blink: a short teleport in the stick direction (or backwards)
        if (g.cooldowns[3] > 0) return;
        Vec3 fwd(sinf(g.camYaw), 0, cosf(g.camYaw)), right(-cosf(g.camYaw), 0, sinf(g.camYaw));
        Vec3 d = fwd * g.pad.sy + right * g.pad.sx;
        if (d.lenXZ() < 0.2f) d = Vec3(-sinf(p.yaw), 0, -cosf(p.yaw));
        d = normalize(d);
        for (f32 dist = 8.0f; dist > 2.0f; dist -= 1.0f) {
            Vec3 np = p.pos + d * dist;
            if (!g_world.walkable(np.x, np.z, p.pos.y)) continue;
            fx::burst(p.pos + Vec3(0, 1, 0), FX_MAGIC, 14);
            p.pos = np;
            p.pos.y = g_world.groundHeight(np.x, np.z);
            g_world.resolveCircle(p.pos, p.radius);
            fx::burst(p.pos + Vec3(0, 1, 0), FX_MAGIC, 14);
            g.dodgeTimer = 0.0f;
            g.cooldowns[3] = 2.0f;
            audio::sfx(SFX_ICE, 0.7f, 1.8f);
            return;
        }
        return;
    }
    if (g.dodgeTimer > 0 || g.cooldowns[3] > 0) return;
    Vec3 fwd(sinf(g.camYaw), 0, cosf(g.camYaw)), right(-cosf(g.camYaw), 0, sinf(g.camYaw));
    Vec3 d = fwd * g.pad.sy + right * g.pad.sx;
    if (d.lenXZ() < 0.2f) d = Vec3(sinf(p.yaw), 0, cosf(p.yaw));
    g.dodgeDir = normalize(d);
    g.dodgeTimer = 0.35f;
    g.cooldowns[3] = 0.75f;
    p.yaw = atan2f(g.dodgeDir.x, g.dodgeDir.z);
    p.play("roll", 0.04f, true, 1.6f);
    audio::sfx(SFX_SWING, 0.7f, 0.7f);
}

void playerUpdate(f32 dt) {
    Actor &p = g.player;
    PadState &pad = g.pad;
    if (s_atkTimer > 0) s_atkTimer -= dt;
    if (s_atkWindow > 0) s_atkWindow -= dt;
    if (s_hitStop > 0) s_hitStop -= dt;
    if (s_hitPending >= 0) {
        s_hitPending -= dt;
        if (s_hitPending < 0) {
            if (g.dodgeTimer <= 0 && !p.dead) meleeImpact(s_hitStep);
            s_hitPending = -1.0f;
        }
    }
    // Z: lock on / cycle targets (re-centre the camera when nothing is around)
    if (pad.pressed & BTN_Z) {
        int n = nearestEnemy(p.pos, 26.0f, g.target);
        if (n < 0 && g.target >= 0) g.target = -1;
        else if (n >= 0) {
            g.target = n;
            audio::sfx(SFX_UI_MOVE);
        } else {
            g.camYaw = p.yaw;
        }
    }
    // D-pad left/right picks the quick item on Y
    if (pad.pressed & BTN_RIGHT) s_quickSlot = (s_quickSlot + 1) % 3, audio::sfx(SFX_UI_MOVE);
    if (pad.pressed & BTN_LEFT) s_quickSlot = (s_quickSlot + 2) % 3, audio::sfx(SFX_UI_MOVE);
    if (pad.pressed & BTN_Y) useConsumable(s_quickSlot);
    // skill wheel: hold R, aim the stick at a skill, release R to use it
    bool R = (pad.held & BTN_R) != 0;
    if (s_wheelOpen) {
        s_wheelAnim = hvMin(1.0f, s_wheelAnim + dt * 8.0f);
        f32 mag = sqrtf(pad.sx * pad.sx + pad.sy * pad.sy);
        if (mag > 0.5f) {
            f32 a = atan2f(pad.sx, pad.sy);   // 0 = up, clockwise
            if (a < 0) a += HV_TAU;
            int sel = (int)((a + HV_TAU / 12.0f) / (HV_TAU / 6.0f)) % 6;
            if (sel != s_wheelSel) audio::sfx(SFX_UI_MOVE, 0.6f);
            s_wheelSel = sel;
        }
        if (!R) {
            s_wheelOpen = false;
            if (s_wheelSel >= 0) tryAbility(WHEEL[s_wheelSel]);
        }
        return;
    }
    if ((pad.pressed & BTN_R) && g.pd.weapon && g.castTimer <= 0) {
        s_wheelOpen = true;
        s_wheelSel = -1;
        s_wheelAnim = 0;
        audio::sfx(SFX_UI_OK, 0.6f);
        return;
    }
    // casting
    if (g.castTimer > 0) {
        g.castTimer -= dt;
        if (g.castTimer <= 0) executeAbility(g.castAbility);
        return;
    }
    if (g.pd.weapon == 0) return;
    // L: guard
    g.guarding = (pad.held & BTN_L) && g.dodgeTimer <= 0 && !p.dead;
    if (g.guarding) {
        if (!p.anim.cur || p.anim.cur->loop) p.play("block", 0.1f);
        return;
    }
    // X: roll / blink
    if (pad.pressed & BTN_X) {
        evade();
        return;
    }
    // B: basic attack combo (presses during a swing are buffered)
    if (pad.pressed & BTN_B) s_atkQueued = true;
    if (s_atkQueued && s_atkTimer <= 0 && g.dodgeTimer <= 0) {
        s_atkQueued = false;
        basicAttack();
    }
}

bool wheelOpen() { return s_wheelOpen; }
f32 timeScale() { return s_wheelOpen ? 0.15f : (s_hitStop > 0 ? 0.06f : 1.0f); }
const char *quickItemName() {
    static const char *const N[3] = {"Heal", "Tonic", "Draught"};
    return N[s_quickSlot];
}
int quickSlot() { return s_quickSlot; }

void projectile(const Vec3 &from, int target, int kind, int damage, bool fromPlayer) {
    for (Projectile &pr : s_proj) {
        if (pr.active) continue;
        pr.active = true;
        pr.pos = from;
        pr.target = target;
        pr.kind = kind;
        pr.damage = damage;
        pr.life = 3.0f;
        pr.fromPlayer = fromPlayer;
        return;
    }
}

void aoe(const Vec3 &p, f32 radius, f32 delay, int damage, int kind, bool hitsPlayer) {
    for (Aoe &a : s_aoe) {
        if (a.active) continue;
        a.active = true;
        a.pos = p;
        a.radius = radius;
        a.timer = a.total = delay;
        a.damage = damage;
        a.kind = kind;
        a.hitsPlayer = hitsPlayer;
        return;
    }
}

static void groundQuad(const Vec3 &c, f32 r, GXColor col) {
    gfx::beginImm(GX_QUADS, 4, true);
    f32 xs[4] = {c.x - r, c.x + r, c.x + r, c.x - r}, zs[4] = {c.z - r, c.z - r, c.z + r, c.z + r};
    f32 us[4] = {0, 1, 1, 0}, vs[4] = {0, 0, 1, 1};
    for (int k = 0; k < 4; k++) {
        f32 y = g_world.groundHeight(xs[k], zs[k], c.y + 1) + 0.12f;
        GX_Position3f32(xs[k], y, zs[k]);
        GX_Color4u8(col.r, col.g, col.b, col.a);
        GX_TexCoord2f32(us[k], vs[k]);
    }
    GX_End();
}

void draw() {
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor &a = g.actors[i];
        if (!a.active || a.kind != AK_ENEMY) continue;
        if (distXZ(a.pos, g.cam.eye) > 75) continue;
        actors::draw(a);
    }
    gfx::loadWorld(Mat34::identity());
    // telegraphs
    if (s_aoeTex) {
        gfx::setShade(SH_TEX | SH_VCOL | SH_BLEND | SH_DOUBLESIDED | SH_NOZWRITE);
        tex::bind(s_aoeTex, GX_TEXMAP0);
        for (Aoe &a : s_aoe) {
            if (!a.active) continue;
            f32 t = 1.0f - a.timer / a.total;
            GXColor c = a.hitsPlayer ? gxc(255, 120, 40, (u8)(120 + 100 * t)) : gxc(120, 200, 255, 150);
            groundQuad(a.pos, a.radius, c);
            groundQuad(a.pos, a.radius * t, gxc(c.r, c.g, c.b, 90));
        }
    }
    // target ring
    if (g.target >= 0 && s_targetTex) {
        Actor &t = g.actors[g.target];
        gfx::setShade(SH_TEX | SH_VCOL | SH_ADDITIVE | SH_DOUBLESIDED);
        tex::bind(s_targetTex, GX_TEXMAP0);
        f32 pulse = 1.0f + 0.08f * sinf(g.time * 6);
        groundQuad(t.pos, (t.radius + 0.7f) * t.scale * pulse, gxc(255, 90, 70, 230));
    }
    // projectiles as glowing cores
    gfx::setShade(SH_TEX | SH_VCOL | SH_ADDITIVE | SH_DOUBLESIDED | SH_NOFOG);
    const Texture *glow = tex::get("tx/glow");
    tex::bind(glow, GX_TEXMAP0);
    const Camera &cam = gfx::camera();
    Vec3 right(cam.view.m[0][0], cam.view.m[0][1], cam.view.m[0][2]), up(cam.view.m[1][0], cam.view.m[1][1], cam.view.m[1][2]);
    for (Projectile &pr : s_proj) {
        if (!pr.active) continue;
        static const GXColor COL[5] = {{255, 140, 50, 255}, {140, 210, 255, 255}, {230, 220, 90, 255}, {190, 110, 255, 255}, {230, 230, 230, 255}};
        GXColor c = COL[pr.kind % 5];
        Vec3 r = right * 0.7f, u = up * 0.7f;
        Vec3 q0 = pr.pos - r + u, q1 = pr.pos + r + u, q2 = pr.pos + r - u, q3 = pr.pos - r - u;
        gfx::beginImm(GX_QUADS, 4, true);
        GX_Position3f32(q0.x, q0.y, q0.z); GX_Color4u8(c.r, c.g, c.b, 255); GX_TexCoord2f32(0, 0);
        GX_Position3f32(q1.x, q1.y, q1.z); GX_Color4u8(c.r, c.g, c.b, 255); GX_TexCoord2f32(1, 0);
        GX_Position3f32(q2.x, q2.y, q2.z); GX_Color4u8(c.r, c.g, c.b, 255); GX_TexCoord2f32(1, 1);
        GX_Position3f32(q3.x, q3.y, q3.z); GX_Color4u8(c.r, c.g, c.b, 255); GX_TexCoord2f32(0, 1);
        GX_End();
    }
}

void drawUi() {
    f32 W = ui::width();
    // enemy health bars above heads
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor &a = g.actors[i];
        if (!a.active || a.kind != AK_ENEMY || a.dead) continue;
        if (s_ai[i].hpBarTimer <= 0 && i != g.target) continue;
        bool on;
        Vec3 s = screenProject(a.headPos() + Vec3(0, 0.5f, 0), &on);
        if (!on || s.z > 45) continue;
        ui::bar(s.x - 26, s.y, 52, 5, (f32)a.hp / a.maxHp, ui::rgba(230, 70, 60));
    }
    // target frame
    if (g.target >= 0) {
        Actor &t = g.actors[g.target];
        // between the player frame (left) and the minimap (right)
        f32 tw = hvMin(300.0f, W - 150 - 268);
        f32 x = 268, y = 18;
        ui::panel(x, y, tw, 52, ui::PANEL);
        char b[64];
        snprintf(b, sizeof(b), "Lv %d  %s", t.level, t.name);
        ui::text(FONT_UI, x + 14, y + 6, b, t.enemyType == EN_BARROW_KING ? ui::GOLD : ui::WHITE);
        ui::bar(x + 14, y + 33, tw - 72, 9, (f32)t.hp / t.maxHp, ui::rgba(230, 70, 60));
        snprintf(b, sizeof(b), "%d%%", t.hp * 100 / hvMax(1, t.maxHp));
        ui::text(FONT_SMALL, x + tw - 14, y + 27, b, ui::TEXT_DIM, AL_RIGHT);
    }
    // skill wheel
    if (s_wheelOpen) {
        f32 H = ui::height();
        f32 k = s_wheelAnim;
        ui::rect(0, 0, W, H, ui::rgba(10, 8, 20, (u8)(90 * k)));
        f32 cx = W * 0.5f, cy = H * 0.5f + 10;
        f32 rad = 112 * (0.7f + 0.3f * k);
        ui::sprite(hvHash("tx/ui_circle"), cx - 34, cy - 34, 68, 68, ui::rgba(30, 26, 40, 220));
        ui::text(FONT_SMALL, cx, cy - 9, g.pd.job == SK_MAGE ? "Magic" : "Skills", ui::GOLD, AL_CENTER);
        for (int n = 0; n < 6; n++) {
            int ai = WHEEL[n];
            const Ability &ab = abilityDef(ai);
            f32 a = n * HV_TAU / 6.0f;
            f32 x = cx + sinf(a) * rad, y = cy - cosf(a) * rad;
            bool sel = n == s_wheelSel;
            bool learned = skillLevel(g.pd.job) >= ab.level;
            bool ready = learned && (ab.cooldown <= 0 || g.cooldowns[ai] <= 0) && g.player.mp >= ab.mp;
            f32 bw = sel ? 132 : 118, bh = sel ? 50 : 44;
            ui::panel(x - bw * 0.5f, y - bh * 0.5f, bw, bh, sel ? ui::SEL : ui::rgba(28, 24, 38, 230), 12);
            if (ab.cooldown > 0 && g.cooldowns[ai] > 0) {
                f32 f = hvSaturate(g.cooldowns[ai] / ab.cooldown);
                ui::rect(x - bw * 0.5f + 4, y + bh * 0.5f - 7, (bw - 8) * f, 3, ui::rgba(140, 180, 255, 220));
            }
            char b[40];
            bool sub = !learned || ab.mp;
            ui::text(FONT_SMALL, x, y - (sub ? 15 : 9), ab.name, ready ? ui::WHITE : ui::TEXT_DIM, AL_CENTER);
            if (!learned) {
                snprintf(b, sizeof(b), "Learn at Lv %d", ab.level);
                ui::text(FONT_SMALL, x, y + 2, b, sel ? ui::rgba(120, 30, 20) : ui::RED, AL_CENTER, 0.85f);
            } else if (ab.mp) {
                snprintf(b, sizeof(b), "%d MP", ab.mp);
                ui::text(FONT_SMALL, x, y + 2, b, ui::BLUE, AL_CENTER, 0.85f);
            }
        }
        ui::text(FONT_SMALL, cx, cy + rad + 34, "Tilt the stick to choose - release R to use", ui::TEXT_DIM, AL_CENTER);
    }
    // cast bar
    if (g.castTimer > 0) {
        f32 x = W * 0.5f - 110, y = ui::height() - 150.0f;
        const Ability &ab = abilityDef(g.castAbility);
        ui::text(FONT_SMALL, W * 0.5f, y - 18, ab.name, ui::WHITE, AL_CENTER);
        ui::bar(x, y, 220, 8, 1.0f - g.castTimer / g.castTotal, ui::rgba(200, 150, 255));
    }
}

// expose ability info to the HUD hotbar
const char *abilityName(int i) { return abilityDef(i).name; }
int abilityLevel(int i) { return abilityDef(i).level; }
f32 abilityCooldown(int i) {
    const Ability &ab = abilityDef(i);
    if (ab.cooldown > 0) return hvMax(0.0f, g.cooldowns[i]) / ab.cooldown;
    return i == 3 ? 0 : hvMax(0.0f, g.gcd) / 2.2f;
}

}  // namespace combat

namespace fx {
void projectile(const Vec3 &from, int targetActor, int kind, int damage, bool fromPlayer) {
    combat::projectile(from, targetActor, kind, damage, fromPlayer);
}
void aoe(const Vec3 &p, f32 radius, f32 delay, int damage, int kind, bool hitsPlayer) {
    combat::aoe(p, radius, delay, damage, kind, hitsPlayer);
}
}  // namespace fx

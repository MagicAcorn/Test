// PC harness: an autopilot that plays the main story using only controller
// input (stick + buttons), to find progression blockers. HV_SCENARIO=story.
// It reports each quest step and anything it could not do on its own.
#ifdef HV_PC
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/game.h"
#include "game/world.h"

namespace {

int s_wait = 0;            // frames until the next button press
Vec3 s_lastPos;
int s_stuckFrames = 0, s_sidestep = 0;
f32 s_sideSign = 1;
int s_teleports = 0;
int s_lastQuest = -1, s_lastStep = -1;
int s_stepFrames = 0;
char s_status[220];

void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char *fmt, ...) {
    char b[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (strcmp(b, s_status) != 0) {
        snprintf(s_status, sizeof(s_status), "%s", b);
        printf("[bot] f%u day %u %05.2fh  %s\n", g.frame, g.pd.day, g.pd.hour, b);
    }
}

void press(u16 b, int gap = 12) {
    if (s_wait > 0) return;
    g.pad.pressed |= b;
    g.pad.held |= b;
    s_wait = gap;
}

bool crossesWater(const Vec3 &a, const Vec3 &b) {
    f32 len = distXZ(a, b);
    int n = (int)(len / 0.8f);
    for (int i = 1; i < n; i++) {
        Vec3 q = lerp(a, b, (f32)i / n);
        if (g_world.isWater(q.x, q.z)) return true;
    }
    return false;
}

// Rivers are only crossable on bridges: route through the best one.
Vec3 routeVia(const Vec3 &target) {
    const Vec3 &p = g.player.pos;
    if (!crossesWater(p, target)) return target;
    int best = -1;
    f32 bc = 1e9f;
    Vec3 nearEnd, farEnd;
    for (int i = 0; i < g_world.numBridges(); i++) {
        const Bridge &b = g_world.bridge(i);
        Vec3 e0(b.x - b.halfLen - 1.5f, 0, b.z), e1(b.x + b.halfLen + 1.5f, 0, b.z);
        if (distXZ(p, e1) < distXZ(p, e0)) {
            Vec3 t = e0;
            e0 = e1;
            e1 = t;
        }
        f32 c = distXZ(p, e0) + distXZ(e1, target);
        if (c < bc) bc = c, best = i, nearEnd = e0, farEnd = e1;
    }
    if (best < 0) return target;
    const Bridge &b = g_world.bridge(best);
    bool onBridge = fabsf(p.x - b.x) <= b.halfLen + 2.6f && fabsf(p.z - b.z) <= b.halfWidth;
    return onBridge ? farEnd : nearEnd;
}

// Steer with the stick toward p; returns true when within `reach`.
bool walkTo(const Vec3 &goal, f32 reach) {
    Vec3 p = routeVia(goal);
    if (distXZ(p, goal) > 0.1f) reach = 0.8f;   // heading for a bridge end
    if (g.frame % 600 == 0)
        printf("[bot]   walking (%.1f, %.1f) -> (%.1f, %.1f) dist %.1f mode %d\n", g.player.pos.x, g.player.pos.z, goal.x, goal.z, distXZ(goal, g.player.pos), g.mode);
    Vec3 d = p - g.player.pos;
    d.y = 0;
    f32 dist = d.lenXZ();
    if (dist <= reach) {
        g.pad.sx = g.pad.sy = 0;
        s_stuckFrames = 0;
        return distXZ(p, goal) < 0.1f;
    }
    d = d * (1.0f / dist);
    if (s_sidestep > 0) {
        s_sidestep--;
        d = Vec3(d.z * s_sideSign, 0, -d.x * s_sideSign) * 0.8f + d * 0.2f;
    }
    Vec3 fwd(sinf(g.camYaw), 0, cosf(g.camYaw));
    Vec3 right(-cosf(g.camYaw), 0, sinf(g.camYaw));
    f32 slow = dist < 2.5f ? 0.55f : 1.0f;
    g.pad.sy = dot(d, fwd) * slow;
    g.pad.sx = dot(d, right) * slow;
    // stuck detection: sidestep, and as a last resort teleport (reported)
    if (g.frame % 60 == 0) {
        if (distXZ(g.player.pos, s_lastPos) < 1.0f) {
            s_stuckFrames += 60;
            s_sidestep = 50;
            s_sideSign = -s_sideSign;
        } else {
            s_stuckFrames = 0;
        }
        s_lastPos = g.player.pos;
    }
    if (s_stuckFrames > 900) {
        printf("[bot] !! stuck near (%.0f, %.0f) heading to (%.0f, %.0f) - teleporting\n", g.player.pos.x, g.player.pos.z, p.x, p.z);
        Vec3 t = p - d * (reach * 0.8f);
        g.player.pos = t;
        g.player.pos.y = g_world.groundHeight(t.x, t.z);
        s_stuckFrames = 0;
        s_teleports++;
    }
    return false;
}

int npcActor(int npc) { return npcs::actorForNpc(npc); }

bool talkTo(int npc) {
    int a = npcActor(npc);
    if (a < 0) {
        say("cannot find npc %d", npc);
        return false;
    }
    if (g.interact == Game::IK_NPC && g.interactIndex == a) {
        g.pad.sx = g.pad.sy = 0;
        press(BTN_A, 20);
        return true;
    }
    walkTo(g.actors[a].pos, 1.0f);
    return true;
}

int stationIndex(int type) {
    int best = -1;
    f32 bd = 1e9f;
    for (int i = 0; i < g.numStations; i++)
        if (g.stations[i].type == type) {
            f32 d = distXZ(g.stations[i].pos, g.player.pos);
            if (d < bd) bd = d, best = i;
        }
    return best;
}

bool useStation(int type) {
    int s = stationIndex(type);
    if (s < 0) {
        say("!! no station of type %d", type);
        return false;
    }
    if (g.interact == Game::IK_STATION && g.interactIndex == s) {
        g.pad.sx = g.pad.sy = 0;
        press(BTN_A, 20);
        return true;
    }
    walkTo(g.stations[s].pos, 0.6f);
    return true;
}

// node that yields `item` at the player's current level, nearest first
int nodeFor(u16 item, bool *fishing) {
    int best = -1;
    f32 bd = 1e9f;
    for (int i = 0; i < g.numNodes; i++) {
        const Node &n = g.nodes[i];
        if (n.respawn > 0) continue;
        const NodeDef *d = nodeDef(n.type);
        if (!d) continue;
        if (d->nightOnly && !isNight()) continue;
        bool ok = false;
        for (const NodeLoot &l : d->loot)
            if (l.item == item && skillLevel(d->skill) >= l.level) ok = true;
        if (!ok) continue;
        f32 dd = distXZ(n.pos, g.player.pos);
        if (dd < bd) bd = dd, best = i;
    }
    if (best >= 0 && fishing) *fishing = g.nodes[best].fishing;
    return best;
}

int recipeFor(u16 item) {
    for (int i = 0; i < NUM_RECIPES; i++)
        if (RECIPES[i].output == item) return i;
    return -1;
}

bool fight(int enemyType);

// Make sure we hold `count` of `item`: craft it, gather it or fight for it.
bool obtain(u16 item, int count, int depth = 0) {
    if (g.pd.inv.count(item) >= count) return true;
    if (depth > 4) return false;
    int r = recipeFor(item);
    if (r >= 0) {
        const Recipe &rc = RECIPES[r];
        int batches = (count - g.pd.inv.count(item) + rc.outCount - 1) / rc.outCount;
        for (const Ingredient &in : rc.in)
            if (in.item && !obtain(in.item, in.count * batches, depth + 1)) return false;
        if (skillLevel(rc.skill) < rc.level) {
            say("!! %s needs %s level %d (have %d)", ITEMS[item].name, SKILLS[rc.skill].name, rc.level, skillLevel(rc.skill));
            return false;
        }
        say("craft %s x%d (%s)", ITEMS[item].name, count, SKILLS[rc.skill].name);
        if (g.mode == MODE_GATHER || g.mode == MODE_FISH) press(BTN_B, 20);
        else if (g.mode == MODE_PLAY) useStation(rc.station);
        else if (g.mode == MODE_CRAFT_SELECT && g.stationOpen != rc.station) {
            press(BTN_B, 15);   // wrong station
        } else if (g.mode == MODE_CRAFT_SELECT) {
            int sel = craft::selectedRecipe();
            if (sel != r) press(BTN_DOWN, 8);
            else press(BTN_A, 20);
        } else if (g.mode == MODE_CRAFT) {
            press(craft::synthesisDone() ? BTN_A : BTN_A, 30);
        }
        return false;
    }
    bool fishing = false;
    int n = nodeFor(item, &fishing);
    if (n >= 0) {
        say("gather %s x%d", ITEMS[item].name, count);
        if (g.mode == MODE_CRAFT_SELECT || g.mode == MODE_CRAFT) {
            press(BTN_B, 20);
        } else if (g.mode == MODE_GATHER) {
            press(BTN_A, 40);
        } else if (g.mode == MODE_FISH) {
            int fs = gather::fishState();
            if (fs == 0 || fs == 3 || fs == 5) press(BTN_A, 20);
        } else if (g.mode == MODE_PLAY) {
            if ((g.interact == Game::IK_NODE || g.interact == Game::IK_FISH) && g.interactIndex == n) {
                g.pad.sx = g.pad.sy = 0;
                press(BTN_A, 20);
            } else {
                walkTo(g.nodes[n].pos, fishing ? 3.0f : 1.6f);
            }
        }
        return false;
    }
    // enemy drops
    for (int e = 1; e < EN_COUNT; e++)
        for (const LootDrop &l : ENEMIES[e].loot)
            if (l.item == item) {
                say("hunt %s for %s", ENEMIES[e].name, ITEMS[item].name);
                fight(e);
                return false;
            }
    say("!! no way to obtain %s", ITEMS[item].name);
    return false;
}

bool fight(int enemyType) {
    if (g.mode == MODE_DEAD) {
        say("!! died fighting - respawning");
        press(BTN_A, 60);
        return false;
    }
    if (g.mode != MODE_PLAY) {
        press(BTN_B, 15);
        return false;
    }
    // nearest live enemy of this type
    int best = -1;
    f32 bd = 1e9f;
    for (int i = 0; i < MAX_ACTORS; i++) {
        const Actor &a = g.actors[i];
        if (!a.active || a.kind != AK_ENEMY || a.dead || a.enemyType != enemyType) continue;
        f32 d = distXZ(a.pos, g.player.pos);
        if (d < bd) bd = d, best = i;
    }
    if (best < 0) {
        // walk to the spawn area and wait for it
        for (int i = 0; i < g_world.numMarkers(); i++) {
            const Marker &m = g_world.marker(i);
            if (m.kind == MK_SPAWN && m.id == enemyType) {
                walkTo(m.pos, 4.0f);
                say("waiting for %s to spawn", ENEMIES[enemyType].name);
                return false;
            }
        }
        say("!! no spawn for enemy %d", enemyType);
        return false;
    }
    // heal when low
    if (g.player.hp < g.player.maxHp / 3 && s_wait <= 0) {
        g.pad.held |= BTN_L;
        press(BTN_A, 30);
        return false;
    }
    if (g.target != best) {
        if (bd < 24) press(BTN_Z, 10);
        else walkTo(g.actors[best].pos, 3.0f);
        return false;
    }
    f32 reach = g.pd.job == SK_MAGE ? 12.0f : 2.2f;
    if (!walkTo(g.actors[best].pos, reach)) return false;
    static int k = 0;
    if (s_wait <= 0) {
        static const u16 seq[3] = {BTN_A, BTN_X, BTN_Y};
        press(seq[k++ % 3], 24);
    }
    return false;
}

// equip the best weapon we own for the current job
void equipBest() {
    u16 best = 0;
    int tier = -1;
    for (const InvSlot &s : g.pd.inv.slots)
        if (s.item && ITEMS[s.item].cat == IC_WEAPON && ITEMS[s.item].skill == g.pd.job && ITEMS[s.item].tier > tier) best = s.item, tier = ITEMS[s.item].tier;
    if (best && best != g.pd.weapon) {
        g.pd.weapon = best;
        g.player.setHeld(ITEMS[best].model);
        playerRecalcStats();
        printf("[bot] equipped %s\n", ITEMS[best].name);
    }
}

}  // namespace

void storyBotTick() {
    g.pad.sx = g.pad.sy = 0;
    if (s_wait > 0) s_wait--;
    if (g.mode == MODE_CUTSCENE) {
        press(BTN_A, 30);
        return;
    }
    if (g.mode == MODE_MENU || g.mode == MODE_SHOP || g.mode == MODE_BOARD) {
        press(BTN_B, 15);
        return;
    }
    if (g.mode == MODE_CRAFT) {   // always finish a synthesis once started
        press(BTN_A, 30);
        return;
    }
    if (g.mode == MODE_DIALOGUE) {
        press(BTN_A, 10);   // read everything, accept offers
        return;
    }
    if (g.frame % 300 == 0) equipBest();
    if (g.frame % 1200 == 0)
        printf("[bot]   status mode %d sel %d interact %d/%d pos (%.0f,%.0f)\n", g.mode, craft::selectedRecipe(), (int)g.interact, g.interactIndex,
               g.player.pos.x, g.player.pos.z);
    // current objective: first active main quest, else accept the next available one
    int q = -1;
    for (int i = 0; i < NUM_QUESTS; i++)
        if (QUESTS[i].main && g.pd.quests[i].status == QST_ACTIVE) { q = i; break; }
    if (q < 0) {
        for (int i = 0; i < NUM_QUESTS; i++)
            if (QUESTS[i].main && g.pd.quests[i].status == QST_AVAILABLE) {
                say("accept '%s' from npc %d", QUESTS[i].title, QUESTS[i].giver);
                if (g.mode != MODE_PLAY) press(BTN_B, 15);
                else talkTo(QUESTS[i].giver);
                return;
            }
        bool allDone = true;
        for (int i = 0; i < NUM_QUESTS; i++)
            if (QUESTS[i].main && g.pd.quests[i].status != QST_DONE) allDone = false;
        if (allDone) {
            say("MAIN STORY COMPLETE (teleports used: %d)", s_teleports);
            static bool quitOnce = false;
            if (!quitOnce) {
                quitOnce = true;
                printf("[bot] skills:");
                for (int k = 0; k < SK_COUNT; k++) printf(" %s %d", SKILLS[k].name, skillLevel(k));
                printf("\n");
            }
        } else {
            say("!! no main quest available or active");
        }
        return;
    }
    QuestState &qs = g.pd.quests[q];
    const QuestStep &st = QUESTS[q].steps[qs.step];
    if (q != s_lastQuest || qs.step != s_lastStep) {
        printf("[bot] f%u === quest %d '%s' step %d: %s  (after %d frames)\n", g.frame, q, QUESTS[q].title, qs.step, st.objective, s_stepFrames);
        s_lastQuest = q;
        s_lastStep = qs.step;
        s_stepFrames = 0;
    }
    s_stepFrames++;
    if (s_stepFrames > 60 * 60 * 40) {
        say("!! step taking over 40 minutes");
    }
    // hand-in available?
    int waiting = -1;
    for (int n = 0; n < 64 && waiting < 0; n++)
        if (quests::stepNpcWaiting(n) == q) waiting = n;
    if (waiting >= 0) {
        say("report to npc %d", waiting);
        if (g.mode != MODE_PLAY) press(BTN_B, 15);
        else talkTo(waiting);
        return;
    }
    switch (st.type) {
        case QS_TALK:
            if (g.mode != MODE_PLAY) press(BTN_B, 15);
            else talkTo(st.target);
            break;
        case QS_GATHER:
            obtain(st.target, (int)g.pd.inv.count(st.target) + 1);
            break;
        case QS_CRAFT:
            obtain(st.target, (int)g.pd.inv.count(st.target) + 1);
            break;
        case QS_DELIVER:
            obtain(st.target, st.count);
            break;
        case QS_KILL:
            if (ENEMIES[st.target].nightOnly && !isNight()) {
                say("waiting for night to hunt %s", ENEMIES[st.target].name);
                if (g.mode != MODE_PLAY) press(BTN_B, 15);
                break;
            }
            fight(st.target);
            break;
        case QS_USE_STATION:
            if (g.mode != MODE_PLAY) press(BTN_B, 15);
            else {
                for (const Ingredient &in : RECIPES[0].in) (void)in;
                if (st.target == ST_HEARTH && (!g.pd.inv.count(IT_OAK_KINDLING) || !g.pd.inv.count(IT_BRONZE_GRATE))) {
                    if (!obtain(IT_OAK_KINDLING, 1)) break;
                    if (!obtain(IT_BRONZE_GRATE, 1)) break;
                }
                useStation(st.target);
            }
            break;
        default:
            say("!! unhandled step type %d", st.type);
    }
}
#endif

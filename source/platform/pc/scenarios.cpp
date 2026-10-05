// PC test harness scenarios: put the game into specific states for screenshots
// and logic checks. Selected with HV_SCENARIO=<name>.
#ifdef HV_PC
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/game.h"
#include "game/world.h"

namespace {
const char *s_name = nullptr;
int s_frame = 0;

int findNode(int type) {
    for (int i = 0; i < g.numNodes; i++)
        if (g.nodes[i].type == type) return i;
    return -1;
}
int findStation(int type) {
    for (int i = 0; i < g.numStations; i++)
        if (g.stations[i].type == type) return i;
    return -1;
}
void teleport(const Vec3 &p, f32 yaw) {
    g.player.pos = p;
    g.player.pos.y = g_world.groundHeight(p.x, p.z);
    g.player.yaw = yaw;
    g.camYaw = yaw;
}
void nearNode(int type) {
    int n = findNode(type);
    if (n < 0) return;
    Vec3 np = g.nodes[n].pos;
    Vec3 off(2.0f, 0, 2.0f);
    teleport(np + off, yawTo(np + off, np));
}
void nearStation(int type) {
    int s = findStation(type);
    if (s < 0) return;
    Vec3 sp = g.stations[s].pos;
    Vec3 off(0, 0, 2.2f);
    teleport(sp + off, yawTo(sp + off, sp));
}
bool is(const char *n) { return s_name && strcmp(s_name, n) == 0; }

void questLogicTest() {
    // drive the main story through its hooks and report the state machine
    auto st = [](int q) { return (int)g.pd.quests[q].status; };
    printf("[test] q0 status %d (expect 1 available)\n", st(0));
    quests::accept(0);
    printf("[test] q0 active step %d\n", g.pd.quests[0].step);
    int wren = npcs::actorForNpc(NPC_CARPENTER);
    npcs::talk(wren);
    while (g.mode == MODE_DIALOGUE) {
        g.pad.pressed = BTN_A;
        g.dialogueChars = 999;
        npcs::updateDialogue(0.016f);
    }
    printf("[test] after talking to Wren: step %d, hatchet %d\n", g.pd.quests[0].step, g.pd.inv.count(IT_WORN_HATCHET));
    giveItem(IT_OAK_LOG, 4);
    quests::onGather(IT_OAK_LOG, 4);
    printf("[test] after 4 logs: step %d\n", g.pd.quests[0].step);
    quests::onCraft(IT_OAK_KINDLING, 1);
    giveItem(IT_OAK_KINDLING, 1);
    printf("[test] crafted kindling: waiting npc -> %d\n", quests::stepNpcWaiting(NPC_CARPENTER));
    quests::advance(0);
    printf("[test] q0 status %d (expect 3 done), q1 %d (expect 1)\n", st(0), st(1));
    quests::accept(1);
    quests::onGather(IT_COPPER_ORE, 3);
    quests::onGather(IT_TIN_ORE, 3);
    quests::onCraft(IT_BRONZE_INGOT, 3);
    quests::onCraft(IT_BRONZE_GRATE, 1);
    giveItem(IT_BRONZE_GRATE, 1);
    printf("[test] q1 waiting at smith: %d\n", quests::stepNpcWaiting(NPC_SMITH));
    quests::advance(1);
    quests::accept(2);
    quests::onGather(IT_TROUT, 3);
    quests::onCraft(IT_GRILLED_TROUT, 3);
    quests::advance(2);
    quests::accept(3);
    quests::onGather(IT_MINT, 4);
    quests::onCraft(IT_POTION_MINOR, 2);
    quests::advance(3);
    quests::accept(4);
    printf("[test] weapon after q4 accept: %s\n", g.pd.weapon ? ITEMS[g.pd.weapon].name : "none");
    for (int i = 0; i < 4; i++) quests::onKill(EN_SLIME_GREEN);
    quests::advance(4);
    quests::accept(5);
    quests::onStation(ST_HEARTH);
    printf("[test] hearth cutscene mode %d (expect %d), lit %d\n", g.mode, MODE_CUTSCENE, g.pd.hearthLit);
    printf("[test] coins %u, smithing lvl %d, carpentry lvl %d\n", g.pd.coins, skillLevel(SK_SMITHING), skillLevel(SK_CARPENTRY));
    for (int q = 0; q < NUM_QUESTS; q++) printf("[test]   quest %d '%s' status %d step %d\n", q, QUESTS[q].title, st(q), g.pd.quests[q].step);
}
}  // namespace

void storyBotTick();

void scenarioTick() {
    if (!s_name) {
        s_name = getenv("HV_SCENARIO");
        if (!s_name) s_name = "";
    }
    if (!*s_name) return;
    s_frame++;
    int f = s_frame;
    if (f == 2) {
        if (is("dialogue")) {
            int e = npcs::actorForNpc(NPC_ELDER);
            teleport(g.actors[e].pos + Vec3(1.5f, 0, 2.5f), 3.6f);
            npcs::talk(e);
        } else if (is("gather")) {
            giveItem(IT_WORN_HATCHET, 1, false, false);
            nearNode(NT_OAK);
            gather::begin(nodes::nearest(g.player.pos, 4, false));
        } else if (is("mine")) {
            giveItem(IT_WORN_PICK, 1, false, false);
            nearNode(NT_COPPER);
            gather::begin(nodes::nearest(g.player.pos, 4, false));
        } else if (is("craft") || is("crafting")) {
            giveItem(IT_COPPER_ORE, 6, false, false);
            giveItem(IT_TIN_ORE, 6, false, false);
            giveItem(IT_SMITH_HAMMER, 1, false, false);
            nearStation(ST_FORGE);
            craft::openStation(ST_FORGE);
        } else if (is("menu") || is("skills") || is("map")) {
            giveItem(IT_OAK_LOG, 12, false, false);
            giveItem(IT_COPPER_ORE, 5, false, false);
            giveItem(IT_BRONZE_SWORD, 1, false, false);
            giveItem(IT_POTION_MINOR, 3, true, false);
            giveItem(IT_TROUT, 4, false, false);
            giveItem(IT_WORN_PICK, 1, false, false);
            g.pd.xp[SK_WOODCUTTING] = 900;
            g.pd.xp[SK_MINING] = 300;
            g.pd.xp[SK_SMITHING] = 2400;
            g.mode = MODE_MENU;
            g.menuTab = is("skills") ? 2 : (is("map") ? 4 : 0);
            g.menuSel = 1;
        } else if (is("shop")) {
            g.pd.coins = 500;
            giveItem(IT_OAK_LOG, 8, false, false);
            giveItem(IT_TROUT, 3, true, false);
            int m = npcs::actorForNpc(NPC_MERCHANT);
            teleport(g.actors[m].pos + Vec3(0, 0, -2.5f), 0);
            shop::open(ST_MARKET);
        } else if (is("board")) {
            giveItem(IT_OAK_LOG, 8, false, false);
            nearStation(ST_NOTICEBOARD);
            shop::open(ST_NOTICEBOARD);
        } else if (is("combat")) {
            giveItem(IT_BRONZE_SWORD, 1, false, false);
            g.pd.weapon = IT_BRONZE_SWORD;
            g.player.setHeld("itm/sword");
            teleport(Vec3(160, 0, 178), 3.14f);
        } else if (is("magic")) {
            giveItem(IT_OAK_STAFF, 1, false, false);
            g.pd.weapon = IT_OAK_STAFF;
            g.pd.job = SK_MAGE;
            g.player.setHeld("itm/staff");
            playerRecalcStats();
            teleport(Vec3(150, 0, 236), 0.5f);
        } else if (is("fish")) {
            giveItem(IT_WORN_ROD, 1, false, false);
            for (int i = 0; i < g.numNodes; i++)
                if (g.nodes[i].type == NT_FISH_LAKE) {
                    Vec3 np = g.nodes[i].pos;
                    Vec3 dir = normalize(Vec3(150 - np.x, 0, 322 - np.z)) * -1.0f;
                    Vec3 shore = np;
                    for (int k = 0; k < 40 && g_world.isWater(shore.x, shore.z); k++) shore += dir * 0.6f;
                    teleport(shore + dir * 0.8f, yawTo(shore, np));
                    gather::beginFishing(i);
                    break;
                }
        } else if (is("barrow")) {
            g.pd.hour = 22.5f;
            giveItem(IT_IRON_SWORD, 1, false, false);
            g.pd.weapon = IT_IRON_SWORD;
            g.player.setHeld("itm/sword");
            teleport(Vec3(296, 0, 104), 3.14f);
        } else if (is("cutscene")) {
            void startHearthCutscene();
            startHearthCutscene();
        } else if (is("title")) {
            g.mode = MODE_TITLE;
        } else if (is("probe")) {
            for (f32 z = 136; z <= 140; z += 4)
                for (f32 x = 226; x <= 260; x += 1) {
                    f32 gh = g_world.groundHeight(x, z);
                    Vec3 c(x, gh, z);
                    bool blocked = g_world.resolveCircle(c, 0.4f);
                    printf("[probe] x %.0f z %.0f h %.2f water %d walk %d obj %d\n", x, z, gh, g_world.isWater(x, z),
                           g_world.walkable(x + 1, z, gh), blocked);
                }
        } else if (is("bridgewalk")) {
            teleport(Vec3(226, 0, 136), 1.57f);
        } else if (is("probe2")) {
            for (int i = 0; i < g_world.numObjects(); i++) {
                const WorldObject &o = g_world.object(i);
                if (!o.colType || distXZ(o.pos, Vec3(232, 0, 196)) > 30) continue;
                printf("[probe] obj %d model %08x pos (%.1f,%.1f) col %d a %.1f b %.1f yaw %.2f\n", i, o.model ? o.model->hash : 0, o.pos.x, o.pos.z,
                       o.colType, o.ca, o.cb, o.yaw);
            }
        } else if (is("quests")) {
            questLogicTest();
        } else if (is("bridge")) {
            teleport(Vec3(236, 0, 183), 0.9f);
            g.camDist = 15;
            g.camPitch = 0.38f;
        } else if (is("bridge2")) {
            teleport(Vec3(222, 0, 136), 1.57f);
            g.camDist = 14;
        } else if (is("oaks")) {
            nearNode(NT_OAK);
            g.player.pos.x += 3;
            g.player.pos.z += 3;
            g.camYaw = g.player.yaw = yawTo(g.player.pos, g.nodes[findNode(NT_OAK)].pos);
        } else if (is("forest")) {
            teleport(Vec3(120, 0, 120), -2.2f);
        } else if (is("quarry")) {
            teleport(Vec3(296, 0, 196), 1.57f);
        } else if (is("lake")) {
            g.pd.hour = 18.6f;
            teleport(Vec3(158, 0, 286), 3.0f);
        } else if (is("town")) {
            teleport(Vec3(200, 0, 228), 3.14f);
            g.camPitch = 0.42f;
            g.camDist = 13;
        }
    }
    if (is("bridgewalk") && f > 3) {
        Vec3 fwd(sinf(g.camYaw), 0, cosf(g.camYaw)), right(-cosf(g.camYaw), 0, sinf(g.camYaw));
        Vec3 d(1, 0, 0);
        g.pad.sy = dot(d, fwd);
        g.pad.sx = dot(d, right);
        if (f % 20 == 0) printf("[walk] f %d pos %.2f %.2f %.2f ground %.2f\n", f, g.player.pos.x, g.player.pos.y, g.player.pos.z,
                                g_world.groundHeight(g.player.pos.x, g.player.pos.z, g.player.pos.y + 1.0f));
        return;
    }
    if (is("story")) {
        storyBotTick();
        return;
    }
    // drive actions on later frames
    if (is("gather") || is("mine")) {
        if (f == 20 || f == 90 || f == 160) g.pad.pressed |= BTN_A, g.pad.held |= BTN_A;
    }
    if (is("crafting")) {
        if (f == 10) g.pad.pressed |= BTN_A;        // start synthesis
        if (f == 40 || f == 100) g.pad.pressed |= BTN_X;   // touch
        if (f == 160 || f == 220) g.pad.pressed |= BTN_A;  // synthesis
    }
    if (is("combat") || is("magic") || is("barrow")) {
        if (f == 100) g.pad.pressed |= BTN_Z;
        if (f > 40 && f % 70 == 0) g.pad.pressed |= (f / 70) % 2 ? BTN_A : BTN_X;
        if (f == 200) g.pad.held |= BTN_R;
    }
    if (is("fish")) {
        if (f == 20) g.pad.pressed |= BTN_A;
    }
    if (f % 60 == 0) {
        int e = npcs::actorForNpc(NPC_ELDER);
        printf("[scen] f %d mode %d region %d target %d combat %.1f hp %d interact %d elderDist %.2f\n", f, g.mode, g.currentRegion, g.target,
               g.combatTimer, g.player.hp, (int)g.interact, e >= 0 ? distXZ(g.actors[e].pos, g.player.pos) : -1.0f);
    }
}
#endif

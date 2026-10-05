// HUD (bars, hotbar, minimap, tracker, prompts, bubbles) and the main menu.
#include <stdio.h>
#include "game/game.h"
#include "game/world.h"
#include "gfx/sky.h"
#include "ui/ui.h"

void gameReturnToTitle();

namespace combat {
const char *abilityName(int i);
int abilityLevel(int i);
f32 abilityCooldown(int i);
}

namespace hud {

namespace {
f32 W() { return (f32)plat::screenW(); }
f32 H() { return (f32)plat::screenH(); }

void playerFrame() {
    Actor &p = g.player;
    f32 x = 18, y = 16;
    ui::panel(x, y, 236, g.pd.job == SK_MAGE ? 86 : 72, ui::PANEL);
    char b[64];
    snprintf(b, sizeof(b), "%s", g.pd.name);
    ui::text(FONT_UI, x + 14, y + 6, b, ui::WHITE);
    snprintf(b, sizeof(b), "%s %d", SKILLS[g.pd.job].name, skillLevel(g.pd.job));
    ui::text(FONT_SMALL, x + 222, y + 9, b, ui::rgba(SKILLS[g.pd.job].r, SKILLS[g.pd.job].g, SKILLS[g.pd.job].b), AL_RIGHT);
    f32 hpF = (f32)p.hp / hvMax(1, p.maxHp);
    GXColor hc = hpF > 0.5f ? ui::rgba(110, 210, 110) : (hpF > 0.25f ? ui::rgba(230, 190, 70) : ui::rgba(230, 70, 60));
    ui::text(FONT_SMALL, x + 14, y + 32, "HP", ui::TEXT_DIM);
    ui::bar(x + 42, y + 35, 140, 9, hpF, hc);
    snprintf(b, sizeof(b), "%d", p.hp);
    ui::text(FONT_SMALL, x + 222, y + 31, b, ui::WHITE, AL_RIGHT);
    f32 ry = y + 50;
    if (g.pd.job == SK_MAGE) {
        ui::text(FONT_SMALL, x + 14, ry, "MP", ui::TEXT_DIM);
        ui::bar(x + 42, ry + 3, 140, 9, (f32)p.mp / hvMax(1, p.maxMp), ui::rgba(110, 160, 255));
        snprintf(b, sizeof(b), "%d", p.mp);
        ui::text(FONT_SMALL, x + 222, ry - 1, b, ui::WHITE, AL_RIGHT);
        ry += 16;
    }
    ui::text(FONT_SMALL, x + 14, ry, "GP", ui::TEXT_DIM);
    ui::bar(x + 42, ry + 3, 140, 7, (f32)g.gp / hvMax(1, g.maxGp), ui::rgba(240, 190, 80));
    if (g.wardShield > 0) ui::text(FONT_SMALL, x + 186, ry, "Ward", ui::BLUE);
    if (g.buffRampart > 0) ui::text(FONT_SMALL, x + 186, ry, "Rampart", ui::GOLD);
}

void clock() {
    f32 sx = W() - 78, sy = 82, r = 58;
    ui::minimap(g.player.pos.x, g.player.pos.z, g.camYaw, 60, sx, sy, r);
    // markers: quest NPCs, stations, nodes
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor &a = g.actors[i];
        if (!a.active) continue;
        GXColor c;
        f32 sz = 6;
        if (a.kind == AK_NPC) {
            bool quest = quests::availableFrom(a.npcId) >= 0 || quests::stepNpcWaiting(a.npcId) >= 0;
            if (!quest) continue;
            c = ui::GOLD;
            sz = 9;
        } else if (a.kind == AK_ENEMY && !a.dead) {
            if (distXZ(a.pos, g.player.pos) > 50) continue;
            c = ui::rgba(240, 80, 70);
            sz = 5;
        } else if (a.kind == AK_ADVENTURER) {
            c = ui::rgba(110, 180, 255);
            sz = 5;
        } else continue;
        ui::minimapDot(a.pos.x, a.pos.z, g.player.pos.x, g.player.pos.z, g.camYaw, 60, sx, sy, r, c, sz);
    }
    // player arrow (always up = camera forward, rotated by facing)
    ui::sprite(hvHash("tx/ui_circle"), sx - 5, sy - 5, 10, 10, ui::WHITE);
    f32 rel = g.player.yaw - g.camYaw;
    f32 ax = sx - sinf(rel) * 9, ay = sy - cosf(rel) * 9;
    ui::sprite(hvHash("tx/ui_circle"), ax - 3, ay - 3, 6, 6, ui::GOLD);
    // N marker
    f32 nx = sx + cosf(g.camYaw) * 0 - sinf(-g.camYaw + HV_PI) * (r - 8);
    f32 ny = sy - cosf(-g.camYaw + HV_PI) * (r - 8);
    (void)nx; (void)ny;
    // time + coins
    char b[48];
    int hh = (int)g.pd.hour, mm = (int)((g.pd.hour - hh) * 60);
    snprintf(b, sizeof(b), "Day %u  %02d:%02d", g.pd.day + 1, hh, mm);
    ui::text(FONT_SMALL, sx, sy + r + 10, b, isNight() ? ui::BLUE : ui::WHITE, AL_CENTER);
    snprintf(b, sizeof(b), "%u coins", g.pd.coins);
    ui::text(FONT_SMALL, sx, sy + r + 26, b, ui::GOLD, AL_CENTER);
}

void tracker() {
    int q = quests::trackedQuest();
    if (q < 0) return;
    char b[96];
    f32 x = W() - 250, y = 200, w = 232;
    ui::panel(x, y, w, 58, ui::rgba(20, 18, 28, 170), 10);
    ui::text(FONT_SMALL, x + 12, y + 7, QUESTS[q].title, QUESTS[q].main ? ui::GOLD : ui::rgba(170, 220, 255));
    quests::objectiveText(q, b, sizeof(b));
    ui::textWrap(FONT_SMALL, x + 12, y + 25, w - 24, b, ui::WHITE);
}

void hotbar() {
    bool L = (g.pad.held & BTN_L) != 0, R = (g.pad.held & BTN_R) != 0;
    bool fighting = g.target >= 0 || combat::inCombat();
    if (!g.pd.weapon) return;
    if (!fighting && !L && !R) return;
    f32 cx = W() * 0.5f, cy = H() - 62;
    ui::panel(cx - 170, cy - 30, 340, 64, ui::rgba(20, 18, 28, 190), 14);
    Glyph gl[4] = {GL_A, GL_X, GL_Y, GL_B};
    static const char *const ITEMS_L[4] = {"Heal", "Tonic", "Draught", ""};
    for (int k = 0; k < 4; k++) {
        f32 x = cx - 160 + k * 82;
        const char *name;
        bool ok = true;
        f32 cd = 0;
        if (L) {
            name = ITEMS_L[k];
            ok = k < 3;
        } else {
            int i = R ? k + 4 : k;
            if (!R && k == 3) {
                name = "Dodge";
            } else {
                name = combat::abilityName(i);
                ok = skillLevel(g.pd.job) >= combat::abilityLevel(i);
                cd = combat::abilityCooldown(i);
            }
            if (!R && g.pd.job == SK_WARRIOR && ((k == 1 && g.combo == 1) || (k == 2 && g.combo == 2))) {
                ui::panel(x - 2, cy - 26, 80, 54, ui::rgba(255, 210, 120, 90), 10);
            }
        }
        ui::glyph(gl[k], x + 2, cy - 22, 26, ok ? 255 : 90);
        if (cd > 0) ui::rect(x + 2, cy - 22 + 26 * (1 - cd), 26, 26 * cd, ui::rgba(0, 0, 0, 150));
        ui::textWrap(FONT_SMALL, x, cy + 6, 80, name, ok ? ui::WHITE : ui::TEXT_DIM, 0.85f);
    }
    ui::text(FONT_SMALL, cx, cy - 46, R ? "R  Job skills" : (L ? "L  Items" : "Hold L / R for more"), ui::TEXT_DIM, AL_CENTER);
}

void interactPrompt() {
    if (g.mode != MODE_PLAY || g.interact == Game::IK_NONE) return;
    bool hasTarget = g.target >= 0;
    if (hasTarget) return;
    char b[64];
    switch (g.interact) {
        case Game::IK_NPC: {
            Actor &a = g.actors[g.interactIndex];
            snprintf(b, sizeof(b), "Talk to %s", a.name);
            break;
        }
        case Game::IK_NODE: {
            const NodeDef *d = nodeDef(g.nodes[g.interactIndex].type);
            snprintf(b, sizeof(b), "%s %s", SKILLS[d->skill].verb, d->name);
            break;
        }
        case Game::IK_FISH: snprintf(b, sizeof(b), "Fish"); break;
        case Game::IK_STATION: {
            static const char *const N[] = {"", "Use Forge", "Use Anvil", "Cook", "Use Workbench", "Brew", "Great Hearth", "Notice Board", "Cook at Campfire", "Market", "Rest"};
            int t = g.stations[g.interactIndex].type;
            snprintf(b, sizeof(b), "%s", t < HV_ARRAY_COUNT(N) ? N[t] : "Use");
            break;
        }
        default: return;
    }
    f32 w = ui::textWidth(FONT_UI, b) + 50;
    f32 x = W() * 0.5f - w * 0.5f, y = H() * 0.68f;
    ui::panel(x - 8, y - 8, w + 16, 40, ui::rgba(20, 18, 28, 190), 12);
    ui::prompt(GL_A, x, y, b);
}

void bubbles() {
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor &a = g.actors[i];
        if (!a.active) continue;
        if (a.kind == AK_ENEMY) continue;
        f32 d = distXZ(a.pos, g.cam.eye);
        if (d > 40) continue;
        bool on;
        Vec3 s = screenProject(a.headPos() + Vec3(0, 0.55f, 0), &on);
        if (!on) continue;
        // quest markers
        if (a.kind == AK_NPC) {
            bool offer = quests::availableFrom(a.npcId) >= 0;
            bool turnin = quests::stepNpcWaiting(a.npcId) >= 0;
            if (offer || turnin) {
                f32 bob = sinf(g.time * 3 + i) * 3;
                ui::text(FONT_BIG, s.x, s.y - 40 + bob, turnin ? "?" : "!", ui::GOLD, AL_CENTER, hvClamp(22.0f / d, 0.5f, 1.0f));
            }
        }
        // name plates
        if (d < 18) {
            char b[48];
            if (a.kind == AK_ADVENTURER) snprintf(b, sizeof(b), "%s  Lv%d", a.name, a.level);
            else snprintf(b, sizeof(b), "%s", a.name);
            GXColor c = a.kind == AK_ADVENTURER ? ui::rgba(140, 200, 255) : ui::rgba(255, 240, 200);
            ui::text(FONT_SMALL, s.x, s.y - 4, b, c, AL_CENTER);
        }
        if (a.chatTimer > 0 && d < 28) {
            f32 tw = hvMin(ui::textWidth(FONT_SMALL, a.chat) + 20, 220.0f);
            f32 bx = s.x - tw * 0.5f, by = s.y - 52;
            u8 al = (u8)(hvSaturate(a.chatTimer) * 230);
            ui::panel(bx, by, tw, 26, ui::rgba(250, 246, 236, al), 10);
            ui::text(FONT_SMALL, s.x, by + 5, a.chat, ui::rgba(40, 34, 30, al), AL_CENTER);
        }
    }
}

void toasts() {
    f32 x = 20, y = H() * 0.5f - 20;
    for (int i = 0; i < HV_ARRAY_COUNT(g.toasts); i++) {
        auto &t = g.toasts[i];
        if (t.timer <= 0) continue;
        u8 a = (u8)(hvSaturate(t.timer) * 255);
        GXColor c = t.color;
        c.a = a;
        f32 w = ui::textWidth(FONT_SMALL, t.text) + (t.icon ? 44 : 20);
        ui::panel(x, y - i * 30, w, 26, ui::rgba(20, 18, 28, (u8)(a * 0.7f)), 9);
        if (t.icon && a > 60) ui::icon(ITEMS[t.icon].model, x + 16, y - i * 30 + 13, 22, 1.5f);
        ui::text(FONT_SMALL, x + (t.icon ? 32 : 10), y - i * 30 + 5, t.text, c);
    }
}

void banner() {
    if (g.bannerTimer > 0) {
        f32 a = hvSaturate(g.bannerTimer * 1.5f) * hvSaturate((g.bannerTimer > 100 ? 1.0f : 1.0f));
        f32 cy = H() * 0.26f;
        ui::rectGradient(0, cy - 10, W(), 30, ui::rgba(0, 0, 0, 0), ui::rgba(0, 0, 0, (u8)(120 * a)));
        ui::rectGradient(0, cy + 20, W(), 50, ui::rgba(0, 0, 0, (u8)(120 * a)), ui::rgba(0, 0, 0, 0));
        ui::text(FONT_BIG, W() * 0.5f, cy - 6, g.banner, ui::rgba(255, 220, 130, (u8)(255 * a)), AL_CENTER);
        if (g.bannerSub[0]) ui::text(FONT_UI, W() * 0.5f, cy + 34, g.bannerSub, ui::rgba(255, 255, 255, (u8)(255 * a)), AL_CENTER);
    }
    if (g.regionTimer > 0 && g.bannerTimer <= 0) {
        f32 a = hvSaturate(g.regionTimer) * hvSaturate((3.5f - g.regionTimer) * 2);
        ui::text(FONT_BIG, W() * 0.5f, H() * 0.18f, g.regionName, ui::rgba(255, 250, 235, (u8)(240 * a)), AL_CENTER, 0.85f);
    }
    if (g.saveMsgTimer > 0) ui::text(FONT_SMALL, W() - 20, H() - 30, g.saveMsg, ui::rgba(255, 255, 255, (u8)(hvSaturate(g.saveMsgTimer) * 255)), AL_RIGHT);
}
}  // namespace

void draw() {
    if (g.mode == MODE_TITLE || g.mode == MODE_CREATE) return;
    bubbles();
    bool full = g.mode == MODE_PLAY || g.mode == MODE_DEAD;
    if (g.mode != MODE_MENU) {
        playerFrame();
        clock();
        if (full) {
            tracker();
            hotbar();
            interactPrompt();
        }
        combat::drawUi();
        sim::drawUi();
    }
    toasts();
    banner();
    if (g.levelUpFlash > 0) ui::rect(0, 0, W(), H(), ui::rgba(255, 230, 160, (u8)(g.levelUpFlash * 70)));
}

// ================================================================ menu
namespace {
const char *const TABS[] = {"Bag", "Gear", "Skills", "Quests", "Map", "System"};
const int NTABS = 6;
int s_sysSel = 0;

void drawBag() {
    f32 x0 = 40, y0 = 90;
    const int COLS = 8;
    f32 cell = 52;
    char b[96];
    for (int i = 0; i < Inventory::SIZE; i++) {
        f32 x = x0 + (i % COLS) * cell, y = y0 + (i / COLS) * cell;
        bool sel = i == g.menuSel;
        ui::panel(x, y, cell - 6, cell - 6, sel ? ui::rgba(255, 210, 130, 200) : ui::rgba(60, 54, 74, 200), 8);
        const InvSlot &s = g.pd.inv.slots[i];
        if (!s.item) continue;
        ui::icon(ITEMS[s.item].model, x + (cell - 6) * 0.5f, y + (cell - 6) * 0.5f, 34, sel ? 1.5f : 0.0f);
        if (s.count > 1) {
            snprintf(b, sizeof(b), "%d", s.count);
            ui::text(FONT_SMALL, x + cell - 10, y + cell - 24, b, ui::WHITE, AL_RIGHT);
        }
        if (s.hq) ui::text(FONT_SMALL, x + 4, y + 2, "HQ", ui::GOLD);
        bool equipped = s.item == g.pd.weapon || s.item == g.pd.armor || s.item == g.pd.accessory;
        if (equipped) ui::text(FONT_SMALL, x + 4, y + cell - 24, "E", ui::GREEN);
    }
    // detail
    f32 dx = x0 + COLS * cell + 12, dw = W() - dx - 30;
    ui::panel(dx, y0, dw, 5 * cell - 6, ui::PANEL_DARK);
    const InvSlot &s = g.pd.inv.slots[g.menuSel];
    if (s.item) {
        const ItemDef &d = ITEMS[s.item];
        ui::icon(d.model, dx + dw * 0.5f, y0 + 54, 72, 1.0f);
        ui::text(FONT_UI, dx + dw * 0.5f, y0 + 98, d.name, s.hq ? ui::GOLD : ui::WHITE, AL_CENTER);
        ui::textWrap(FONT_SMALL, dx + 12, y0 + 124, dw - 24, d.desc, ui::TEXT_DIM);
        snprintf(b, sizeof(b), "Value %u coins", (unsigned)(d.value * s.count));
        ui::text(FONT_SMALL, dx + 12, y0 + 5 * cell - 52, b, ui::GOLD);
        const char *act = nullptr;
        if (d.cat == IC_WEAPON || d.cat == IC_ARMOR) act = "Equip";
        else if (d.cat == IC_FOOD || d.cat == IC_POTION) act = "Use";
        if (act) ui::prompt(GL_A, dx + 10, y0 + 5 * cell - 34, act);
    } else {
        ui::text(FONT_SMALL, dx + dw * 0.5f, y0 + 110, "Empty", ui::TEXT_DIM, AL_CENTER);
    }
    snprintf(b, sizeof(b), "%d / %d slots", Inventory::SIZE - g.pd.inv.freeSlots(), Inventory::SIZE);
    ui::text(FONT_SMALL, x0, y0 + 5 * cell + 4, b, ui::TEXT_DIM);
    ui::prompt(GL_X, x0 + 140, y0 + 5 * cell + 2, "Sort");
}

void drawGear() {
    f32 x = 60, y = 96;
    char b[96];
    const char *slots[3] = {"Weapon", "Body", "Accessory"};
    u16 items[3] = {g.pd.weapon, g.pd.armor, g.pd.accessory};
    for (int k = 0; k < 3; k++) {
        f32 ry = y + k * 70;
        ui::panel(x, ry, 300, 60, ui::PANEL_DARK, 10);
        ui::text(FONT_SMALL, x + 70, ry + 8, slots[k], ui::TEXT_DIM);
        if (items[k]) {
            ui::icon(ITEMS[items[k]].model, x + 32, ry + 30, 44, 0.8f);
            ui::text(FONT_UI, x + 70, ry + 26, ITEMS[items[k]].name, ui::WHITE);
        } else {
            ui::text(FONT_UI, x + 70, ry + 26, "-", ui::TEXT_DIM);
        }
    }
    int def = (g.pd.armor ? ITEMS[g.pd.armor].power : 0) + (g.pd.accessory ? ITEMS[g.pd.accessory].power : 0);
    snprintf(b, sizeof(b), "Job: %s Lv %d     Damage %d     Defence %d", SKILLS[g.pd.job].name, skillLevel(g.pd.job),
             g.pd.weapon ? ITEMS[g.pd.weapon].power : 8, def);
    ui::text(FONT_UI, x, y + 222, b, ui::GOLD);
    ui::text(FONT_SMALL, x, y + 248, "Tools are used automatically - the best one in your bag.", ui::TEXT_DIM);
    // tools list
    f32 tx = x + 330;
    ui::text(FONT_UI, tx, y, "Tools", ui::GOLD);
    static const int KINDS[8] = {TOOL_AXE, TOOL_PICK, TOOL_SICKLE, TOOL_ROD, TOOL_HAMMER, TOOL_SAW, TOOL_PAN, TOOL_FLASK};
    static const char *const KN[8] = {"Hatchet", "Pickaxe", "Sickle", "Rod", "Hammer", "Saw", "Skillet", "Alembic"};
    for (int k = 0; k < 8; k++) {
        int t = bestTool(KINDS[k]);
        snprintf(b, sizeof(b), "%s: %s", KN[k], t ? ITEMS[t].name : "-");
        ui::text(FONT_SMALL, tx, y + 30 + k * 22, b, t ? ui::WHITE : ui::TEXT_DIM);
    }
}

void drawSkills() {
    f32 x = 50, y = 92;
    char b[64];
    for (int k = 0; k < SK_COUNT; k++) {
        f32 cx = x + (k % 2) * 280, cy = y + (k / 2) * 62;
        bool sel = k == g.menuSel;
        ui::panel(cx, cy, 266, 54, sel ? ui::rgba(80, 70, 50, 220) : ui::PANEL_DARK, 10);
        ui::icon(SKILLS[k].icon, cx + 28, cy + 27, 36, sel ? 1.2f : 0.4f);
        int lvl = skillLevel(k);
        ui::text(FONT_UI, cx + 54, cy + 5, SKILLS[k].name, ui::rgba(SKILLS[k].r, SKILLS[k].g, SKILLS[k].b));
        snprintf(b, sizeof(b), "Lv %d", lvl);
        ui::text(FONT_UI, cx + 254, cy + 5, b, ui::WHITE, AL_RIGHT);
        u32 a = xpForLevel(lvl), bnext = xpForLevel(lvl + 1);
        f32 f = lvl >= MAX_LEVEL ? 1.0f : (f32)(g.pd.xp[k] - a) / hvMax<u32>(1, bnext - a);
        ui::bar(cx + 54, cy + 34, 150, 7, f, ui::rgba(SKILLS[k].r, SKILLS[k].g, SKILLS[k].b));
        if (lvl < MAX_LEVEL) snprintf(b, sizeof(b), "%u/%u", g.pd.xp[k] - a, bnext - a);
        else snprintf(b, sizeof(b), "MAX");
        ui::text(FONT_SMALL, cx + 254, cy + 28, b, ui::TEXT_DIM, AL_RIGHT);
    }
}

void drawQuests() {
    f32 x = 50, y = 92;
    char b[128];
    int row = 0;
    for (int q = 0; q < NUM_QUESTS; q++) {
        u8 st = g.pd.quests[q].status;
        if (st == QST_LOCKED) continue;
        f32 ry = y + row * 34;
        if (ry > H() - 120) break;
        GXColor c = st == QST_DONE ? ui::TEXT_DIM : (st == QST_ACTIVE ? ui::WHITE : ui::rgba(255, 230, 150));
        const char *tag = st == QST_DONE ? "Done" : (st == QST_ACTIVE ? (QUESTS[q].main ? "Story" : "Side") : "New");
        ui::panel(x, ry, 540, 30, ui::PANEL_DARK, 8);
        ui::text(FONT_SMALL, x + 10, ry + 6, tag, QUESTS[q].main ? ui::GOLD : ui::BLUE);
        ui::text(FONT_SMALL, x + 60, ry + 6, QUESTS[q].title, c);
        if (st == QST_ACTIVE) {
            quests::objectiveText(q, b, sizeof(b));
            ui::text(FONT_SMALL, x + 530, ry + 6, b, ui::TEXT_DIM, AL_RIGHT);
        } else if (st == QST_AVAILABLE) {
            const NpcDef *d = npcDef(QUESTS[q].giver);
            snprintf(b, sizeof(b), "Speak to %s", d ? d->name : "?");
            ui::text(FONT_SMALL, x + 530, ry + 6, b, ui::TEXT_DIM, AL_RIGHT);
        }
        row++;
    }
    if (!row) ui::text(FONT_UI, x, y, "No quests yet.", ui::TEXT_DIM);
}

void drawMap() {
    f32 size = hvMin(W() - 80, H() - 150);
    f32 x = (W() - size) * 0.5f, y = 86;
    ui::panel(x - 8, y - 8, size + 16, size + 16, ui::PANEL_DARK, 12);
    ui::sprite(hvHash("tx/minimap"), x, y, size, size, ui::WHITE);
    f32 k = size / 384.0f;
    // labels
    static const struct { f32 x, z; const char *n; } L[] = {{200, 196, "Emberwick"}, {92, 112, "Whisperwood"}, {318, 196, "Copperhill"},
                                                           {300, 72, "Old Barrow"}, {88, 258, "Hollis Farm"}, {150, 322, "Mirror Lake"}};
    for (auto &l : L) ui::text(FONT_SMALL, x + l.x * k, y + l.z * k - 22, l.n, ui::WHITE, AL_CENTER);
    for (int i = 0; i < MAX_ACTORS; i++) {
        Actor &a = g.actors[i];
        if (!a.active || a.kind != AK_NPC) continue;
        if (quests::availableFrom(a.npcId) < 0 && quests::stepNpcWaiting(a.npcId) < 0) continue;
        ui::sprite(hvHash("tx/ui_circle"), x + a.pos.x * k - 5, y + a.pos.z * k - 5, 10, 10, ui::GOLD);
    }
    f32 px = x + g.player.pos.x * k, pz = y + g.player.pos.z * k;
    f32 pulse = 10 + 3 * sinf(g.time * 5);
    ui::sprite(hvHash("tx/ui_circle"), px - pulse * 0.5f, pz - pulse * 0.5f, pulse, pulse, ui::rgba(255, 90, 80));
}

void drawSystem() {
    f32 x = W() * 0.5f - 140, y = 120;
    const char *opts[4] = {"Save Game", "Rest until morning", "Toggle Job (Warrior / Mage)", "Return to Title"};
    for (int k = 0; k < 4; k++) {
        bool sel = k == s_sysSel;
        ui::panel(x, y + k * 50, 280, 40, sel ? ui::rgba(255, 210, 130, 200) : ui::PANEL_DARK, 12);
        ui::text(FONT_UI, x + 140, y + k * 50 + 9, opts[k], sel ? ui::rgba(40, 30, 20) : ui::WHITE, AL_CENTER);
    }
    char b[96];
    u32 s = g.pd.playSeconds;
    snprintf(b, sizeof(b), "Play time %u:%02u   Kills %u   Crafted %u (%u HQ)   Gathered %u", s / 3600, (s / 60) % 60, g.pd.kills,
             g.pd.crafted, g.pd.hqCrafted, g.pd.gathered);
    ui::text(FONT_SMALL, W() * 0.5f, y + 220, b, ui::TEXT_DIM, AL_CENTER);
}
}  // namespace

void drawMenu() {
    ui::rect(0, 0, W(), H(), ui::rgba(10, 8, 18, 150));
    ui::panel(20, 20, W() - 40, H() - 40, ui::rgba(30, 26, 40, 235), 16);
    // tabs
    f32 tx = 40;
    for (int i = 0; i < NTABS; i++) {
        bool sel = i == g.menuTab;
        f32 w = ui::textWidth(FONT_UI, TABS[i]) + 26;
        ui::panel(tx, 32, w, 34, sel ? ui::rgba(255, 200, 110, 230) : ui::rgba(60, 54, 74, 220), 10);
        ui::text(FONT_UI, tx + 13, 37, TABS[i], sel ? ui::rgba(40, 30, 20) : ui::WHITE);
        tx += w + 8;
    }
    ui::glyph(GL_L, W() - 120, 34, 26);
    ui::glyph(GL_R, W() - 90, 34, 26);
    switch (g.menuTab) {
        case 0: drawBag(); break;
        case 1: drawGear(); break;
        case 2: drawSkills(); break;
        case 3: drawQuests(); break;
        case 4: drawMap(); break;
        case 5: drawSystem(); break;
    }
    ui::prompt(GL_B, 40, H() - 56, "Close");
}

void drawPause() {}

static void equip(u16 item) {
    const ItemDef &d = ITEMS[item];
    if (skillLevel(d.cat == IC_WEAPON ? d.skill : g.pd.job) < d.level && d.cat == IC_WEAPON) {
        toast("Level too low to equip.", ui::RED);
        audio::sfx(SFX_FAIL);
        return;
    }
    if (d.cat == IC_WEAPON) {
        g.pd.weapon = item;
        if (d.skill == SK_WARRIOR || d.skill == SK_MAGE) g.pd.job = d.skill;
        g.player.setHeld(d.model);
    } else if (d.cat == IC_ARMOR) {
        if (d.model && (strstr(d.name, "Ring") || strstr(d.name, "Amulet"))) g.pd.accessory = item;
        else g.pd.armor = item;
    }
    playerRecalcStats();
    char b[64];
    snprintf(b, sizeof(b), "Equipped %s", d.name);
    toast(b, ui::GREEN, item);
    audio::sfx(SFX_UI_OK);
}

void updateMenu(f32 dt) {
    (void)dt;
    PadState &pad = g.pad;
    if (pad.pressed & (BTN_B | BTN_START)) {
        g.mode = MODE_PLAY;
        audio::sfx(SFX_UI_BACK);
        return;
    }
    if (pad.pressed & BTN_R) { g.menuTab = (g.menuTab + 1) % NTABS; g.menuSel = 0; audio::sfx(SFX_UI_MOVE); }
    if (pad.pressed & BTN_L) { g.menuTab = (g.menuTab + NTABS - 1) % NTABS; g.menuSel = 0; audio::sfx(SFX_UI_MOVE); }
    switch (g.menuTab) {
        case 0: {
            int s = g.menuSel;
            if (pad.pressed & BTN_RIGHT) s++;
            if (pad.pressed & BTN_LEFT) s--;
            if (pad.pressed & BTN_DOWN) s += 8;
            if (pad.pressed & BTN_UP) s -= 8;
            s = (s + Inventory::SIZE) % Inventory::SIZE;
            if (s != g.menuSel) audio::sfx(SFX_UI_MOVE);
            g.menuSel = s;
            if (pad.pressed & BTN_X) { g.pd.inv.sort(); audio::sfx(SFX_UI_OK); }
            if (pad.pressed & BTN_A) {
                InvSlot &sl = g.pd.inv.slots[g.menuSel];
                if (!sl.item) break;
                const ItemDef &d = ITEMS[sl.item];
                if (d.cat == IC_WEAPON || d.cat == IC_ARMOR) equip(sl.item);
                else if (d.cat == IC_FOOD || d.cat == IC_POTION) {
                    if (sl.item == IT_SUNPETAL_DRAUGHT) g.buffDamage = 60;
                    else if (sl.item == IT_SAGE_TONIC) { g.player.mp = hvMin(g.player.maxMp, g.player.mp + 150); g.gp = hvMin(g.maxGp, g.gp + 150); }
                    else g.player.hp = hvMin(g.player.maxHp, g.player.hp + d.power * (sl.hq ? 11 : 10) / 10);
                    char b[64];
                    snprintf(b, sizeof(b), "Used %s", d.name);
                    toast(b, ui::GREEN);
                    g.pd.inv.remove(sl.item, 1);
                    audio::sfx(SFX_HEAL);
                }
            }
            break;
        }
        case 2:
            if (pad.pressed & BTN_RIGHT) g.menuSel = (g.menuSel + 1) % SK_COUNT;
            if (pad.pressed & BTN_LEFT) g.menuSel = (g.menuSel + SK_COUNT - 1) % SK_COUNT;
            if (pad.pressed & BTN_DOWN) g.menuSel = (g.menuSel + 2) % SK_COUNT;
            if (pad.pressed & BTN_UP) g.menuSel = (g.menuSel + SK_COUNT - 2) % SK_COUNT;
            break;
        case 5:
            if (pad.pressed & BTN_DOWN) { s_sysSel = (s_sysSel + 1) % 4; audio::sfx(SFX_UI_MOVE); }
            if (pad.pressed & BTN_UP) { s_sysSel = (s_sysSel + 3) % 4; audio::sfx(SFX_UI_MOVE); }
            if (pad.pressed & BTN_A) {
                if (s_sysSel == 0) save::write();
                else if (s_sysSel == 1) {
                    if (g.pd.hour > 6.0f) g.pd.day++;
                    g.pd.hour = 6.5f;
                    shop::newDay();
                    g.player.hp = g.player.maxHp;
                    g.mode = MODE_PLAY;
                    g.fadeIn = 1.5f;
                    toast("You rest until morning.", ui::GOLD);
                } else if (s_sysSel == 2) {
                    g.pd.job = g.pd.job == SK_MAGE ? SK_WARRIOR : SK_MAGE;
                    u16 want = g.pd.job == SK_MAGE ? IT_OAK_STAFF : IT_TRAINING_SWORD;
                    // equip the best owned weapon for the job
                    u16 best = 0;
                    int bestTier = -1;
                    for (const InvSlot &s : g.pd.inv.slots)
                        if (s.item && ITEMS[s.item].cat == IC_WEAPON && ITEMS[s.item].skill == g.pd.job && ITEMS[s.item].tier > bestTier) {
                            best = s.item;
                            bestTier = ITEMS[s.item].tier;
                        }
                    if (!best && g.pd.inv.count(want)) best = want;
                    g.pd.weapon = best;
                    g.player.setHeld(best ? ITEMS[best].model : nullptr);
                    playerRecalcStats();
                    char b[48];
                    snprintf(b, sizeof(b), "Job changed to %s", SKILLS[g.pd.job].name);
                    toast(b, ui::GOLD);
                } else {
                    gameReturnToTitle();
                }
            }
            break;
    }
}

}  // namespace hud

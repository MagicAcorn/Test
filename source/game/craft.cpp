// Crafting: recipe selection and the FF14-inspired synthesis minigame.
#include <stdio.h>
#include "game/game.h"
#include "ui/ui.h"

namespace craft {

namespace {
int s_station = 0;
int s_skill = SK_SMITHING;
int s_list[64];
int s_count = 0;
int s_sel = 0, s_scroll = 0;

// synthesis state
int s_recipe = -1;
int s_progress, s_quality, s_dur, s_maxDur, s_cp, s_maxCp;
int s_step;
int s_innerQuiet;
int s_veneration, s_innovation, s_wasteNot;
bool s_observed;
enum Cond { C_NORMAL, C_GOOD, C_EXCELLENT, C_POOR };
int s_cond;
bool s_done, s_success, s_hq;
f32 s_anim;           // action animation timer
int s_pendingAction = -1;
f32 s_resultTimer;
char s_lastMsg[64];
f32 s_lastMsgTimer;

u32 s_rng = 0xC0FFEE11u;
f32 frand() {
    s_rng = s_rng * 1664525u + 1013904223u;
    return (s_rng >> 8) / 16777216.0f;
}

struct Action {
    const char *name;
    u8 level;
    u8 cp;
    u8 dur;
    const char *desc;
};
const Action ACTIONS[12] = {
    {"Basic Synthesis", 1, 0, 10, "Progress 120%"},
    {"Basic Touch", 1, 18, 10, "Quality 100%"},
    {"Master's Mend", 5, 88, 0, "Restore 30 durability"},
    {"", 1, 0, 0, ""},
    {"Careful Synthesis", 8, 7, 10, "Progress 150%"},
    {"Standard Touch", 10, 32, 10, "Quality 125%"},
    {"Veneration", 12, 18, 0, "Progress +50% (4 steps)"},
    {"Innovation", 15, 18, 0, "Quality +50% (4 steps)"},
    {"Hasty Touch", 3, 0, 10, "Quality 100%, 60% success"},
    {"Rapid Synthesis", 6, 0, 10, "Progress 250%, 50% success"},
    {"Observe", 13, 7, 0, "Wait for better conditions"},
    {"Waste Not", 17, 56, 0, "Halve durability loss (4 steps)"},
};

int stationSkill(int st) {
    switch (st) {
        case ST_FORGE: case ST_ANVIL: return SK_SMITHING;
        case ST_COOKPOT: case ST_CAMPFIRE: return SK_COOKING;
        case ST_WORKBENCH: return SK_CARPENTRY;
        case ST_ALCHEMY: return SK_ALCHEMY;
    }
    return -1;
}

bool recipeAt(const Recipe &r, int st) {
    if (r.station == st) return true;
    if (st == ST_CAMPFIRE && r.station == ST_COOKPOT) return true;
    return false;
}

bool hasMats(const Recipe &r) {
    for (const Ingredient &in : r.in)
        if (in.item && g.pd.inv.count(in.item) < in.count) return false;
    return true;
}

int toolKindFor(int skill) {
    switch (skill) {
        case SK_SMITHING: return TOOL_HAMMER;
        case SK_CARPENTRY: return TOOL_SAW;
        case SK_COOKING: return TOOL_PAN;
        case SK_ALCHEMY: return TOOL_FLASK;
    }
    return TOOL_NONE;
}

int craftsmanship() { return 60 + skillLevel(s_skill) * 7 + toolTier(toolKindFor(s_skill)) * 15; }
int control() { return 50 + skillLevel(s_skill) * 7 + toolTier(toolKindFor(s_skill)) * 15; }
f32 levelMod(const Recipe &r) {
    int diff = skillLevel(s_skill) - r.level;
    return hvClamp(1.0f + diff * 0.04f, 0.6f, 1.3f);
}

void setMsg(const char *m) {
    snprintf(s_lastMsg, sizeof(s_lastMsg), "%s", m);
    s_lastMsgTimer = 1.6f;
}

const char *heldFor(int skill) {
    switch (skill) {
        case SK_SMITHING: return "itm/hammer";
        case SK_CARPENTRY: return "itm/knife";
        case SK_ALCHEMY: return "itm/spellbook";
        default: return nullptr;
    }
}
const char *animFor(int skill) {
    switch (skill) {
        case SK_SMITHING: return "chop";
        case SK_CARPENTRY: return "slash";
        case SK_ALCHEMY: return "cast";
        default: return "interact";
    }
}

void rollCondition() {
    if (s_cond == C_EXCELLENT) {
        s_cond = C_POOR;
        return;
    }
    f32 r = frand();
    if (s_observed) {
        s_cond = r < 0.45f ? C_GOOD : (r < 0.52f ? C_EXCELLENT : C_NORMAL);
        s_observed = false;
        return;
    }
    s_cond = r < 0.2f ? C_GOOD : (r < 0.24f ? C_EXCELLENT : C_NORMAL);
}

f32 condQuality() {
    switch (s_cond) {
        case C_GOOD: return 1.5f;
        case C_EXCELLENT: return 4.0f;
        case C_POOR: return 0.5f;
    }
    return 1.0f;
}

void finish(bool success) {
    s_done = true;
    s_success = success;
    const Recipe &r = RECIPES[s_recipe];
    if (success) {
        f32 q = (f32)s_quality / hvMax(1, (int)r.quality);
        f32 hqChance = q >= 1.0f ? 1.0f : powf(hvSaturate(q), 1.7f);
        s_hq = frand() < hqChance;
        giveItem(r.output, r.outCount, s_hq);
        quests::onCraft(r.output, r.outCount);
        g.pd.crafted++;
        if (s_hq) g.pd.hqCrafted++;
        addXp(s_skill, (u32)(r.xp * (1.0f + q * 0.6f) * (s_hq ? 1.1f : 1.0f)));
        fx::burst(g.player.pos + Vec3(0, 1.4f, 0), s_hq ? FX_GOLD : FX_SPARK, 30);
        audio::sfx(SFX_CRAFT_DONE);
    } else {
        addXp(s_skill, r.xp / 6 + 1);
        audio::sfx(SFX_FAIL);
    }
    s_resultTimer = 2.4f;
}

void applyAction(int a) {
    const Recipe &r = RECIPES[s_recipe];
    const Action &act = ACTIONS[a];
    s_cp -= act.cp;
    f32 mod = levelMod(r);
    int durCost = act.dur;
    if (s_wasteNot > 0 && durCost) durCost /= 2;
    f32 progBase = (craftsmanship() * 0.32f + 12.0f) * mod * (s_veneration > 0 ? 1.5f : 1.0f);
    f32 qualBase = (control() * 0.4f + 18.0f) * mod * condQuality() * (1.0f + s_innerQuiet * 0.1f) * (s_innovation > 0 ? 1.5f : 1.0f);
    char b[64];
    switch (a) {
        case 0: s_progress += (int)(progBase * 1.2f); setMsg("Basic Synthesis"); break;
        case 1: s_quality += (int)(qualBase * 1.0f); s_innerQuiet = hvMin(10, s_innerQuiet + 1); setMsg("Basic Touch"); break;
        case 2: s_dur = hvMin(s_maxDur, s_dur + 30); setMsg("Master's Mend: +30 durability"); break;
        case 4: s_progress += (int)(progBase * 1.5f); setMsg("Careful Synthesis"); break;
        case 5: s_quality += (int)(qualBase * 1.25f); s_innerQuiet = hvMin(10, s_innerQuiet + 1); setMsg("Standard Touch"); break;
        case 6: s_veneration = 5; setMsg("Veneration!"); break;
        case 7: s_innovation = 5; setMsg("Innovation!"); break;
        case 8:
            if (frand() < 0.6f) { s_quality += (int)qualBase; s_innerQuiet = hvMin(10, s_innerQuiet + 1); setMsg("Hasty Touch succeeded"); }
            else setMsg("Hasty Touch failed...");
            break;
        case 9:
            if (frand() < 0.5f) { s_progress += (int)(progBase * 2.5f); setMsg("Rapid Synthesis succeeded!"); }
            else setMsg("Rapid Synthesis failed...");
            break;
        case 10: s_observed = true; setMsg("Observing..."); break;
        case 11: s_wasteNot = 5; setMsg("Waste Not!"); break;
    }
    (void)b;
    s_dur -= durCost;
    s_step++;
    if (s_veneration > 0) s_veneration--;
    if (s_innovation > 0) s_innovation--;
    if (s_wasteNot > 0) s_wasteNot--;
    if (s_quality > r.quality) s_quality = r.quality;
    audio::sfx(SFX_CRAFT_STEP, 1.0f, 0.9f + frand() * 0.25f);
    fx::burst(g.player.pos + Vec3(sinf(g.player.yaw) * 0.9f, 1.1f, cosf(g.player.yaw) * 0.9f), s_skill == SK_ALCHEMY ? FX_MAGIC : FX_SPARK, 8);
    if (s_progress >= r.progress) {
        s_progress = r.progress;
        finish(true);
        return;
    }
    if (s_dur <= 0) {
        s_dur = 0;
        finish(false);
        return;
    }
    rollCondition();
}

void buildList() {
    s_count = 0;
    int lvl = skillLevel(s_skill);
    for (int i = 0; i < NUM_RECIPES && s_count < 64; i++) {
        const Recipe &r = RECIPES[i];
        if (!recipeAt(r, s_station)) continue;
        if (r.level > lvl + 6) continue;
        s_list[s_count++] = i;
    }
}

void exitToPlay() {
    g.mode = MODE_PLAY;
    g.player.setHeld(g.pd.weapon ? ITEMS[g.pd.weapon].model : nullptr);
    g.player.play("idle", 0.25f);
}
}  // namespace

void openStation(int st) {
    int sk = stationSkill(st);
    if (sk < 0) return;
    s_station = st;
    s_skill = sk;
    buildList();
    s_sel = 0;
    s_scroll = 0;
    g.mode = MODE_CRAFT_SELECT;
    // face the station
    for (int i = 0; i < g.numStations; i++)
        if (g.stations[i].type == st && distXZ(g.stations[i].pos, g.player.pos) < 6) g.player.yaw = yawTo(g.player.pos, g.stations[i].pos);
    audio::sfx(SFX_UI_OK);
}

void updateSelect(f32 dt) {
    (void)dt;
    PadState &pad = g.pad;
    if (pad.pressed & BTN_B) {
        exitToPlay();
        audio::sfx(SFX_UI_BACK);
        return;
    }
    if (!s_count) return;
    if (pad.pressed & BTN_UP) { s_sel = (s_sel + s_count - 1) % s_count; audio::sfx(SFX_UI_MOVE); }
    if (pad.pressed & BTN_DOWN) { s_sel = (s_sel + 1) % s_count; audio::sfx(SFX_UI_MOVE); }
    if (s_sel < s_scroll) s_scroll = s_sel;
    if (s_sel >= s_scroll + 6) s_scroll = s_sel - 5;
    if (pad.pressed & BTN_A) {
        const Recipe &r = RECIPES[s_list[s_sel]];
        if (skillLevel(s_skill) < r.level) {
            toast("Your level is too low.", ui::RED);
            audio::sfx(SFX_FAIL);
            return;
        }
        if (!hasMats(r)) {
            toast("Missing materials.", ui::RED);
            audio::sfx(SFX_FAIL);
            return;
        }
        for (const Ingredient &in : r.in)
            if (in.item) g.pd.inv.remove(in.item, in.count);
        s_recipe = s_list[s_sel];
        s_progress = s_quality = 0;
        s_dur = s_maxDur = r.durability;
        s_maxCp = 180 + skillLevel(s_skill) * 6;
        s_cp = s_maxCp;
        s_step = 1;
        s_innerQuiet = 0;
        s_veneration = s_innovation = s_wasteNot = 0;
        s_observed = false;
        s_cond = C_NORMAL;
        s_done = false;
        s_anim = 0;
        s_pendingAction = -1;
        s_lastMsgTimer = 0;
        g.mode = MODE_CRAFT;
        g.player.setHeld(heldFor(s_skill));
        audio::sfx(SFX_UI_OK);
    }
}

void update(f32 dt) {
    PadState &pad = g.pad;
    if (s_lastMsgTimer > 0) s_lastMsgTimer -= dt;
    if (s_done) {
        s_resultTimer -= dt;
        if (s_resultTimer <= 0 || (pad.pressed & BTN_A)) {
            buildList();
            g.mode = MODE_CRAFT_SELECT;
            g.player.play("idle", 0.25f);
        }
        return;
    }
    if (s_anim > 0) {
        f32 before = s_anim;
        s_anim -= dt;
        if (before > 0.35f && s_anim <= 0.35f && s_pendingAction >= 0) {
            applyAction(s_pendingAction);
            s_pendingAction = -1;
        }
        return;
    }
    int a = -1;
    bool L = (pad.held & BTN_L) != 0, R = (pad.held & BTN_R) != 0;
    int base = R ? 4 : (L ? 8 : 0);
    if (pad.pressed & BTN_A) a = base + 0;
    else if (pad.pressed & BTN_X) a = base + 1;
    else if (pad.pressed & BTN_Y) a = base + 2;
    else if (pad.pressed & BTN_B) a = base + 3;
    if (a < 0) return;
    if (a == 3) {
        // quit: materials are lost
        toast("Synthesis abandoned.", ui::TEXT_DIM);
        audio::sfx(SFX_UI_BACK);
        buildList();
        g.mode = MODE_CRAFT_SELECT;
        return;
    }
    const Action &act = ACTIONS[a];
    if (skillLevel(s_skill) < act.level) { setMsg("Not learned yet"); audio::sfx(SFX_FAIL); return; }
    if (s_cp < act.cp) { setMsg("Not enough CP"); audio::sfx(SFX_FAIL); return; }
    s_pendingAction = a;
    s_anim = 0.75f;
    g.player.play(animFor(s_skill), 0.08f, true, 1.25f);
}

void drawSelectUi() {
    f32 W = (f32)plat::screenW();
    f32 x = 24, y = 70, w = 360, h = 360;
    ui::panel(x, y, w, h, ui::PANEL);
    char b[96];
    static const char *const TITLES[] = {"", "Forge", "Anvil", "Cooking Pot", "Workbench", "Alchemy Table", "", "", "Campfire"};
    snprintf(b, sizeof(b), "%s", s_station < HV_ARRAY_COUNT(TITLES) ? TITLES[s_station] : "Craft");
    ui::text(FONT_BIG, x + 16, y + 6, b, ui::GOLD, AL_LEFT, 0.75f);
    snprintf(b, sizeof(b), "%s Lv %d", SKILLS[s_skill].name, skillLevel(s_skill));
    ui::text(FONT_SMALL, x + w - 16, y + 16, b, ui::TEXT_DIM, AL_RIGHT);
    if (!s_count) {
        ui::text(FONT_UI, x + 20, y + 60, "No recipes available yet.", ui::TEXT_DIM);
    }
    for (int k = 0; k < 6 && s_scroll + k < s_count; k++) {
        int ri = s_list[s_scroll + k];
        const Recipe &r = RECIPES[ri];
        f32 ry = y + 46 + k * 48;
        bool sel = s_scroll + k == s_sel;
        bool lvlOk = skillLevel(s_skill) >= r.level;
        bool mats = hasMats(r);
        if (sel) ui::panel(x + 8, ry - 2, w - 16, 46, ui::rgba(255, 220, 140, 70), 8);
        ui::icon(ITEMS[r.output].model, x + 34, ry + 20, 36, 0.7f);
        ui::text(FONT_UI, x + 60, ry + 2, ITEMS[r.output].name, lvlOk ? (mats ? ui::WHITE : ui::TEXT_DIM) : ui::RED);
        snprintf(b, sizeof(b), "Lv %d", r.level);
        ui::text(FONT_SMALL, x + w - 18, ry + 6, b, lvlOk ? ui::TEXT_DIM : ui::RED, AL_RIGHT);
        // materials summary
        f32 mx = x + 60;
        for (const Ingredient &in : r.in) {
            if (!in.item) continue;
            int have = g.pd.inv.count(in.item);
            snprintf(b, sizeof(b), "%s %d/%d", ITEMS[in.item].name, have, in.count);
            mx += ui::text(FONT_SMALL, mx, ry + 23, b, have >= in.count ? ui::GREEN : ui::RED) + 10;
        }
    }
    if (s_count > 6) {
        snprintf(b, sizeof(b), "%d/%d", s_sel + 1, s_count);
        ui::text(FONT_SMALL, x + w - 16, y + h - 22, b, ui::TEXT_DIM, AL_RIGHT);
    }
    // detail panel
    if (s_count) {
        const Recipe &r = RECIPES[s_list[s_sel]];
        f32 dx = x + w + 14, dw = W - dx - 24;
        ui::panel(dx, y, dw, 200, ui::PANEL_DARK);
        ui::icon(ITEMS[r.output].model, dx + dw * 0.5f, y + 60, 80, 1.0f);
        ui::text(FONT_UI, dx + dw * 0.5f, y + 108, ITEMS[r.output].name, ui::GOLD, AL_CENTER);
        ui::textWrap(FONT_SMALL, dx + 12, y + 134, dw - 24, ITEMS[r.output].desc, ui::TEXT_DIM);
        snprintf(b, sizeof(b), "Difficulty %d   Quality %d   Durability %d", r.progress, r.quality, r.durability);
        ui::text(FONT_SMALL, dx + 12, y + 176, b, ui::TEXT_DIM);
    }
    f32 py = y + h + 12;
    f32 px = x;
    px += ui::prompt(GL_A, px, py, "Synthesize");
    ui::prompt(GL_B, px, py, "Close");
}

void drawUi() {
    if (s_recipe < 0) return;
    const Recipe &r = RECIPES[s_recipe];
    f32 W = (f32)plat::screenW(), H = (f32)plat::screenH();
    f32 w = 420, h = 200;
    f32 x = (W - w) * 0.5f, y = 40;
    ui::panel(x, y, w, h, ui::PANEL);
    ui::icon(ITEMS[r.output].model, x + 40, y + 40, 52, 1.0f);
    ui::text(FONT_UI, x + 76, y + 14, ITEMS[r.output].name, ui::GOLD);
    char b[64];
    snprintf(b, sizeof(b), "Step %d", s_step);
    ui::text(FONT_SMALL, x + w - 16, y + 16, b, ui::TEXT_DIM, AL_RIGHT);
    static const char *const CN[] = {"Normal", "Good", "Excellent", "Poor"};
    static const GXColor CC[] = {{220, 220, 230, 255}, {255, 140, 120, 255}, {255, 230, 120, 255}, {130, 130, 150, 255}};
    snprintf(b, sizeof(b), "Condition: %s", CN[s_cond]);
    ui::text(FONT_SMALL, x + 76, y + 38, b, CC[s_cond]);
    // bars
    f32 bx = x + 110, bw = w - 130;
    ui::text(FONT_SMALL, x + 18, y + 70, "Progress", ui::TEXT_DIM);
    ui::bar(bx, y + 72, bw, 12, (f32)s_progress / r.progress, ui::rgba(120, 220, 120));
    snprintf(b, sizeof(b), "%d / %d", s_progress, r.progress);
    ui::text(FONT_SMALL, bx + bw, y + 86, b, ui::WHITE, AL_RIGHT);
    ui::text(FONT_SMALL, x + 18, y + 106, "Quality", ui::TEXT_DIM);
    ui::bar(bx, y + 108, bw, 12, (f32)s_quality / r.quality, ui::rgba(120, 180, 255));
    f32 q = (f32)s_quality / r.quality;
    snprintf(b, sizeof(b), "%d / %d   HQ %d%%", s_quality, r.quality, (int)(powf(hvSaturate(q), 1.7f) * 100));
    ui::text(FONT_SMALL, bx + bw, y + 122, b, ui::WHITE, AL_RIGHT);
    snprintf(b, sizeof(b), "Durability %d / %d", s_dur, s_maxDur);
    ui::text(FONT_SMALL, x + 18, y + 146, b, s_dur <= 10 ? ui::RED : ui::WHITE);
    ui::text(FONT_SMALL, x + 230, y + 146, "CP", ui::GOLD);
    ui::bar(x + 256, y + 148, w - 276, 10, (f32)s_cp / s_maxCp, ui::rgba(200, 120, 255));
    snprintf(b, sizeof(b), "%d", s_cp);
    ui::text(FONT_SMALL, x + w - 18, y + 160, b, ui::WHITE, AL_RIGHT);
    // buffs
    f32 fx2 = x + 18;
    if (s_innerQuiet) { snprintf(b, sizeof(b), "Inner Quiet %d", s_innerQuiet); fx2 += ui::text(FONT_SMALL, fx2, y + 172, b, ui::BLUE) + 12; }
    if (s_veneration) { snprintf(b, sizeof(b), "Veneration %d", s_veneration); fx2 += ui::text(FONT_SMALL, fx2, y + 172, b, ui::GREEN) + 12; }
    if (s_innovation) { snprintf(b, sizeof(b), "Innovation %d", s_innovation); fx2 += ui::text(FONT_SMALL, fx2, y + 172, b, ui::GOLD) + 12; }
    if (s_wasteNot) { snprintf(b, sizeof(b), "Waste Not %d", s_wasteNot); ui::text(FONT_SMALL, fx2, y + 172, b, ui::TEXT_DIM); }
    if (s_lastMsgTimer > 0) ui::text(FONT_UI, W * 0.5f, y + h + 10, s_lastMsg, ui::rgba(255, 255, 255, (u8)(hvSaturate(s_lastMsgTimer) * 255)), AL_CENTER);
    // action palette
    if (!s_done) {
        bool L = (g.pad.held & BTN_L) != 0, R = (g.pad.held & BTN_R) != 0;
        int base = R ? 4 : (L ? 8 : 0);
        f32 ax = 24, ay = H - 132;
        ui::panel(ax - 8, ay - 8, 300, 116, ui::PANEL_DARK);
        ui::text(FONT_SMALL, ax, ay - 2, R ? "R  Synthesis & buffs" : (L ? "L  Risky & utility" : "Actions   (hold L / R for more)"), ui::GOLD);
        Glyph gl[4] = {GL_A, GL_X, GL_Y, GL_B};
        for (int k = 0; k < 4; k++) {
            int ai = base + k;
            f32 ry = ay + 18 + k * 22;
            ui::glyph(gl[k], ax, ry - 3, 22);
            if (ai == 3) {
                ui::text(FONT_SMALL, ax + 28, ry, "Quit (lose materials)", ui::TEXT_DIM);
                continue;
            }
            const Action &act = ACTIONS[ai];
            bool ok = skillLevel(s_skill) >= act.level;
            if (ok) snprintf(b, sizeof(b), "%s  %s%s", act.name, act.cp ? "" : "", act.desc);
            else snprintf(b, sizeof(b), "%s  (Lv %d)", act.name, act.level);
            ui::text(FONT_SMALL, ax + 28, ry, b, ok ? (s_cp >= act.cp ? ui::WHITE : ui::RED) : ui::TEXT_DIM);
            if (ok && act.cp) {
                snprintf(b, sizeof(b), "%d CP", act.cp);
                ui::text(FONT_SMALL, ax + 286, ry, b, ui::rgba(200, 150, 255), AL_RIGHT);
            }
        }
    } else {
        const char *t = s_success ? (s_hq ? "HQ Synthesis!" : "Synthesis complete!") : "Synthesis failed...";
        ui::text(FONT_BIG, W * 0.5f, H * 0.6f, t, s_success ? (s_hq ? ui::GOLD : ui::GREEN) : ui::RED, AL_CENTER, 0.9f);
    }
}

}  // namespace craft

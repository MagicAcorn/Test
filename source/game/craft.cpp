// Crafting: recipe selection and a forge minigame in the spirit of Dragon
// Quest XI's Fun-Sized Forge: every item is a row of parts, each with a
// target zone; hit parts with techniques (focus costs), land them in their
// zones, then finish. The score decides quality (HQ) and XP.
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

// forge state
const int MAX_PARTS = 6;
int s_recipe = -1;
int s_parts;
int s_val[MAX_PARTS], s_lo[MAX_PARTS], s_hi[MAX_PARTS];
bool s_broken[MAX_PARTS];
int s_cur = 0;                 // selected part
int s_focus, s_maxFocus;
int s_step;
bool s_inspired;               // next hit doubled
bool s_done, s_success, s_hq;
int s_score;                   // 0..100
f32 s_anim;
int s_pendingAction = -1;
f32 s_resultTimer;
char s_lastMsg[64];
f32 s_lastMsgTimer;
f32 s_flash[MAX_PARTS];
const int GAUGE_MAX = 120;

u32 s_rng = 0xC0FFEE11u;
f32 frand() {
    s_rng = s_rng * 1664525u + 1013904223u;
    return (s_rng >> 8) / 16777216.0f;
}
int irand(int lo, int hi) { return lo + (int)(frand() * (hi - lo + 1)); }

// technique ids
enum { T_HIT, T_HEAVY, T_TAP, T_FINISH, T_SWEEP, T_INSPIRE, T_COOL, T_COUNT };
struct Technique {
    u8 level, focus;
    s8 lo, hi;          // gauge change
    bool all;           // affects every part
};
const Technique TECH[T_COUNT] = {
    {1, 5, 10, 20, false},   // A
    {1, 9, 20, 35, false},   // X
    {1, 7, 1, 6, false},     // Y
    {1, 0, 0, 0, false},     // B finish
    {4, 14, 5, 11, true},    // R+A
    {8, 10, 0, 0, false},    // R+X
    {3, 8, -12, -5, false},  // R+Y
};
const char *const TNAMES[4][T_COUNT] = {
    {"Strike", "Heavy Strike", "Precise Tap", "Finish", "Hammer Sweep", "Flux", "Quench"},       // smithing
    {"Stir", "Sear", "Pinch of Salt", "Plate Up", "Simmer All", "Secret Spice", "Cool Down"},    // cooking
    {"Cut", "Chop", "Sand", "Assemble", "Plane Over", "Measure Twice", "Shave Back"},             // carpentry
    {"Infuse", "Boil", "Single Drop", "Bottle", "Swirl", "Moonlight", "Chill"},                   // alchemy
};
int skillRow() { return s_skill == SK_COOKING ? 1 : (s_skill == SK_CARPENTRY ? 2 : (s_skill == SK_ALCHEMY ? 3 : 0)); }
const char *techName(int t) { return TNAMES[skillRow()][t]; }

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

void setupForge(int recipe) {
    const Recipe &r = RECIPES[recipe];
    s_recipe = recipe;
    s_parts = hvClamp(2 + (int)r.progress / 45, 2, MAX_PARTS);
    // target zones get narrower for recipes above your level, wider below it
    int diff = skillLevel(s_skill) - r.level;
    int width = hvClamp(12 + diff * 2 + toolTier(toolKindFor(s_skill)) * 2, 7, 22);
    for (int i = 0; i < s_parts; i++) {
        int c = irand(42, 96);
        s_lo[i] = c - width / 2;
        s_hi[i] = s_lo[i] + width;
        s_val[i] = 0;
        s_broken[i] = false;
        s_flash[i] = 0;
    }
    s_maxFocus = 34 + s_parts * 15 + skillLevel(s_skill) * 3 + toolTier(toolKindFor(s_skill)) * 8;
    s_focus = s_maxFocus;
    s_cur = 0;
    s_step = 0;
    s_inspired = false;
    s_done = false;
    s_anim = 0;
    s_pendingAction = -1;
    s_lastMsgTimer = 0;
}

int partScore(int i) {
    if (s_broken[i]) return 0;
    int v = s_val[i];
    if (v >= s_lo[i] && v <= s_hi[i]) return 100;
    int d = v < s_lo[i] ? s_lo[i] - v : v - s_hi[i];
    return hvMax(0, 70 - d * 4);
}

void finish() {
    s_done = true;
    const Recipe &r = RECIPES[s_recipe];
    int total = 0;
    for (int i = 0; i < s_parts; i++) total += partScore(i);
    s_score = total / s_parts;
    s_success = s_score >= 35;
    if (s_success) {
        s_hq = s_score >= 90 || (s_score >= 70 && frand() < (s_score - 70) / 25.0f);
        giveItem(r.output, r.outCount, s_hq);
        quests::onCraft(r.output, r.outCount);
        g.pd.crafted++;
        if (s_hq) g.pd.hqCrafted++;
        addXp(s_skill, (u32)(r.xp * (0.6f + s_score / 100.0f) * (s_hq ? 1.15f : 1.0f)));
        fx::burst(g.player.pos + Vec3(0, 1.4f, 0), s_hq ? FX_GOLD : FX_SPARK, 30);
        audio::sfx(SFX_CRAFT_DONE);
    } else {
        addXp(s_skill, r.xp / 6 + 1);
        audio::sfx(SFX_FAIL);
    }
    s_resultTimer = 2.6f;
}

void hitPart(int i, int amount, bool crit) {
    if (s_broken[i]) return;
    s_val[i] += amount;
    if (s_val[i] < 0) s_val[i] = 0;
    s_flash[i] = crit ? 0.6f : 0.35f;
    if (s_val[i] > GAUGE_MAX) {
        s_val[i] = GAUGE_MAX;
        s_broken[i] = true;
        setMsg("A part cracked!");
        audio::sfx(SFX_FAIL, 0.8f);
    }
}

void applyTechnique(int t) {
    const Technique &tc = TECH[t];
    s_focus -= tc.focus;
    s_step++;
    char b[64];
    if (t == T_INSPIRE) {
        s_inspired = true;
        snprintf(b, sizeof(b), "%s: next hit counts double", techName(t));
        setMsg(b);
    } else {
        bool crit = tc.hi > 6 && frand() < 0.14f;
        int mul = (crit ? 2 : 1) * (s_inspired && tc.hi > 0 ? 2 : 1);
        if (tc.hi > 0) s_inspired = false;
        if (tc.all) {
            for (int i = 0; i < s_parts; i++) hitPart(i, irand(tc.lo, tc.hi) * mul, crit);
        } else {
            hitPart(s_cur, irand(tc.lo, tc.hi) * (tc.lo < 0 ? 1 : mul), crit);
        }
        if (crit) setMsg("Critical hit!");
        else if (mul > 1) setMsg("Inspired!");
        else setMsg(techName(t));
    }
    audio::sfx(SFX_CRAFT_STEP, 1.0f, 0.85f + frand() * 0.3f);
    fx::burst(g.player.pos + Vec3(sinf(g.player.yaw) * 0.9f, 1.1f, cosf(g.player.yaw) * 0.9f), s_skill == SK_ALCHEMY ? FX_MAGIC : FX_SPARK, 8);
    // out of focus: the piece is done
    int cheapest = 99;
    for (int k = 0; k < T_COUNT; k++)
        if (k != T_FINISH && skillLevel(s_skill) >= TECH[k].level) cheapest = hvMin(cheapest, (int)TECH[k].focus);
    if (s_focus < cheapest) finish();
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

int selectedRecipe() { return (g.mode == MODE_CRAFT_SELECT && s_count) ? s_list[s_sel] : -1; }
bool synthesisDone() { return s_done; }
int forgeParts() { return g.mode == MODE_CRAFT ? s_parts : 0; }
int forgeSelected() { return s_cur; }
void forgePart(int i, int *val, int *lo, int *hi) {
    *val = s_val[i];
    *lo = s_lo[i];
    *hi = s_hi[i];
}

void openStation(int st) {
    int sk = stationSkill(st);
    if (sk < 0) return;
    s_station = st;
    g.stationOpen = st;
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
        setupForge(s_list[s_sel]);
        g.mode = MODE_CRAFT;
        g.player.setHeld(heldFor(s_skill));
        audio::sfx(SFX_UI_OK);
    }
}

void update(f32 dt) {
    PadState &pad = g.pad;
    if (s_lastMsgTimer > 0) s_lastMsgTimer -= dt;
    for (int i = 0; i < s_parts; i++)
        if (s_flash[i] > 0) s_flash[i] -= dt;
    if (s_done) {
        s_resultTimer -= dt;
        if (s_resultTimer <= 0 || (pad.pressed & BTN_A)) {
            buildList();
            g.mode = MODE_CRAFT_SELECT;
            g.player.play("idle", 0.25f);
        }
        return;
    }
    // choose a part with the stick or D-pad
    static f32 stickHold = 0;
    int dir = 0;
    if (pad.pressed & BTN_RIGHT) dir = 1;
    if (pad.pressed & BTN_LEFT) dir = -1;
    if (fabsf(pad.sx) > 0.6f) {
        if (stickHold <= 0) dir = pad.sx > 0 ? 1 : -1, stickHold = 0.22f;
        else stickHold -= dt;
    } else {
        stickHold = 0;
    }
    if (dir) {
        s_cur = (s_cur + dir + s_parts) % s_parts;
        audio::sfx(SFX_UI_MOVE, 0.6f);
    }
    if (s_anim > 0) {
        f32 before = s_anim;
        s_anim -= dt;
        if (before > 0.25f && s_anim <= 0.25f && s_pendingAction >= 0) {
            applyTechnique(s_pendingAction);
            s_pendingAction = -1;
        }
        return;
    }
    bool R = (pad.held & BTN_R) != 0;
    int t = -1;
    if (pad.pressed & BTN_A) t = R ? T_SWEEP : T_HIT;
    else if (pad.pressed & BTN_X) t = R ? T_INSPIRE : T_HEAVY;
    else if (pad.pressed & BTN_Y) t = R ? T_COOL : T_TAP;
    else if (pad.pressed & BTN_B) t = T_FINISH;
    if (t < 0) return;
    if (t == T_FINISH) {
        finish();
        return;
    }
    const Technique &tc = TECH[t];
    if (skillLevel(s_skill) < tc.level) { setMsg("Not learned yet"); audio::sfx(SFX_FAIL); return; }
    if (s_focus < tc.focus) { setMsg("Not enough focus"); audio::sfx(SFX_FAIL); return; }
    s_pendingAction = t;
    s_anim = 0.5f;
    g.player.play(animFor(s_skill), 0.06f, true, 1.5f);
}

void drawSelectUi() {
    f32 W = ui::width();
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
    f32 W = ui::width(), H = ui::height();
    char b[64];
    // header
    f32 w = 470, x = (W - w) * 0.5f, y = 18;
    ui::panel(x, y, w, 58, ui::PANEL);
    ui::icon(ITEMS[r.output].model, x + 32, y + 29, 44, 1.0f);
    ui::text(FONT_UI, x + 62, y + 8, ITEMS[r.output].name, ui::GOLD);
    snprintf(b, sizeof(b), "Focus %d / %d", s_focus, s_maxFocus);
    ui::text(FONT_SMALL, x + 62, y + 32, b, ui::rgba(200, 160, 255));
    ui::bar(x + 190, y + 36, w - 210, 9, (f32)s_focus / s_maxFocus, ui::rgba(190, 120, 255));
    if (s_inspired) ui::text(FONT_SMALL, x + w - 14, y + 10, "Inspired!", ui::GOLD, AL_RIGHT);
    // the parts: vertical gauges with target zones
    f32 gw = 54, gap = 16, gh = 170;
    f32 total = s_parts * gw + (s_parts - 1) * gap;
    f32 gx0 = (W - total) * 0.5f, gy = 108;
    ui::panel(gx0 - 18, gy - 22, total + 36, gh + 64, ui::rgba(20, 18, 28, 200), 14);
    for (int i = 0; i < s_parts; i++) {
        f32 gx = gx0 + i * (gw + gap);
        bool sel = i == s_cur && !s_done;
        ui::rect(gx, gy, gw, gh, ui::rgba(40, 36, 52, 230));
        // target zone
        f32 zy0 = gy + gh * (1.0f - (f32)s_hi[i] / GAUGE_MAX), zy1 = gy + gh * (1.0f - (f32)s_lo[i] / GAUGE_MAX);
        ui::rect(gx, zy0, gw, zy1 - zy0, ui::rgba(255, 200, 80, 120));
        // fill
        f32 fh = gh * (f32)s_val[i] / GAUGE_MAX;
        int sc = partScore(i);
        GXColor fc = s_broken[i] ? ui::rgba(120, 60, 60) : (sc == 100 ? ui::rgba(120, 230, 120) : ui::rgba(110, 170, 255));
        if (s_flash[i] > 0) fc = ui::rgba(255, 255, 255);
        ui::rect(gx + 8, gy + gh - fh, gw - 16, fh, fc);
        // zone edges stay visible on top of the fill
        ui::rect(gx - 3, zy0 - 1, gw + 6, 3, ui::rgba(255, 210, 90));
        ui::rect(gx - 3, zy1 - 1, gw + 6, 3, ui::rgba(255, 210, 90));
        snprintf(b, sizeof(b), "%d", s_val[i]);
        ui::text(FONT_SMALL, gx + gw * 0.5f, gy + gh + 4, b, s_broken[i] ? ui::RED : ui::WHITE, AL_CENTER);
        snprintf(b, sizeof(b), "%d-%d", s_lo[i], s_hi[i]);
        ui::text(FONT_SMALL, gx + gw * 0.5f, gy - 19, b, ui::GOLD, AL_CENTER, 0.85f);
        if (sel) {
            ui::panel(gx - 4, gy + gh + 22, gw + 8, 8, ui::SEL, 4);
            ui::text(FONT_UI, gx + gw * 0.5f, gy + gh + 26, "^", ui::SEL, AL_CENTER);
        }
    }
    if (s_lastMsgTimer > 0) ui::text(FONT_UI, W * 0.5f, gy + gh + 46, s_lastMsg, ui::rgba(255, 255, 255, (u8)(hvSaturate(s_lastMsgTimer) * 255)), AL_CENTER);
    if (s_done) {
        const char *t = s_success ? (s_hq ? "Masterwork! (HQ)" : "Crafted!") : "It fell apart...";
        ui::text(FONT_BIG, W * 0.5f, H * 0.66f, t, s_success ? (s_hq ? ui::GOLD : ui::GREEN) : ui::RED, AL_CENTER, 0.9f);
        snprintf(b, sizeof(b), "Score %d", s_score);
        ui::text(FONT_UI, W * 0.5f, H * 0.66f + 42, b, ui::WHITE, AL_CENTER);
        return;
    }
    // technique palette
    bool R = (g.pad.held & BTN_R) != 0;
    f32 px = 24, py = H - 128;
    ui::panel(px - 8, py - 8, W - 32, 118, ui::PANEL_DARK);
    ui::text(FONT_SMALL, px, py - 2, R ? "R  Special techniques" : "Techniques   (hold R for more,  stick/D-pad: choose part)", ui::GOLD);
    Glyph gl[4] = {GL_A, GL_X, GL_Y, GL_B};
    int ts[4] = {R ? T_SWEEP : T_HIT, R ? T_INSPIRE : T_HEAVY, R ? T_COOL : T_TAP, T_FINISH};
    for (int k = 0; k < 4; k++) {
        int t = ts[k];
        const Technique &tc = TECH[t];
        f32 cx = px + (k % 2) * ((W - 48) * 0.5f), cy = py + 22 + (k / 2) * 40;
        bool ok = skillLevel(s_skill) >= tc.level;
        ui::glyph(gl[k], cx, cy - 3, 24);
        ui::text(FONT_UI, cx + 30, cy - 4, techName(t), ok ? (s_focus >= tc.focus ? ui::WHITE : ui::RED) : ui::TEXT_DIM, AL_LEFT, 0.9f);
        if (t == T_FINISH) snprintf(b, sizeof(b), "end and score the piece");
        else if (!ok) snprintf(b, sizeof(b), "learn at Lv %d", tc.level);
        else if (t == T_INSPIRE) snprintf(b, sizeof(b), "next hit x2   %d focus", tc.focus);
        else snprintf(b, sizeof(b), "%s%+d to %+d   %d focus", tc.all ? "all parts " : "", tc.lo, tc.hi, tc.focus);
        ui::text(FONT_SMALL, cx + 30, cy + 16, b, ui::TEXT_DIM, AL_LEFT, 0.85f);
    }
}

}  // namespace craft

// Gathering nodes, the FF14-style gathering window and fishing.
#include <stdio.h>
#include <stdlib.h>
#include "game/game.h"
#include "game/world.h"
#include "gfx/water.h"
#include "ui/ui.h"

static u32 g_rng = 0x1234567;
static inline f32 frand() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return (g_rng & 0xFFFFFF) / 16777216.0f;
}

// =============================================================== nodes
namespace nodes {

void init() {
    g.numNodes = 0;
    for (int i = 0; i < g_world.numMarkers() && g.numNodes < MAX_NODES; i++) {
        const Marker &m = g_world.marker(i);
        if (m.kind != MK_NODE && m.kind != MK_FISH) continue;
        const NodeDef *d = nodeDef(m.id);
        if (!d) continue;
        Node &n = g.nodes[g.numNodes++];
        n.type = (u8)m.id;
        n.pos = m.pos;
        n.yaw = m.yaw;
        n.respawn = 0;
        n.integrity = d->integrity;
        n.fishing = m.kind == MK_FISH;
        n.sparkle = frand() * 3;
    }
    hvLog("nodes: %d", g.numNodes);
}

static bool visibleNow(const Node &n) {
    const NodeDef *d = nodeDef(n.type);
    return !(d->nightOnly && !isNight());
}

void update(f32 dt) {
    for (int i = 0; i < g.numNodes; i++) {
        Node &n = g.nodes[i];
        if (n.respawn > 0) {
            n.respawn -= dt;
            if (n.respawn <= 0) {
                n.respawn = 0;
                n.integrity = nodeDef(n.type)->integrity;
            }
        }
        // occasional sparkle on gatherable nodes near the player
        if (!n.fishing && n.respawn <= 0 && visibleNow(n)) {
            n.sparkle -= dt;
            if (n.sparkle < 0) {
                n.sparkle = 1.6f + frand() * 2.0f;
                const NodeDef *d = nodeDef(n.type);
                if (distXZ(n.pos, g.player.pos) < 28 && skillLevel(d->skill) >= d->level && d->skill != SK_WOODCUTTING)
                    fx::burst(n.pos + Vec3(0, 0.8f, 0), FX_SPARK, 3);
            }
        }
    }
}

void draw() {
    const Camera &cam = gfx::camera();
    for (int i = 0; i < g.numNodes; i++) {
        const Node &n = g.nodes[i];
        const NodeDef *d = nodeDef(n.type);
        f32 dist = distXZ(n.pos, cam.eye);
        if (n.fishing) {
            if (dist < 60) {
                f32 t = g.time * 0.6f + i;
                for (int k = 0; k < 2; k++) {
                    f32 ph = t + k * 0.5f;
                    ph -= floorf(ph);
                    water::drawRipple(n.pos, 0.6f + ph * 2.6f, (1.0f - ph) * 0.5f);
                }
            }
            continue;
        }
        if (!visibleNow(n)) continue;
        if (dist > 160) continue;
        const char *model = n.respawn > 0 ? d->depletedModel : d->model;
        if (!model) continue;
        const Model *m = mdl::get(model);
        f32 sc = d->scale;
        if (n.respawn > 0 && d->skill == SK_WOODCUTTING) sc = 1.0f;
        gfx::drawModel(m, Mat34::place(n.pos, n.yaw, sc), gxc(255, 255, 255), d->skill == SK_WOODCUTTING ? SH_RIM : 0);
    }
}

int nearest(const Vec3 &p, f32 maxDist, bool fishingOnly) {
    int best = -1;
    f32 bd = 1e9f;
    for (int i = 0; i < g.numNodes; i++) {
        const Node &n = g.nodes[i];
        if (fishingOnly && !n.fishing) continue;
        if (n.respawn > 0 || !visibleNow(n)) continue;
        f32 limit = n.fishing ? 9.5f : (nodeDef(n.type)->skill == SK_WOODCUTTING ? maxDist + 0.6f : maxDist);
        f32 d = distXZ(n.pos, p);
        if (d < limit && d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

}  // namespace nodes

// ============================================================ gathering
namespace gather {

namespace {
int s_node = -1;
int s_sel = 0;
int s_lootIdx[3];
int s_numLoot = 0;
f32 s_swing = 0;       // >0 while a swing is in progress
bool s_resolved = false;
int s_steady = 0;
bool s_bountiful = false, s_lucky = false, s_solidUsed = false;
f32 s_closeTimer = 0;
char s_msg[64];
f32 s_msgTimer = 0;

const char *swingAnim(int skill) {
    switch (skill) {
        case SK_WOODCUTTING: return "chop";
        case SK_MINING: return "heavy";
        default: return "pickup";
    }
}

int toolFor(int skill) {
    switch (skill) {
        case SK_WOODCUTTING: return TOOL_AXE;
        case SK_MINING: return TOOL_PICK;
        case SK_HERBALISM: return TOOL_SICKLE;
        case SK_FISHING: return TOOL_ROD;
    }
    return TOOL_NONE;
}

int chanceFor(const NodeDef *d, int li) {
    const NodeLoot &l = d->loot[li];
    int lvl = skillLevel(d->skill);
    if (lvl < l.level) return 0;
    int c = l.chance + (lvl - l.level) * 2 + toolTier(toolFor(d->skill)) * 6 + (s_steady > 0 ? 25 : 0);
    return hvClamp(c, 5, 100);
}

void msg(const char *m) {
    snprintf(s_msg, sizeof(s_msg), "%s", m);
    s_msgTimer = 2.0f;
}

void end() {
    g.mode = MODE_PLAY;
    s_node = -1;
    g.player.setHeld(g.pd.weapon ? ITEMS[g.pd.weapon].model : nullptr);
    g.player.play("idle", 0.25f);
}

const char *gpAbilityName(int k) {
    static const char *const N[] = {"Steady Hand", "Bountiful Yield", "Solid Reason", "Lucky Break"};
    return N[k];
}
const int GP_COST[4] = {120, 160, 260, 200};
const int GP_LEVEL[4] = {3, 7, 12, 18};
}  // namespace

void begin(int node) {
    Node &n = g.nodes[node];
    const NodeDef *d = nodeDef(n.type);
    int lvl = skillLevel(d->skill);
    if (lvl < d->level) {
        char b[64];
        snprintf(b, sizeof(b), "Requires %s Lv %d", SKILLS[d->skill].name, d->level);
        toast(b, ui::RED);
        audio::sfx(SFX_FAIL);
        return;
    }
    int tool = bestTool(toolFor(d->skill));
    if (!tool) {
        static const char *const NEED[] = {"You need a hatchet.", "You need a pickaxe.", "You need a fishing rod.", "You need a sickle."};
        toast(NEED[d->skill], ui::RED);
        audio::sfx(SFX_FAIL);
        return;
    }
    s_node = node;
    s_numLoot = 0;
    for (int i = 0; i < 3; i++)
        if (d->loot[i].item) s_lootIdx[s_numLoot++] = i;
    s_sel = 0;
    s_swing = 0;
    s_steady = 0;
    s_bountiful = s_lucky = s_solidUsed = false;
    s_closeTimer = 0;
    s_msgTimer = 0;
    g.mode = MODE_GATHER;
    Actor &p = g.player;
    p.yaw = yawTo(p.pos, n.pos);
    p.setHeld(ITEMS[tool].model);
    p.play("idle", 0.2f);
    audio::sfx(SFX_UI_OK);
}

static void resolve() {
    Node &n = g.nodes[s_node];
    const NodeDef *d = nodeDef(n.type);
    int li = s_lootIdx[s_sel];
    const NodeLoot &l = d->loot[li];
    int chance = chanceFor(d, li);
    // feedback particles
    int fxk = d->skill == SK_WOODCUTTING ? FX_LEAF : (d->skill == SK_MINING ? FX_ROCK : FX_LEAF);
    fx::burst(n.pos + Vec3(0, d->skill == SK_WOODCUTTING ? 1.4f : 0.7f, 0), fxk, 10);
    audio::sfx(d->skill == SK_WOODCUTTING ? SFX_CHOP : (d->skill == SK_MINING ? SFX_MINE : SFX_PICK), 1.0f, 0.9f + frand() * 0.2f);
    if (s_steady > 0) s_steady--;
    if (frand() * 100.0f < chance) {
        int count = 1 + (s_bountiful ? 1 : 0) + (frand() < 0.08f + skillLevel(d->skill) * 0.006f ? 1 : 0);
        s_bountiful = false;
        bool hq = frand() < 0.06f + toolTier(toolFor(d->skill)) * 0.02f;
        giveItem(l.item, count, hq);
        quests::onGather(l.item, count);
        g.pd.gathered += count;
        u32 xp = d->xp * (100 + (hq ? 50 : 0)) / 100;
        addXp(d->skill, xp);
        // gems / rare bonus
        if (d->skill == SK_MINING && frand() < (s_lucky ? 0.35f : 0.05f)) {
            static const u16 GEMS[3] = {IT_RUBY, IT_SAPPHIRE, IT_EMERALD};
            u16 gem = GEMS[(int)(frand() * 3) % 3];
            giveItem(gem, 1);
            quests::onGather(gem, 1);
        }
        s_lucky = false;
        char b[48];
        snprintf(b, sizeof(b), "+%d %s", count, ITEMS[l.item].name);
        fx::floatText(n.pos + Vec3(0, 2.2f, 0), b, ui::WHITE);
    } else {
        msg("Missed!");
        fx::floatText(n.pos + Vec3(0, 2.2f, 0), "Miss", ui::TEXT_DIM);
    }
    if (n.integrity > 0) n.integrity--;
    if (n.integrity == 0) {
        n.respawn = d->respawn;
        s_closeTimer = 1.0f;
        if (d->skill == SK_WOODCUTTING) fx::burst(n.pos + Vec3(0, 2.0f, 0), FX_LEAF, 30);
    }
}

void update(f32 dt) {
    if (s_node < 0) {
        end();
        return;
    }
    Node &n = g.nodes[s_node];
    const NodeDef *d = nodeDef(n.type);
    PadState &pad = g.pad;
    if (s_msgTimer > 0) s_msgTimer -= dt;
    if (s_closeTimer > 0) {
        s_closeTimer -= dt;
        if (s_closeTimer <= 0) end();
        return;
    }
    if (s_swing > 0) {
        f32 before = s_swing;
        s_swing -= dt;
        // impact moment ~0.55s into the swing
        if (before > 0.45f && s_swing <= 0.45f && !s_resolved) {
            s_resolved = true;
            resolve();
        }
        return;
    }
    if (pad.pressed & BTN_B) {
        audio::sfx(SFX_UI_BACK);
        end();
        return;
    }
    if (pad.pressed & BTN_UP) { s_sel = (s_sel + s_numLoot - 1) % s_numLoot; audio::sfx(SFX_UI_MOVE); }
    if (pad.pressed & BTN_DOWN) { s_sel = (s_sel + 1) % s_numLoot; audio::sfx(SFX_UI_MOVE); }
    bool r = (pad.held & BTN_R) != 0;
    if (r) {
        int k = -1;
        if (pad.pressed & BTN_A) k = 0;
        if (pad.pressed & BTN_X) k = 1;
        if (pad.pressed & BTN_Y) k = 2;
        if (pad.pressed & BTN_B) k = 3;
        if (k >= 0) {
            int lvl = skillLevel(d->skill);
            if (lvl < GP_LEVEL[k]) { toast("Ability not learned yet", ui::RED); audio::sfx(SFX_FAIL); }
            else if (g.gp < GP_COST[k]) { toast("Not enough GP", ui::RED); audio::sfx(SFX_FAIL); }
            else if (k == 2 && s_solidUsed) { toast("Already used on this node", ui::RED); }
            else {
                g.gp -= GP_COST[k];
                if (k == 0) s_steady = 2;
                if (k == 1) s_bountiful = true;
                if (k == 2) { n.integrity++; s_solidUsed = true; }
                if (k == 3) s_lucky = true;
                char b[48];
                snprintf(b, sizeof(b), "%s!", gpAbilityName(k));
                fx::floatText(g.player.pos + Vec3(0, 2.6f, 0), b, ui::GOLD);
                fx::burst(g.player.pos + Vec3(0, 1.2f, 0), FX_MAGIC, 14);
                audio::sfx(SFX_HEAL);
            }
        }
        return;
    }
    if (pad.pressed & BTN_A) {
        int li = s_lootIdx[s_sel];
        if (chanceFor(d, li) <= 0) {
            msg("Your skill is too low for that.");
            audio::sfx(SFX_FAIL);
            return;
        }
        g.player.play(swingAnim(d->skill), 0.1f, true, d->skill == SK_HERBALISM ? 1.4f : 1.15f);
        s_swing = 1.0f;
        s_resolved = false;
    }
}

void drawUi() {
    if (s_node < 0) return;
    Node &n = g.nodes[s_node];
    const NodeDef *d = nodeDef(n.type);
    f32 W = ui::width();
    f32 x = W - 300, y = 150, w = 280;
    f32 h = 92 + s_numLoot * 48;
    ui::panel(x, y, w, h, ui::PANEL);
    ui::text(FONT_UI, x + 16, y + 10, d->name, ui::GOLD);
    char b[64];
    snprintf(b, sizeof(b), "%s Lv %d", SKILLS[d->skill].name, skillLevel(d->skill));
    ui::text(FONT_SMALL, x + w - 14, y + 13, b, ui::TEXT_DIM, AL_RIGHT);
    // integrity pips
    ui::text(FONT_SMALL, x + 16, y + 36, "Integrity", ui::TEXT_DIM);
    for (int i = 0; i < d->integrity + (s_solidUsed ? 1 : 0); i++) {
        GXColor c = i < n.integrity ? ui::rgba(130, 220, 150) : ui::rgba(60, 60, 70);
        ui::panel(x + 90 + i * 18, y + 37, 13, 13, c, 4);
    }
    for (int k = 0; k < s_numLoot; k++) {
        int li = s_lootIdx[k];
        const NodeLoot &l = d->loot[li];
        f32 ry = y + 62 + k * 48;
        bool sel = k == s_sel;
        if (sel) ui::panel(x + 8, ry - 2, w - 16, 44, ui::rgba(255, 220, 140, 70), 8);
        int ch = chanceFor(d, li);
        ui::icon(ITEMS[l.item].model, x + 34, ry + 20, 36, 0.8f);
        ui::text(FONT_UI, x + 60, ry + 2, ITEMS[l.item].name, ch > 0 ? ui::WHITE : ui::TEXT_DIM);
        if (ch > 0) snprintf(b, sizeof(b), "%d%%", ch);
        else snprintf(b, sizeof(b), "Lv %d", l.level);
        ui::text(FONT_UI, x + w - 18, ry + 10, b, ch > 0 ? (ch >= 80 ? ui::GREEN : ui::WHITE) : ui::RED, AL_RIGHT);
        snprintf(b, sizeof(b), "Lv %d", l.level);
        ui::text(FONT_SMALL, x + 60, ry + 22, b, ui::TEXT_DIM);
    }
    // GP bar + buffs
    f32 by = y + h - 24;
    ui::text(FONT_SMALL, x + 16, by - 3, "GP", ui::GOLD);
    ui::bar(x + 44, by, w - 64, 10, (f32)g.gp / hvMax(1, g.maxGp), ui::rgba(240, 190, 80));
    // prompts
    f32 py = y + h + 10;
    f32 px = x + 4;
    px += ui::prompt(GL_A, px, py, SKILLS[d->skill].verb);
    px += ui::prompt(GL_B, px, py, "Leave");
    ui::prompt(GL_R, x + 4, py + 30, "+ buttons: GP abilities");
    if (g.pad.held & BTN_R) {
        f32 ay = y - 112;
        ui::panel(x, ay, w, 104, ui::PANEL_DARK);
        Glyph gl[4] = {GL_A, GL_X, GL_Y, GL_B};
        for (int k = 0; k < 4; k++) {
            bool ok = skillLevel(d->skill) >= GP_LEVEL[k];
            ui::glyph(gl[k], x + 12, ay + 8 + k * 24, 22, ok ? 255 : 90);
            snprintf(b, sizeof(b), ok ? "%s  (%d GP)" : "%s  (Lv %d)", gpAbilityName(k), ok ? GP_COST[k] : GP_LEVEL[k]);
            ui::text(FONT_SMALL, x + 40, ay + 11 + k * 24, b, ok ? ui::WHITE : ui::TEXT_DIM);
        }
    }
    if (s_steady > 0 || s_bountiful || s_lucky) {
        f32 bx = x + 16;
        if (s_steady > 0) bx += ui::text(FONT_SMALL, bx, y + h - 44, "Steady", ui::GREEN) + 10;
        if (s_bountiful) bx += ui::text(FONT_SMALL, bx, y + h - 44, "Bountiful", ui::GOLD) + 10;
        if (s_lucky) ui::text(FONT_SMALL, bx, y + h - 44, "Lucky", ui::BLUE);
    }
    if (s_msgTimer > 0) ui::text(FONT_UI, W * 0.5f, 120, s_msg, ui::rgba(255, 255, 255, (u8)(hvSaturate(s_msgTimer) * 255)), AL_CENTER);
}

// ============================================================== fishing
namespace {
enum FishState { FS_READY, FS_CAST, FS_WAIT, FS_BITE, FS_REEL, FS_RESULT };
FishState f_state;
f32 f_timer;
Vec3 f_bobber;
int f_spot = -1;
u16 f_caught;
}  // namespace

void beginFishing(int node) {
    Node &n = g.nodes[node];
    int rod = bestTool(TOOL_ROD);
    if (!rod) {
        toast("You need a fishing rod.", ui::RED);
        audio::sfx(SFX_FAIL);
        return;
    }
    f_spot = node;
    f_state = FS_READY;
    f_timer = 0;
    g.mode = MODE_FISH;
    Actor &p = g.player;
    p.yaw = yawTo(p.pos, n.pos);
    p.setHeld(ITEMS[rod].model);
    p.play("idle", 0.2f);
    audio::sfx(SFX_UI_OK);
}

static u16 rollFish(const NodeDef *d) {
    int lvl = skillLevel(SK_FISHING);
    int tier = toolTier(TOOL_ROD);
    f32 total = 0;
    f32 w[3] = {0, 0, 0};
    for (int i = 0; i < 3; i++) {
        const NodeLoot &l = d->loot[i];
        if (!l.item || lvl < l.level) continue;
        w[i] = (f32)l.chance;
        if (l.item == IT_GOLDEN_CARP) w[i] *= (isNight() || (dayHour() > 17.5f && dayHour() < 20.5f) ? 2.5f : 0.6f) * (1 + tier * 0.25f);
        total += w[i];
    }
    f32 r = frand() * total;
    for (int i = 0; i < 3; i++) {
        if (r < w[i]) return d->loot[i].item;
        r -= w[i];
    }
    return d->loot[0].item;
}

void updateFishing(f32 dt) {
    if (f_spot < 0) {
        g.mode = MODE_PLAY;
        return;
    }
    Node &n = g.nodes[f_spot];
    const NodeDef *d = nodeDef(n.type);
    Actor &p = g.player;
    PadState &pad = g.pad;
    f_timer -= dt;
    switch (f_state) {
        case FS_READY:
            if (pad.pressed & BTN_A) {
                p.play("throw", 0.1f, true);
                f_state = FS_CAST;
                f_timer = 1.0f;
                Vec3 dir = normalize(n.pos - p.pos);
                f32 dist = hvMin(distXZ(n.pos, p.pos), 8.0f);
                f_bobber = p.pos + dir * dist + Vec3(frand() - 0.5f, 0, frand() - 0.5f);
                f_bobber.y = g_world.waterLevel();
            } else if (pad.pressed & BTN_B) {
                g.mode = MODE_PLAY;
                f_spot = -1;
                p.setHeld(g.pd.weapon ? ITEMS[g.pd.weapon].model : nullptr);
                audio::sfx(SFX_UI_BACK);
            }
            break;
        case FS_CAST:
            if (f_timer <= 0) {
                fx::burst(f_bobber, FX_SPLASH, 8);
                audio::sfx(SFX_SPLASH, 0.6f);
                f_state = FS_WAIT;
                f_timer = 2.0f + frand() * 5.0f - toolTier(TOOL_ROD) * 0.5f;
                p.play("idle", 0.3f);
            }
            break;
        case FS_WAIT:
            if (pad.pressed & BTN_A) {
                toast("Too early! The fish swam off.", ui::TEXT_DIM);
                f_state = FS_READY;
                break;
            }
            if (f_timer <= 0) {
                f_state = FS_BITE;
                f_timer = 0.95f;
                fx::burst(f_bobber, FX_SPLASH, 14);
                audio::sfx(SFX_BITE);
                plat::rumble(true);
            }
            break;
        case FS_BITE:
            if (pad.pressed & BTN_A) {
                plat::rumble(false);
                f_state = FS_REEL;
                f_timer = 0.8f;
                p.play("pickup", 0.1f, true, 1.3f);
                f_caught = rollFish(d);
            } else if (f_timer <= 0) {
                plat::rumble(false);
                toast("The fish got away...", ui::TEXT_DIM);
                f_state = FS_READY;
            }
            break;
        case FS_REEL:
            if (f_timer <= 0) {
                bool hq = frand() < 0.08f + toolTier(TOOL_ROD) * 0.03f;
                giveItem(f_caught, 1, hq);
                quests::onGather(f_caught, 1);
                g.pd.gathered++;
                int lvlIdx = 0;
                for (int i = 0; i < 3; i++)
                    if (d->loot[i].item == f_caught) lvlIdx = d->loot[i].level;
                addXp(SK_FISHING, (u32)(d->xp + lvlIdx * 4 + (f_caught == IT_GOLDEN_CARP ? 200 : 0)));
                fx::burst(f_bobber, FX_SPLASH, 20);
                audio::sfx(SFX_SPLASH);
                f_state = FS_RESULT;
                f_timer = 1.6f;
            }
            break;
        case FS_RESULT:
            if (f_timer <= 0 || (pad.pressed & BTN_A)) f_state = FS_READY;
            break;
    }
}

void drawWorld() {
    if (g.mode == MODE_FISH && f_spot >= 0 && f_state != FS_READY && f_state != FS_CAST) {
        f32 bob = sinf(g.time * 3.0f) * 0.05f;
        if (f_state == FS_BITE) bob = -0.15f + sinf(g.time * 30.0f) * 0.08f;
        const Model *m = mdl::get("ip/potion_red");
        gfx::drawModel(m, Mat34::place(f_bobber + Vec3(0, bob - 0.05f, 0), 0, 0.45f));
        water::drawRipple(f_bobber, 0.7f + 0.3f * sinf(g.time * 2), 0.5f);
    }
}

void drawFishingUi() {
    f32 W = ui::width(), H = ui::height();
    f32 x = W * 0.5f;
    const char *msg = nullptr;
    switch (f_state) {
        case FS_READY: msg = "Ready to cast"; break;
        case FS_CAST: msg = "Casting..."; break;
        case FS_WAIT: msg = "Waiting for a bite..."; break;
        case FS_BITE: msg = nullptr; break;
        case FS_REEL: msg = "Reeling in!"; break;
        case FS_RESULT: msg = nullptr; break;
    }
    if (f_state == FS_BITE) {
        f32 s = 1.0f + 0.15f * sinf(g.time * 30);
        ui::text(FONT_BIG, x, H * 0.32f, "!  BITE  !", ui::GOLD, AL_CENTER, s);
        ui::prompt(GL_A, x - 40, H * 0.32f + 50, "Hook!");
    } else if (f_state == FS_RESULT) {
        ui::panel(x - 140, H * 0.25f, 280, 110, ui::PANEL);
        ui::icon(ITEMS[f_caught].model, x, H * 0.25f + 46, 64, 1.2f);
        ui::text(FONT_UI, x, H * 0.25f + 82, ITEMS[f_caught].name, ui::GOLD, AL_CENTER);
    } else if (msg) {
        ui::text(FONT_UI, x, H * 0.7f, msg, ui::WHITE, AL_CENTER);
    }
    f32 py = H - 70;
    f32 px = x - 120;
    if (f_state == FS_READY) {
        px += ui::prompt(GL_A, px, py, "Cast");
        ui::prompt(GL_B, px, py, "Stop fishing");
    }
    char b[48];
    snprintf(b, sizeof(b), "Fishing Lv %d", skillLevel(SK_FISHING));
    ui::text(FONT_SMALL, W - 24, 160, b, ui::TEXT_DIM, AL_RIGHT);
}

}  // namespace gather

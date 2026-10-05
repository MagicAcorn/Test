#include "game/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/pak.h"
#include "game/scene.h"
#include "game/world.h"
#include "gfx/sky.h"
#include "gfx/water.h"
#include "ui/ui.h"

Game g;

// ------------------------------------------------------------- inventory
int Inventory::count(u16 item) const {
    int n = 0;
    for (const InvSlot &s : slots)
        if (s.item == item) n += s.count;
    return n;
}

int Inventory::add(u16 item, int n, bool hq) {
    if (!item || n <= 0) return 0;
    int added = 0;
    const int STACK = 99;
    bool stackable = ITEMS[item].cat != IC_TOOL && ITEMS[item].cat != IC_WEAPON && ITEMS[item].cat != IC_ARMOR;
    if (stackable) {
        for (InvSlot &s : slots) {
            if (s.item == item && s.hq == (hq ? 1 : 0) && s.count < STACK) {
                int k = hvMin(n - added, STACK - (int)s.count);
                s.count = (u8)(s.count + k);
                added += k;
                if (added >= n) return added;
            }
        }
    }
    for (InvSlot &s : slots) {
        if (!s.item) {
            int k = stackable ? hvMin(n - added, STACK) : 1;
            s.item = item;
            s.count = (u8)k;
            s.hq = hq ? 1 : 0;
            added += k;
            if (added >= n) return added;
        }
    }
    return added;
}

bool Inventory::remove(u16 item, int n) {
    if (count(item) < n) return false;
    for (int pass = 0; pass < 2 && n > 0; pass++) {
        // normal quality first, then HQ
        for (InvSlot &s : slots) {
            if (s.item != item || s.hq != pass) continue;
            int k = hvMin(n, (int)s.count);
            s.count = (u8)(s.count - k);
            n -= k;
            if (!s.count) s.item = 0;
            if (!n) break;
        }
    }
    return true;
}

int Inventory::freeSlots() const {
    int n = 0;
    for (const InvSlot &s : slots)
        if (!s.item) n++;
    return n;
}

void Inventory::sort() {
    for (int i = 0; i < SIZE; i++)
        for (int j = i + 1; j < SIZE; j++) {
            const InvSlot &a = slots[i], &b = slots[j];
            bool swap = (!a.item && b.item) || (a.item && b.item && (ITEMS[a.item].cat > ITEMS[b.item].cat ||
                        (ITEMS[a.item].cat == ITEMS[b.item].cat && a.item > b.item)));
            if (swap) {
                InvSlot t = slots[i];
                slots[i] = slots[j];
                slots[j] = t;
            }
        }
}

// ---------------------------------------------------------------- helpers
int skillLevel(int skill) {
    if (skill < 0 || skill >= SK_COUNT) return 1;
    return levelForXp(g.pd.xp[skill]);
}

void toast(const char *text, GXColor c, u16 icon) {
    // repeating the newest message just keeps it on screen longer
    if (g.toasts[0].timer > 0 && strcmp(g.toasts[0].text, text) == 0) {
        g.toasts[0].timer = 3.5f;
        return;
    }
    for (int i = HV_ARRAY_COUNT(g.toasts) - 1; i > 0; i--) g.toasts[i] = g.toasts[i - 1];
    snprintf(g.toasts[0].text, sizeof(g.toasts[0].text), "%s", text);
    g.toasts[0].color = c;
    g.toasts[0].timer = 3.5f;
    g.toasts[0].icon = icon;
}

void showBanner(const char *title, const char *sub, f32 seconds) {
    snprintf(g.banner, sizeof(g.banner), "%s", title);
    snprintf(g.bannerSub, sizeof(g.bannerSub), "%s", sub ? sub : "");
    g.bannerTimer = seconds;
}

int simEventSkill();

void addXp(int skill, u32 amount) {
    if (skill < 0 || skill >= SK_COUNT || !amount) return;
    if (skill == simEventSkill()) amount = amount * 3 / 2;
    int before = skillLevel(skill);
    u32 cap = xpForLevel(MAX_LEVEL);
    g.pd.xp[skill] = hvMin(cap, g.pd.xp[skill] + amount);
    int after = skillLevel(skill);
    char buf[64];
    snprintf(buf, sizeof(buf), "+%u %s XP", amount, SKILLS[skill].name);
    toast(buf, ui::rgba(SKILLS[skill].r, SKILLS[skill].g, SKILLS[skill].b));
    if (after > before) {
        char t[64];
        snprintf(t, sizeof(t), "%s Level %d!", SKILLS[skill].name, after);
        showBanner("Level Up!", t, 4.0f);
        g.levelUpFlash = 1.0f;
        fx::burst(g.player.pos + Vec3(0, 1, 0), FX_LEVEL, 40);
        audio::sfx(SFX_LEVEL);
        if (g.mode == MODE_PLAY) g.player.play("cheer", 0.15f, true);
        sim::onPlayerLevelUp();
        playerRecalcStats();
    }
}

void giveItem(u16 item, int n, bool hq, bool notify) {
    if (!item || n <= 0) return;
    int added = g.pd.inv.add(item, n, hq);
    if (notify) {
        char buf[64];
        if (added < n) snprintf(buf, sizeof(buf), "Inventory full! (%s)", ITEMS[item].name);
        else if (n > 1) snprintf(buf, sizeof(buf), "%s%s x%d", hq ? "HQ " : "", ITEMS[item].name, n);
        else snprintf(buf, sizeof(buf), "%s%s", hq ? "HQ " : "", ITEMS[item].name);
        toast(buf, added < n ? ui::RED : (hq ? ui::GOLD : ui::WHITE), item);
    }
}

const char *lookModel(int look) {
    static const char *const LOOKS[] = {"chr/knight", "chr/barbarian", "chr/mage", "chr/rogue", "chr/rogue_hooded"};
    return LOOKS[hvClamp(look, 0, 4)];
}

int bestTool(int kind) {
    int best = 0, bestTier = 0;
    for (const InvSlot &s : g.pd.inv.slots) {
        if (!s.item) continue;
        const ItemDef &d = ITEMS[s.item];
        if (d.cat == IC_TOOL && d.tool == kind && d.tier > bestTier && skillLevel(d.skill) >= d.level) {
            best = s.item;
            bestTier = d.tier;
        }
    }
    return best;
}

int toolTier(int kind) {
    int t = bestTool(kind);
    return t ? ITEMS[t].tier : 0;
}

void playerRecalcStats() {
    int jl = skillLevel(g.pd.job);
    int def = 0;
    if (g.pd.armor) def += ITEMS[g.pd.armor].power;
    if (g.pd.accessory) def += ITEMS[g.pd.accessory].power;
    int maxHp = 320 + jl * 48 + def * 4;
    g.player.maxHp = maxHp;
    if (g.player.hp > maxHp) g.player.hp = maxHp;
    g.player.maxMp = 200 + skillLevel(SK_MAGE) * 12;
    if (g.player.mp > g.player.maxMp) g.player.mp = g.player.maxMp;
    int gl = hvMax(hvMax(skillLevel(SK_WOODCUTTING), skillLevel(SK_MINING)), hvMax(skillLevel(SK_FISHING), skillLevel(SK_HERBALISM)));
    g.maxGp = 300 + gl * 10;
    if (g.gp > g.maxGp) g.gp = g.maxGp;
    g.player.level = jl;
}

Vec3 screenProject(const Vec3 &p, bool *onScreen) {
    Vec3 v = g.cam.view.point(p);
    f32 w = -v.z;
    if (onScreen) *onScreen = w > 0.5f;
    if (w <= 0.01f) w = 0.01f;
    f32 x = g.cam.proj.m[0][0] * v.x / w;
    f32 y = g.cam.proj.m[1][1] * v.y / w;
    f32 sx = (x * 0.5f + 0.5f) * ui::width();
    f32 sy = (0.5f - y * 0.5f) * ui::height();
    if (onScreen && (sx < -40 || sx > ui::width() + 40 || sy < -40 || sy > ui::height() + 40)) *onScreen = false;
    return Vec3(sx, sy, w);
}

f32 dayHour() { return g.pd.hour; }
bool isNight() { return g.pd.hour < 5.5f || g.pd.hour > 20.0f; }

// -------------------------------------------------------------- camera
static void updateCamera(f32 dt, bool userControl) {
    Actor &p = g.player;
    if (userControl) {
        if (fabsf(g.pad.cx) > 0.05f) g.camYaw -= g.pad.cx * 2.6f * dt;
        if (fabsf(g.pad.cy) > 0.05f) g.camPitch = hvClamp(g.camPitch - g.pad.cy * 1.4f * dt, 0.05f, 1.05f);
        // gentle auto-follow while running and not steering the camera
        static f32 idleCam = 0;
        if (fabsf(g.pad.cx) > 0.05f) idleCam = 0;
        else idleCam += dt;
        f32 spd = p.vel.lenXZ();
        if (idleCam > 1.0f && spd > 2.0f && fabsf(g.pad.sx) < 0.85f) {
            g.camYaw = hvApproachAngle(g.camYaw, p.yaw, dt * 0.9f * hvSaturate((spd - 2.0f) / 4.0f));
        }
    }
    Vec3 tgt = p.pos + Vec3(0, 1.7f, 0);
    f32 cp = cosf(g.camPitch), sp = sinf(g.camPitch);
    Vec3 eye = tgt + Vec3(-sinf(g.camYaw) * cp * g.camDist, sp * g.camDist, -cosf(g.camYaw) * cp * g.camDist);
    // framing shots: over the shoulder toward whoever / whatever the player works with
    Vec3 focus;
    f32 back = 0, side = 0, up = 0, toward = 0.5f, look = 1.0f;
    bool framed = false;
    if (g.mode == MODE_DIALOGUE) {
        int a = npcs::actorForNpc(g.dialogueNpc);
        if (a >= 0) {
            focus = g.actors[a].pos;
            back = 3.3f, side = 1.9f, up = 2.1f, toward = 0.72f, look = 0.85f;
            framed = true;
        }
    } else if (g.mode == MODE_GATHER || g.mode == MODE_FISH) {
        int n = gather::currentNode();
        if (n >= 0) {
            focus = g.nodes[n].pos;
            back = 5.2f, side = 1.6f, up = 2.6f, toward = 0.3f, look = 1.2f;
            framed = true;
        }
    } else if (g.mode == MODE_CRAFT || g.mode == MODE_CRAFT_SELECT) {
        for (int i = 0; i < g.numStations; i++)
            if (g.stations[i].type == g.stationOpen && distXZ(g.stations[i].pos, p.pos) < 8) {
                focus = g.stations[i].pos;
                back = 4.6f, side = 2.6f, up = 4.2f, toward = 0.55f, look = 0.4f;
                framed = true;
            }
    }
    if (framed) {
        Vec3 d = focus - p.pos;
        d.y = 0;
        f32 len = d.lenXZ();
        d = len > 0.01f ? d * (1.0f / len) : Vec3(sinf(p.yaw), 0, cosf(p.yaw));
        Vec3 right(d.z, 0, -d.x);
        // stay on whichever side the camera already is, so it never swings through the player
        f32 sgn = dot(g.cam.eye - p.pos, right) >= 0 ? 1.0f : -1.0f;
        eye = p.pos - d * back + right * (side * sgn) + Vec3(0, up, 0);
        tgt = lerp(p.pos, focus, toward) + Vec3(0, look, 0);
        tgt.y = hvMax(tgt.y, p.pos.y + look);
        g.camYaw = atan2f(tgt.x - eye.x, tgt.z - eye.z);   // resume normal play from this angle
    }
    // pull the camera in front of buildings and trees between it and the player
    {
        Vec3 d = eye - tgt;
        f32 len = d.len();
        f32 keep = len;
        for (f32 t = 1.2f; t < len; t += 0.5f) {
            if (g_world.insideObject(tgt + d * (t / len), 0.35f)) {
                keep = hvMax(1.5f, t - 0.5f);
                break;
            }
        }
        static f32 s_camKeep = 100.0f;   // smooth: snap in fast, ease back out
        if (keep < s_camKeep) s_camKeep = keep;
        else s_camKeep = hvMin(keep, s_camKeep + dt * 6.0f);
        if (s_camKeep < len) eye = tgt + d * (s_camKeep / len);
    }
    f32 gy = g_world.groundHeight(eye.x, eye.z) + 0.8f;
    if (eye.y < gy) eye.y = gy;
    if (eye.y < g_world.waterLevel() + 0.6f) eye.y = g_world.waterLevel() + 0.6f;
    // smooth
    f32 k = hvDamp(14.0f, dt);
    static bool init = false;
    if (!init || dt > 0.2f) {
        g.cam.eye = eye;
        g.cam.target = tgt;
        init = true;
    } else {
        g.cam.eye = lerp(g.cam.eye, eye, k);
        g.cam.target = lerp(g.cam.target, tgt, hvDamp(20.0f, dt));
    }
}

// ------------------------------------------------------- player control
// Pick what A would use: the closest thing, weighted toward what the player
// faces, with people slightly favoured over the stations they stand at.
static f32 interactScore(const Vec3 &target, f32 bias) {
    const Actor &p = g.player;
    Vec3 d = target - p.pos;
    d.y = 0;
    f32 dist = d.lenXZ();
    f32 facing = dist > 0.01f ? (d.x * sinf(p.yaw) + d.z * cosf(p.yaw)) / dist : 1.0f;
    return dist + (1.0f - facing) * 1.6f + bias;
}

static void updateInteraction() {
    Actor &p = g.player;
    g.interact = Game::IK_NONE;
    g.interactIndex = -1;
    f32 best = 1e9f;
    int npc = npcs::nearestTalkable(p.pos, 4.3f);
    if (npc >= 0) {
        g.interact = Game::IK_NPC;
        g.interactIndex = npc;
        best = interactScore(g.actors[npc].pos, -1.0f);
    }
    int node = nodes::nearest(p.pos, 3.8f, false);
    if (node >= 0) {
        f32 d = interactScore(g.nodes[node].pos, 0);
        if (d < best) {
            best = d;
            g.interact = g.nodes[node].fishing ? Game::IK_FISH : Game::IK_NODE;
            g.interactIndex = node;
        }
    }
    for (int i = 0; i < g.numStations; i++) {
        if (distXZ(g.stations[i].pos, p.pos) >= g.stations[i].radius) continue;
        f32 d = interactScore(g.stations[i].pos, 0);
        if (d < best) {
            best = d;
            g.interact = Game::IK_STATION;
            g.interactIndex = i;
        }
    }
}

static void doInteract() {
    switch (g.interact) {
        case Game::IK_NPC: npcs::talk(g.interactIndex); break;
        case Game::IK_NODE: gather::begin(g.interactIndex); break;
        case Game::IK_FISH: gather::beginFishing(g.interactIndex); break;
        case Game::IK_STATION: {
            int st = g.stations[g.interactIndex].type;
            if (st == ST_MARKET) shop::open(st);
            else if (st == ST_NOTICEBOARD) shop::open(st);
            else if (st == ST_HEARTH) quests::onStation(ST_HEARTH);
            else craft::openStation(st);
            break;
        }
        default: break;
    }
}

static void updatePlayer(f32 dt) {
    Actor &p = g.player;
    PadState &pad = g.pad;
    // movement relative to the camera
    Vec3 fwd(sinf(g.camYaw), 0, cosf(g.camYaw));
    Vec3 right(-cosf(g.camYaw), 0, sinf(g.camYaw));
    f32 mag = sqrtf(pad.sx * pad.sx + pad.sy * pad.sy);
    if (mag > 1) mag = 1;
    Vec3 dir = fwd * pad.sy + right * pad.sx;
    bool casting = g.castTimer > 0;
    bool locked = g.actionLock > 0 || p.dead;
    f32 speed = 0;
    if (g.dodgeTimer > 0) {
        g.dodgeTimer -= dt;
        p.vel = g.dodgeDir * 11.0f;
    } else if (mag > 0.12f && !locked) {
        if (casting && mag > 0.4f) {
            g.castTimer = 0;   // moving interrupts a cast
            toast("Cast interrupted", ui::TEXT_DIM);
        }
        speed = mag > 0.7f ? 7.2f : 3.2f * mag / 0.7f + 0.6f;
        if (combat::inCombat()) speed *= 0.92f;
        Vec3 d = normalize(dir);
        p.vel = d * speed;
        p.yaw = hvApproachAngle(p.yaw, atan2f(d.x, d.z), dt * 12.0f);
    } else {
        p.vel = lerp(p.vel, Vec3(), hvDamp(16.0f, dt));
    }
    actors::move(p, p.vel, dt);
    // locomotion animation (unless an action clip is playing)
    bool busy = (p.anim.cur && !p.anim.cur->loop && !p.anim.finished()) || g.actionLock > 0;
    if (!busy && !p.dead) {
        f32 v = p.vel.lenXZ();
        bool armed = combat::inCombat();
        if (v > 4.5f) p.play("run", 0.15f, false, hvClamp(v / 7.2f, 0.8f, 1.2f));
        else if (v > 0.35f) p.play("walk", 0.2f, false, hvClamp(v / 3.0f, 0.7f, 1.4f));
        else p.play(armed ? "idle_2h" : (casting ? "casting" : "idle"), 0.25f);
    }
    if (g.actionLock > 0) g.actionLock -= dt;

    updateInteraction();
    bool hasTarget = g.target >= 0 && g.actors[g.target].active && !g.actors[g.target].dead;
    // A: interact when not fighting a target
    if ((pad.pressed & BTN_A) && !(pad.held & (BTN_L | BTN_R)) && !hasTarget && g.interact != Game::IK_NONE && !locked) doInteract();
    if (pad.pressed & BTN_START) {
        g.mode = MODE_MENU;
        g.menuTab = 0;
        g.menuSel = 0;
        audio::sfx(SFX_UI_OK);
    }
    if ((pad.pressed & BTN_Y) && !(pad.held & (BTN_L | BTN_R)) && !hasTarget) {
        g.mode = MODE_MENU;
        g.menuTab = 0;
        g.menuSel = 0;
        audio::sfx(SFX_UI_OK);
    }
    if (pad.pressed & BTN_UP) g.camDist = hvMax(5.0f, g.camDist - 1.5f);
    if (pad.pressed & BTN_DOWN) g.camDist = hvMin(15.0f, g.camDist + 1.5f);
    combat::playerUpdate(dt);
    // regen
    static f32 regenT = 0;
    regenT += dt;
    if (regenT > 1.0f) {
        regenT = 0;
        bool fighting = combat::inCombat();
        p.hp = hvMin(p.maxHp, p.hp + (fighting ? 2 : p.maxHp / 30));
        p.mp = hvMin(p.maxMp, p.mp + (fighting ? 6 : 20));
        g.gp = hvMin(g.maxGp, g.gp + 8);
    }
}

// ------------------------------------------------------------ regions
static void updateRegion() {
    int best = -1;
    f32 bd = 1e9f;
    for (int i = 0; i < g_world.numMarkers(); i++) {
        const Marker &m = g_world.marker(i);
        if (m.kind != MK_REGION) continue;
        f32 d = distXZ(m.pos, g.player.pos);
        if (d < m.radius && d / m.radius < bd) {
            bd = d / m.radius;
            best = m.id;
        }
    }
    if (best != g.currentRegion && best >= 0) {
        static const char *const NAMES[] = {"", "Emberwick", "Whisperwood", "Copperhill Quarry", "The Old Barrow", "Hollis Farm",
                                            "Mirror Lake", "The Emberrun", "Sunny Meadows", "The Ringwall Peaks"};
        if (best < HV_ARRAY_COUNT(NAMES)) {
            snprintf(g.regionName, sizeof(g.regionName), "%s", NAMES[best]);
            g.regionTimer = 3.5f;
        }
        g.currentRegion = best;
    }
}

// Background music follows the situation: battle > festival > night > place.
static void updateMusic() {
    if (g.mode == MODE_TITLE || g.mode == MODE_CREATE) return;
    int want;
    if (combat::inCombat()) want = MUS_BATTLE;
    else if (g.pd.festival && g.currentRegion == 1) want = MUS_FESTIVAL;
    else if (isNight()) want = MUS_NIGHT;
    else if (g.currentRegion == 1 || g.currentRegion == 5) want = MUS_TOWN;
    else want = MUS_FIELD;
    audio::music(want);
}

// --------------------------------------------------------------- init
static void initStations() {
    g.numStations = 0;
    for (int i = 0; i < g_world.numMarkers() && g.numStations < MAX_STATIONS; i++) {
        const Marker &m = g_world.marker(i);
        if (m.kind != MK_STATION) continue;
        StationInst &s = g.stations[g.numStations++];
        s.type = (u8)m.id;
        s.pos = m.pos;
        s.radius = m.radius;
    }
}

void gameSpawnPlayer() {
    Actor &p = g.player;
    p.active = true;
    p.kind = AK_PLAYER;
    snprintf(p.name, sizeof(p.name), "%s", g.pd.name);
    p.setModel(lookModel(g.pd.look));
    p.radius = 0.55f;
    p.pos = Vec3(g.pd.px, g.pd.py, g.pd.pz);
    p.yaw = g.pd.pyaw;
    p.pos.y = g_world.groundHeight(p.pos.x, p.pos.z);
    p.play("idle", 0);
    p.dead = false;
    playerRecalcStats();
    p.hp = p.maxHp;
    p.mp = p.maxMp;
    g.gp = g.maxGp;
    g.camYaw = p.yaw;
    const ItemDef *w = g.pd.weapon ? &ITEMS[g.pd.weapon] : nullptr;
    p.setHeld(w ? w->model : nullptr, nullptr);
}

// ---------------------------------------------------------------- frame
void titleUpdate(f32 dt);
void titleDraw();
void createUpdate(f32 dt);
void createDraw();
void cutsceneUpdate(f32 dt);
void cutsceneDraw();
bool cutsceneActive();
void createDraw3D();

#ifdef HV_PC
// Per-section GPU load report for the PC harness (HV_PROFILE=<frame>).
namespace {
struct ProfSec {
    const char *name;
    GXEmuStats st;
};
ProfSec s_prof[24];
int s_profN = 0;
GXEmuStats s_profPrev;
int s_profFrame = -2;
void profBegin() {
    if (s_profFrame == -2) s_profFrame = getenv("HV_PROFILE") ? atoi(getenv("HV_PROFILE")) : -1;
    tex::takeLoadCount();
    s_profN = 0;
    gxemu_get_stats(&s_profPrev);
}
void profMark(const char *name) {
    if (s_profN >= 24) return;
    GXEmuStats now;
    gxemu_get_stats(&now);
    ProfSec &p = s_prof[s_profN++];
    p.name = name;
    p.st.verts = now.verts - s_profPrev.verts;
    p.st.tris_in = now.tris_in - s_profPrev.tris_in;
    p.st.prims = now.prims - s_profPrev.prims;
    p.st.dl_calls = now.dl_calls - s_profPrev.dl_calls;
    p.st.mtx_loads = now.mtx_loads - s_profPrev.mtx_loads;
    s_profPrev = now;
}
void profReport() {
    if (s_profFrame < 0 || (int)g.frame != s_profFrame) return;
    GXEmuStats t = {};
    printf("[prof] frame %u  %-14s %8s %8s %6s %6s %6s\n", g.frame, "section", "verts", "tris", "prims", "dls", "mtx");
    for (int i = 0; i < s_profN; i++) {
        const GXEmuStats &s = s_prof[i].st;
        printf("[prof]   %-20s %8u %8u %6u %6u %6u\n", s_prof[i].name, s.verts, s.tris_in, s.prims, s.dl_calls, s.mtx_loads);
        t.verts += s.verts;
        t.tris_in += s.tris_in;
        t.prims += s.prims;
        t.dl_calls += s.dl_calls;
        t.mtx_loads += s.mtx_loads;
    }
    printf("[prof]   %-20s %8u %8u %6u %6u %6u\n", "TOTAL", t.verts, t.tris_in, t.prims, t.dl_calls, t.mtx_loads);
    const gfx::Stats &gs = gfx::stats();
    printf("[prof]   models %u skinned %u batches %u culled %u shade changes %u tex loads %u\n", gs.models, gs.skinned, gs.batches,
           gs.culled, gs.shadeChanges, tex::takeLoadCount());
}
}  // namespace
#define PROF_BEGIN() profBegin()
#define PROF(n) profMark(n)
#define PROF_REPORT() profReport()
#else
#define PROF_BEGIN()
#define PROF(n)
#define PROF_REPORT()
#endif

static void drawWorld() {
    PROF_BEGIN();
    scene::drawSkyAndTerrain(g.time);
    PROF("sky+terrain");
    scene::drawObjects();
    PROF("objects");
    nodes::draw();
    PROF("nodes");
    // shadows before characters
    if (g.mode != MODE_TITLE) actors::drawShadow(g.player);
    for (Actor &a : g.actors)
        if (a.active && distXZ(a.pos, g.cam.eye) < 70) actors::drawShadow(a);
    PROF("shadows");
    if (g.mode != MODE_TITLE && g.mode != MODE_CREATE) actors::draw(g.player);
    createDraw3D();
    PROF("player");
    npcs::draw();
    PROF("npcs");
    for (Actor &a : g.actors)
        if (a.active && a.kind == AK_ADVENTURER && distXZ(a.pos, g.cam.eye) < 70) actors::draw(a);
    PROF("adventurers");
    combat::draw();
    PROF("enemies");
    gather::drawWorld();
    sky::drawClouds();
    PROF("clouds");
    scene::drawWaterAndGrass(g.time);
    PROF("water+grass");
    fx::draw();
    scene::drawLightGlows(g.time);
    PROF("fx+glows");
}

static void drawUi() {
    PROF("(world end)");
    ui::begin();
    ui::setTime(g.time);
    switch (g.mode) {
        case MODE_TITLE: titleDraw(); break;
        case MODE_CREATE: createDraw(); break;
        case MODE_CUTSCENE: cutsceneDraw(); break;
        default:
            fx::drawFloatTexts();
            hud::draw();
            break;
    }
    if (g.mode == MODE_DIALOGUE) npcs::drawDialogueUi();
    if (g.mode == MODE_GATHER) gather::drawUi();
    if (g.mode == MODE_FISH) gather::drawFishingUi();
    if (g.mode == MODE_CRAFT_SELECT) craft::drawSelectUi();
    if (g.mode == MODE_CRAFT) craft::drawUi();
    if (g.mode == MODE_SHOP || g.mode == MODE_BOARD) shop::drawUi();
    if (g.mode == MODE_MENU) hud::drawMenu();
    // fade from black
    if (g.fadeIn > 0) ui::rect(0, 0, ui::width(), ui::height(), ui::rgba(0, 0, 0, (u8)(hvSaturate(g.fadeIn) * 255)));
    ui::end();
    PROF("ui");
    PROF_REPORT();
}

#ifdef HV_PC
void scenarioTick();
#endif

void gameFrame() {
    plat::pollInput(g.pad);
#ifdef HV_PC
    scenarioTick();
#endif
    f32 dt = plat::frameDelta();
    g.dt = dt;
    g.time += dt;
    g.frame++;
    if (g.fadeIn > 0) g.fadeIn -= dt * 1.2f;

    bool worldPaused = (g.mode == MODE_MENU);
    if (!worldPaused && g.mode != MODE_TITLE && g.mode != MODE_CREATE) {
        g.pd.hour += dt / 60.0f;   // one in-game hour per real minute
        if (g.pd.hour >= 24.0f) {
            g.pd.hour -= 24.0f;
            g.pd.day++;
            shop::newDay();
        }
        g.pd.playSeconds = (u32)(g.pd.playSeconds + 0) + (g.frame % 60 == 0 ? 1 : 0);
    }
    sky::setTime(g.mode == MODE_TITLE ? 18.6f : (g.mode == MODE_CREATE ? 9.5f : g.pd.hour));
    sky::update(dt);

    switch (g.mode) {
        case MODE_TITLE: titleUpdate(dt); break;
        case MODE_CREATE: createUpdate(dt); break;
        case MODE_PLAY: updatePlayer(dt); break;
        case MODE_DIALOGUE: npcs::updateDialogue(dt); break;
        case MODE_GATHER: gather::update(dt); break;
        case MODE_FISH: gather::updateFishing(dt); break;
        case MODE_CRAFT_SELECT: craft::updateSelect(dt); break;
        case MODE_CRAFT: craft::update(dt); break;
        case MODE_SHOP:
        case MODE_BOARD: shop::update(dt); break;
        case MODE_MENU: hud::updateMenu(dt); break;
        case MODE_CUTSCENE: cutsceneUpdate(dt); break;
        default: break;
    }
    if (g.mode != MODE_TITLE && g.mode != MODE_CREATE) {
        if (g.mode != MODE_PLAY && g.mode != MODE_MENU) {
            // keep the player animated but still
            g.player.vel = Vec3();
        }
        if (!worldPaused) {
            nodes::update(dt);
            npcs::update(dt);
            sim::update(dt);
            combat::update(dt);
            fx::update(dt);
        }
        actors::update(g.player, dt, true);
        if (g.mode != MODE_CUTSCENE) updateCamera(dt, g.mode == MODE_PLAY || g.mode == MODE_DEAD);
        updateRegion();
        updateMusic();
    }
    for (auto &t : g.toasts)
        if (t.timer > 0) t.timer -= dt;
    if (g.bannerTimer > 0) g.bannerTimer -= dt;
    if (g.regionTimer > 0) g.regionTimer -= dt;
    if (g.levelUpFlash > 0) g.levelUpFlash -= dt;
    if (g.saveMsgTimer > 0) g.saveMsgTimer -= dt;
    audio::update(dt);

    // ---- render
    g.cam.update(ui::aspect());
    GX_SetCopyClear(gfx::env().fogColor, 0x00FFFFFF);
    gfx::beginScene(g.cam);
#ifdef HV_PC
    // HV_FAST: the harness skips drawing frames nobody looks at (long bot runs)
    static bool fast = getenv("HV_FAST") != nullptr;
    if (!fast || plat::wantsFrameRendered()) {
        drawWorld();
        drawUi();
    }
#else
    drawWorld();
    drawUi();
#endif
    plat::endFrame();
}

void gameInitWorld() {
    scene::init();
    ui::init();
    ui::setWidescreen(plat::widescreen());
    actors::init();
    initStations();
    nodes::init();
    npcs::init();
    combat::init();
    sim::init();
    fx::init();
    quests::init();
    audio::init();
}

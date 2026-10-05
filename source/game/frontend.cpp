// Title screen, character creation and the Hearth cutscene.
#include <stdio.h>
#include <stdlib.h>
#include "game/game.h"
#include "game/world.h"
#include "gfx/sky.h"
#include "ui/ui.h"

void gameSpawnPlayer();

namespace {
int s_titleSel = 0;
bool s_hasSave = false;
bool s_saveChecked = false;
f32 s_titleT = 0;

// character creation
int s_row = 0;
int s_look = 0, s_nameIdx = 0, s_job = SK_WARRIOR;
Actor s_preview;
const char *const NAMES[] = {"Rowan", "Ember", "Kestrel", "Wren", "Aldric", "Isla", "Bramble", "Corin", "Marigold", "Thane", "Juniper", "Faye"};
const int NNAMES = HV_ARRAY_COUNT(NAMES);

// cutscene
f32 s_cutT = 0;
bool s_lit = false;
}  // namespace

static const Vec3 HEARTH(200, 6, 196);

void titleUpdate(f32 dt) {
    s_titleT += dt;
    if (!s_saveChecked) {
        s_hasSave = plat::saveExists();
        s_saveChecked = true;
        s_titleSel = s_hasSave ? 0 : 1;
        audio::music(MUS_TITLE);
    }
    // slow orbit over Emberwick at dusk
    f32 a = s_titleT * 0.05f + 2.2f;
    g.cam.eye = Vec3(200 + cosf(a) * 70, 38, 196 + sinf(a) * 70);
    g.cam.target = Vec3(200, 8, 196);
    nodes::update(dt);
    npcs::update(dt);
    sim::update(dt);
    fx::update(dt);
    PadState &pad = g.pad;
    if (pad.pressed & (BTN_UP | BTN_DOWN)) {
        if (s_hasSave) s_titleSel ^= 1;
        audio::sfx(SFX_UI_MOVE);
    }
    if (pad.pressed & (BTN_A | BTN_START)) {
        audio::sfx(SFX_UI_OK);
        if (s_titleSel == 0 && s_hasSave && save::read()) {
            gameSpawnPlayer();
            quests::init();
            fx::hearthFire(g.pd.hearthLit != 0);
            g.mode = MODE_PLAY;
            g.fadeIn = 1.0f;
            showBanner("Welcome back", g.pd.name, 3.0f);
            audio::music(MUS_TOWN);
        } else {
            g.mode = MODE_CREATE;
            s_row = 0;
            s_preview = Actor();
            s_preview.active = true;
            s_preview.setModel(lookModel(s_look));
            s_preview.play("idle", 0);
            g.fadeIn = 0.6f;
        }
    }
}

void titleDraw() {
    f32 W = ui::width(), H = ui::height();
    ui::rectGradient(0, 0, W, 170, ui::rgba(0, 0, 0, 140), ui::rgba(0, 0, 0, 0));
    f32 glow = 0.85f + 0.15f * sinf(s_titleT * 1.5f);
    ui::text(FONT_TITLE, W * 0.5f, 52, "HEARTHVALE", ui::rgba(255, (u8)(200 * glow + 30), 120), AL_CENTER);
    ui::text(FONT_UI, W * 0.5f, 128, "~  a tale of the vale  ~", ui::rgba(255, 240, 220, 220), AL_CENTER);
    f32 y = H * 0.62f;
    const char *opts[2] = {"Continue", "New Game"};
    for (int i = 0; i < 2; i++) {
        bool enabled = i == 1 || s_hasSave;
        bool sel = i == s_titleSel;
        f32 w = 220;
        ui::panel(W * 0.5f - w * 0.5f, y + i * 50, w, 40, sel ? ui::SEL : ui::rgba(30, 26, 40, 200), 14);
        ui::text(FONT_UI, W * 0.5f, y + i * 50 + 9, opts[i], (sel || enabled) ? ui::WHITE : ui::TEXT_DIM, AL_CENTER);
    }
    ui::text(FONT_SMALL, W * 0.5f, H - 34, "Press START", ui::rgba(255, 255, 255, (u8)(160 + 90 * sinf(s_titleT * 3))), AL_CENTER);
    ui::text(FONT_SMALL, 16, H - 34, "Art: KayKit by Kay Lousberg (CC0)", ui::rgba(255, 255, 255, 120));
}

void createUpdate(f32 dt) {
    PadState &pad = g.pad;
    // preview stage in front of the hearth
    Vec3 stage(196, 0, 220);   // a quiet corner of the square, clear of NPCs
    stage.y = g_world.groundHeight(stage.x, stage.z);
    s_preview.pos = stage;
    s_preview.yaw += dt * 0.6f;
    actors::update(s_preview, dt, true);
    g.cam.eye = stage + Vec3(-1.0f, 1.7f, 4.0f);
    g.cam.target = stage + Vec3(-1.25f, 1.15f, 0);
    if (pad.pressed & BTN_UP) { s_row = (s_row + 3) % 4; audio::sfx(SFX_UI_MOVE); }
    if (pad.pressed & BTN_DOWN) { s_row = (s_row + 1) % 4; audio::sfx(SFX_UI_MOVE); }
    int d = (pad.pressed & BTN_RIGHT) ? 1 : ((pad.pressed & BTN_LEFT) ? -1 : 0);
    if (d) {
        audio::sfx(SFX_UI_MOVE);
        if (s_row == 0) {
            s_look = (s_look + d + 5) % 5;
            s_preview.setModel(lookModel(s_look));
            s_preview.play("cheer", 0.1f, true);
        } else if (s_row == 1) {
            s_nameIdx = (s_nameIdx + d + NNAMES) % NNAMES;
        } else if (s_row == 2) {
            s_job = s_job == SK_WARRIOR ? SK_MAGE : SK_WARRIOR;
            s_preview.setHeld(s_job == SK_MAGE ? "itm/staff" : "itm/sword");
            s_preview.play(s_job == SK_MAGE ? "cast" : "slash", 0.1f, true);
        }
    }
    if (s_preview.anim.finished()) s_preview.play("idle", 0.3f);
    if (pad.pressed & BTN_X) {
        s_nameIdx = (int)(g.time * 997) % NNAMES;
        audio::sfx(SFX_UI_MOVE);
    }
    if (pad.pressed & BTN_B) {
        g.mode = MODE_TITLE;
        audio::sfx(SFX_UI_BACK);
    }
    if ((pad.pressed & (BTN_A | BTN_START)) && (s_row == 3 || (pad.pressed & BTN_START))) {
        save::newGame(NAMES[s_nameIdx], s_look, s_job);
        gameSpawnPlayer();
        quests::init();
        fx::hearthFire(false);
        g.mode = MODE_PLAY;
        g.fadeIn = 1.5f;
        showBanner("Emberwick", "Speak with Elder Maren by the cold Hearth", 5.0f);
        toast("Tip: walk with the Control Stick, talk with A", ui::GOLD);
        audio::sfx(SFX_QUEST);
        audio::music(MUS_TOWN);
    } else if ((pad.pressed & BTN_A) && s_row < 3) {
        s_row++;
        audio::sfx(SFX_UI_OK);
    }
}

void createDraw() {
    f32 W = ui::width(), H = ui::height();
    // draw the preview character over the scene (it is rendered in 3D by createDraw3D)
    ui::text(FONT_BIG, 40, 24, "Create your adventurer", ui::GOLD, AL_LEFT, 0.85f);
    static const char *const LOOKS[] = {"Knight", "Ranger", "Mage", "Rogue", "Wanderer"};
    const char *labels[4] = {"Appearance", "Name", "Starting job", ""};
    char vals[4][48];
    snprintf(vals[0], 48, "%s", LOOKS[s_look]);
    snprintf(vals[1], 48, "%s", NAMES[s_nameIdx]);
    snprintf(vals[2], 48, "%s", s_job == SK_MAGE ? "Mage" : "Warrior");
    snprintf(vals[3], 48, "Begin Adventure");
    f32 x = 40, y = 110;
    for (int r = 0; r < 4; r++) {
        bool sel = r == s_row;
        ui::panel(x, y + r * 62, 280, 52, sel ? ui::SEL : ui::rgba(30, 26, 40, 210), 14);
        GXColor tc = ui::WHITE;
        if (r < 3) {
            ui::text(FONT_SMALL, x + 16, y + r * 62 + 6, labels[r], sel ? ui::rgba(255, 236, 200) : ui::TEXT_DIM);
            ui::text(FONT_UI, x + 140, y + r * 62 + 22, vals[r], tc, AL_CENTER);
            ui::text(FONT_UI, x + 22, y + r * 62 + 22, "<", tc);
            ui::text(FONT_UI, x + 258, y + r * 62 + 22, ">", tc);
        } else {
            ui::text(FONT_UI, x + 140, y + r * 62 + 14, vals[r], tc, AL_CENTER);
        }
    }
    const char *jobDesc = s_job == SK_MAGE ? "Mages hurl fire and ice from range, heal themselves and blink away from danger."
                                           : "Warriors chain combos up close, shrug off blows with Rampart and recover with Second Wind.";
    ui::panel(x, y + 260, 280, 76, ui::rgba(30, 26, 40, 200), 12);
    ui::textWrap(FONT_SMALL, x + 12, y + 268, 256, jobDesc, ui::TEXT_DIM);
    f32 px = 40;
    f32 py = H - 40;
    px += ui::prompt(GL_DPAD, px, py, "Choose");
    px += ui::prompt(GL_A, px, py, "Next");
    px += ui::prompt(GL_X, px, py, "Random name");
    ui::prompt(GL_B, px, py, "Back");
    (void)W;
}

void createDraw3D() {
    if (g.mode == MODE_CREATE) {
        actors::drawShadow(s_preview);
        actors::draw(s_preview);
    }
}

void startHearthCutscene() {
    g.mode = MODE_CUTSCENE;
    s_cutT = 0;
    s_lit = false;
    g.player.vel = Vec3();
    g.player.play("interact", 0.2f, true);
}

bool cutsceneActive() { return g.mode == MODE_CUTSCENE; }

void cutsceneUpdate(f32 dt) {
    s_cutT += dt;
    f32 a = s_cutT * 0.35f + 1.0f;
    f32 r = 14 - s_cutT * 0.5f;
    g.cam.eye = HEARTH + Vec3(cosf(a) * r, 5 + s_cutT * 0.4f, sinf(a) * r);
    g.cam.target = HEARTH + Vec3(0, 2.5f, 0);
    if (!s_lit && s_cutT > 2.2f) {
        s_lit = true;
        g.pd.hearthLit = 1;
        fx::hearthFire(true);
        fx::burst(HEARTH + Vec3(0, 2.8f, 0), FX_FIRE, 60);
        fx::burst(HEARTH + Vec3(0, 3.0f, 0), FX_GOLD, 60);
        fx::burst(HEARTH + Vec3(0, 3.0f, 0), FX_SPARK, 40);
        showBanner("The Great Hearth burns again!", "Warmth returns to Emberwick", 5.0f);
        audio::sfx(SFX_FIRE);
        audio::sfx(SFX_LEVEL);
        for (int i = 0; i < MAX_ACTORS; i++) {
            Actor &n = g.actors[i];
            if (n.active && (n.kind == AK_NPC || n.kind == AK_ADVENTURER) && distXZ(n.pos, HEARTH) < 60) n.play("cheer", 0.2f, true);
        }
        g.player.play("cheer", 0.2f, true);
    }
    if (s_cutT > 2.2f && s_cutT < 6.0f && (g.frame % 4) == 0) fx::burst(HEARTH + Vec3(0, 3.5f, 0), FX_SPARK, 2);
    if (s_cutT > 8.0f || (s_cutT > 3.0f && (g.pad.pressed & BTN_A))) {
        g.mode = MODE_PLAY;
        toast("Return to Elder Maren.", ui::GOLD);
    }
}

void cutsceneDraw() {
    f32 W = ui::width(), H = ui::height();
    ui::rect(0, 0, W, 44, ui::rgba(0, 0, 0, 255));
    ui::rect(0, H - 44, W, 44, ui::rgba(0, 0, 0, 255));
    fx::drawFloatTexts();
    hud::draw();
}

void gameReturnToTitle() {
    g.mode = MODE_TITLE;
    s_saveChecked = false;
    g.fadeIn = 1.0f;
    g.target = -1;
}

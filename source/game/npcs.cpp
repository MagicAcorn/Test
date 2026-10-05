// Villagers, dialogue and quest hand-off.
#include <stdio.h>
#include "game/game.h"
#include "game/world.h"
#include "gfx/texture.h"
#include "ui/ui.h"

namespace npcs {

namespace {
struct NpcHome {
    int actor;
    Vec3 home;
    f32 yaw;
    f32 chatCooldown;
};
NpcHome s_npcs[32];
int s_num = 0;
u32 s_rng = 0x51ED27u;
f32 frand() {
    s_rng = s_rng * 1103515245u + 12345u;
    return ((s_rng >> 8) & 0xFFFF) / 65536.0f;
}

int allocActor() {
    for (int i = 0; i < MAX_ACTORS; i++)
        if (!g.actors[i].active) return i;
    return -1;
}
}  // namespace

int actorForNpc(int npcId) {
    for (int i = 0; i < s_num; i++)
        if (g.actors[s_npcs[i].actor].npcId == npcId) return s_npcs[i].actor;
    return -1;
}

void init() {
    s_num = 0;
    for (int i = 0; i < g_world.numMarkers() && s_num < 32; i++) {
        const Marker &m = g_world.marker(i);
        if (m.kind != MK_NPC) continue;
        const NpcDef *d = npcDef(m.id);
        if (!d) continue;
        int ai = allocActor();
        if (ai < 0) break;
        Actor &a = g.actors[ai];
        a = Actor();
        a.active = true;
        a.kind = AK_NPC;
        a.npcId = d->id;
        snprintf(a.name, sizeof(a.name), "%s", d->name);
        a.title = d->title;
        a.setModel(d->model);
        a.setHeld(d->held);
        a.pos = m.pos;
        a.pos.y = g_world.groundHeight(m.pos.x, m.pos.z);
        a.yaw = m.yaw;
        a.radius = 0.6f;
        if (d->id == NPC_CHILD) a.scale = 0.72f;
        a.play(d->idleAnim, 0);
        a.anim.t = frand() * 2.0f;
        NpcHome &h = s_npcs[s_num++];
        h.actor = ai;
        h.home = a.pos;
        h.yaw = a.yaw;
        h.chatCooldown = 5 + frand() * 20;
    }
    hvLog("npcs: %d villagers", s_num);
}

void update(f32 dt) {
    const Vec3 &pp = g.player.pos;
    for (int i = 0; i < s_num; i++) {
        NpcHome &h = s_npcs[i];
        Actor &a = g.actors[h.actor];
        const NpcDef *d = npcDef(a.npcId);
        f32 dist = distXZ(a.pos, pp);
        f32 want = h.yaw;
        if (dist < 7.0f && g.mode != MODE_TITLE) want = yawTo(a.pos, pp);
        if (g.mode == MODE_DIALOGUE && g.dialogueNpc == a.npcId) want = yawTo(a.pos, pp);
        a.yaw = hvApproachAngle(a.yaw, want, dt * 3.0f);
        if (a.anim.finished()) a.play(d->idleAnim, 0.3f);
        // ambient chatter when the player is around
        h.chatCooldown -= dt;
        if (h.chatCooldown <= 0) {
            h.chatCooldown = 18 + frand() * 25;
            if (dist < 16 && g.mode == MODE_PLAY) a.say(d->smallTalk[(int)(frand() * 3) % 3], 4.5f);
        }
        bool near = distXZ(a.pos, g.cam.eye) < 70;
        actors::update(a, dt, near);
    }
}

void draw() {
    for (int i = 0; i < s_num; i++) {
        Actor &a = g.actors[s_npcs[i].actor];
        if (distXZ(a.pos, g.cam.eye) > 70) continue;
        actors::draw(a);
    }
}

int nearestTalkable(const Vec3 &p, f32 maxDist) {
    int best = -1;
    f32 bd = maxDist;
    for (int i = 0; i < s_num; i++) {
        Actor &a = g.actors[s_npcs[i].actor];
        f32 d = distXZ(a.pos, p);
        if (d < bd) {
            bd = d;
            best = s_npcs[i].actor;
        }
    }
    return best;
}

static void openDialogue(int npcId, int kind, int quest, const char *const *lines, int maxLines) {
    g.mode = MODE_DIALOGUE;
    g.dialogueNpc = npcId;
    g.dialogueKind = kind;
    g.dialogueQuest = quest;
    g.dialogueLine = 0;
    g.dialogueChars = 0;
    g.dialogueCount = 0;
    for (int i = 0; i < maxLines && i < 8; i++) {
        if (!lines[i]) break;
        g.dialogueLines[g.dialogueCount++] = lines[i];
    }
    if (!g.dialogueCount) {
        static const char *const dots = "...";
        g.dialogueLines[0] = dots;
        g.dialogueCount = 1;
    }
    g.player.vel = Vec3();
    g.player.play("idle", 0.25f);
}

void talk(int ai) {
    Actor &a = g.actors[ai];
    const NpcDef *d = npcDef(a.npcId);
    if (!d) return;
    g.player.yaw = yawTo(g.player.pos, a.pos);
    audio::sfx(SFX_UI_OK);
    // 1. a quest step completes here
    int q = quests::stepNpcWaiting(a.npcId);
    if (q >= 0) {
        const QuestDef &qd = QUESTS[q];
        const QuestStep &st = qd.steps[g.pd.quests[q].step];
        openDialogue(a.npcId, 2, q, st.lines, 4);
        return;
    }
    // 2. a quest is offered
    q = quests::availableFrom(a.npcId);
    if (q >= 0) {
        openDialogue(a.npcId, 1, q, QUESTS[q].offer, 4);
        return;
    }
    // 3. merchant opens the shop after the greeting
    if (a.npcId == NPC_MERCHANT) {
        shop::open(ST_MARKET);
        return;
    }
    // 4. greeting / small talk
    static const char *lines[2];
    lines[0] = d->greeting;
    lines[1] = d->smallTalk[(int)(frand() * 3) % 3];
    openDialogue(a.npcId, 0, -1, lines, 2);
    quests::onTalk(a.npcId);
}

void updateDialogue(f32 dt) {
    PadState &pad = g.pad;
    const char *line = g.dialogueLines[g.dialogueLine];
    int len = (int)strlen(line);
    g.dialogueChars += dt * 60.0f;
    bool lastLine = g.dialogueLine >= g.dialogueCount - 1;
    if (pad.pressed & (BTN_A | BTN_B)) {
        if (g.dialogueChars < len) {
            g.dialogueChars = (f32)len;   // skip typing
            return;
        }
        if (!lastLine) {
            if (pad.pressed & BTN_A) {
                g.dialogueLine++;
                g.dialogueChars = 0;
                audio::sfx(SFX_UI_MOVE);
            } else {
                g.mode = MODE_PLAY;
            }
            return;
        }
        // last line
        if (g.dialogueKind == 1) {
            if (pad.pressed & BTN_A) quests::accept(g.dialogueQuest);
            else audio::sfx(SFX_UI_BACK);
        } else if (g.dialogueKind == 2) {
            int q = g.dialogueQuest;
            g.mode = MODE_PLAY;
            quests::advance(q);
            if (g.mode == MODE_DIALOGUE) return;   // advance may chain into another dialogue/cutscene
        }
        if (g.mode == MODE_DIALOGUE) g.mode = MODE_PLAY;
    }
}

void drawDialogueUi() {
    f32 W = ui::width(), H = ui::height();
    const NpcDef *d = npcDef(g.dialogueNpc);
    f32 x = 40, w = W - 80, h = 128, y = H - h - 26;
    ui::panel(x, y, w, h, ui::rgba(28, 24, 36, 235));
    // name plate
    if (d) {
        f32 nw = ui::textWidth(FONT_UI, d->name) + 30;
        ui::panel(x + 18, y - 18, nw, 30, ui::rgba(120, 76, 40, 245), 10);
        ui::text(FONT_UI, x + 33, y - 15, d->name, ui::GOLD);
        if (d->title) ui::text(FONT_SMALL, x + 30 + nw, y - 11, d->title, ui::TEXT_DIM);
    }
    const char *line = g.dialogueLines[g.dialogueLine];
    ui::textWrap(FONT_UI, x + 22, y + 22, w - 44, line, ui::WHITE, 1.0f, (int)g.dialogueChars);
    bool typed = g.dialogueChars >= strlen(line);
    bool lastLine = g.dialogueLine >= g.dialogueCount - 1;
    if (typed) {
        f32 py = y + h - 32;
        if (lastLine && g.dialogueKind == 1) {
            f32 px = x + w - 260;
            px += ui::prompt(GL_A, px, py, "Accept");
            ui::prompt(GL_B, px, py, "Not now");
            ui::text(FONT_SMALL, x + 22, py + 4, QUESTS[g.dialogueQuest].title, ui::GOLD);
        } else {
            f32 bob = sinf(g.time * 6) * 2;
            ui::glyph(GL_A, x + w - 44, py + bob, 24);
        }
    }
}

}  // namespace npcs

// Memory card save/load of PlayerData.
#include <stdio.h>
#include "game/game.h"
#include "game/world.h"
#include "ui/ui.h"

namespace save {

static u32 checksum(const PlayerData &pd) {
    const u8 *p = (const u8 *)&pd;
    u32 h = 0x811C9DC5u;
    for (size_t i = 0; i < offsetof(PlayerData, checksum); i++) {
        h ^= p[i];
        h *= 0x01000193u;
    }
    return h;
}

bool write() {
    if (g.mode == MODE_TITLE || g.mode == MODE_CREATE) return false;
    g.pd.px = g.player.pos.x;
    g.pd.py = g.player.pos.y;
    g.pd.pz = g.player.pos.z;
    g.pd.pyaw = g.player.yaw;
    g.pd.version = SAVE_VERSION;
    g.pd.checksum = checksum(g.pd);
    bool ok = plat::saveWrite(&g.pd, sizeof(PlayerData));
    snprintf(g.saveMsg, sizeof(g.saveMsg), ok ? "Game saved." : "Save failed (memory card in slot A?)");
    g.saveMsgTimer = 3.0f;
    if (ok) audio::sfx(SFX_UI_OK);
    return ok;
}

bool read() {
    static PlayerData tmp;
    s32 n = plat::saveRead(&tmp, sizeof(PlayerData));
    if (n < (s32)sizeof(PlayerData)) return false;
    if (tmp.version != SAVE_VERSION || tmp.checksum != checksum(tmp)) return false;
    g.pd = tmp;
    return true;
}

void newGame(const char *name, int look, int job) {
    memset(&g.pd, 0, sizeof(g.pd));
    g.pd.version = SAVE_VERSION;
    snprintf(g.pd.name, sizeof(g.pd.name), "%s", name);
    g.pd.look = (u8)look;
    g.pd.job = (u8)job;
    g.pd.coins = 40;
    g.pd.hour = 8.0f;
    g.pd.day = 0;
    for (int i = 0; i < MAX_QUESTS; i++) g.pd.quests[i].status = QST_LOCKED;
    g.pd.inv.clear();
    g.pd.inv.add(IT_GRILLED_TROUT, 3);
    g.pd.inv.add(IT_CLOTH, 2);
    // start in the plaza
    for (int i = 0; i < g_world.numMarkers(); i++) {
        const Marker &m = g_world.marker(i);
        if (m.kind == MK_PLAYER) {
            g.pd.px = m.pos.x;
            g.pd.pz = m.pos.z;
            g.pd.pyaw = m.yaw;
        }
    }
    g.pd.py = g_world.groundHeight(g.pd.px, g.pd.pz);
}

}  // namespace save

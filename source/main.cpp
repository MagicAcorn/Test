// Hearthvale - entry point (shared by GameCube and the PC test harness).
#include <stdlib.h>
#include "core/pak.h"
#include "game/game.h"
#include "game/world.h"
#include "gfx/sky.h"
#include "gfx/texture.h"

void gameFrame();
void gameInitWorld();
void gameSpawnPlayer();

int main(int argc, char **argv) {
    plat::setArgs(argc, argv);
    plat::init();
    u32 pakSize;
    const u8 *pak = plat::assetData(&pakSize);
    if (!g_pak.init(pak, pakSize)) {
        hvLog("failed to open asset pak");
        return 1;
    }
    tex::initAll();
    gfx::init();
    gfx::setEnvironment(Environment());
    if (!g_world.load("world/vale")) {
        hvLog("failed to load world");
        return 1;
    }
    gameInitWorld();
    g.mode = MODE_TITLE;
#ifdef HV_PC
    // test harness shortcuts
    if (getenv("HV_AUTOSTART")) {
        save::newGame("Rowan", atoi(getenv("HV_AUTOSTART")), getenv("HV_MAGE") ? SK_MAGE : SK_WARRIOR);
        if (getenv("HV_HOUR")) g.pd.hour = (f32)atof(getenv("HV_HOUR"));
        gameSpawnPlayer();
        quests::init();
        g.mode = MODE_PLAY;
        g.fadeIn = 0;
    }
#endif
    while (!plat::quitRequested()) gameFrame();
#ifdef HV_PC
    GXEmuStats st;
    gxemu_get_stats(&st);
    hvLog("gxemu: prims %u tris %u drawn %u errors %u", st.prims, st.tris_in, st.tris_drawn, st.errors);
#endif
    plat::shutdown();
    return 0;
}

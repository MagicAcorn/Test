// Hearthvale - entry point (shared by GameCube and the PC test harness).
#include "core/pak.h"
#include "gfx/anim.h"
#include "gfx/renderer.h"
#include "platform/platform.h"

static void drawGround() {
    gfx::setShade(SH_VCOL | SH_LIT);
    gfx::loadWorld(Mat34::identity());
    const int N = 16;
    const f32 S = 6.0f;
    gfx::beginImm(GX_QUADS, N * N * 4, false, true);
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            f32 x0 = (x - N / 2) * S, z0 = (z - N / 2) * S;
            u8 g = ((x + z) & 1) ? 150 : 140;
            // clockwise when seen from above (GX front faces are clockwise)
            f32 xs[4] = {x0, x0 + S, x0 + S, x0};
            f32 zs[4] = {z0, z0, z0 + S, z0 + S};
            for (int k = 0; k < 4; k++) {
                GX_Position3f32(xs[k], 0, zs[k]);
                GX_Normal3f32(0, 1, 0);
                GX_Color4u8(96, g, 70, 255);
            }
        }
    GX_End();
}

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

    const Skeleton *skel = anim::skeleton(hvHash("skel/humanoid"));
    const Model *knight = mdl::get("chr/knight");
    const Model *mage = mdl::get("chr/mage");
    const Model *skw = mdl::get("chr/sk_warrior");
    const Model *house = mdl::get("hx/home_a");
    const Model *tavern = mdl::get("hx/tavern");
    const Model *tree = mdl::get("hx/tree_single_a");
    const Model *pine = mdl::get("hw/tree_pine_yellow_large");
    const Model *barrel = mdl::get("dn/barrel_large");
    hvLog("skel %p knight %p house %p tree %p", (const void *)skel, (const void *)knight, (const void *)house, (const void *)tree);

    Animator an[3];
    an[0].play(anim::clip("anim/idle"), 0);
    an[1].play(anim::clip("anim/walk"), 0);
    an[2].play(anim::clip("anim/sk_idle"), 0);

    Camera cam;
    f32 t = 0;
    while (!plat::quitRequested()) {
        PadState pad;
        plat::pollInput(pad);
        f32 dt = plat::frameDelta();
        t += dt;
        for (auto &a : an) a.update(dt);

        f32 ang = 0.6f + pad.cx * 0.5f;
        cam.eye = Vec3(sinf(ang) * 9.0f, 4.0f, cosf(ang) * 9.0f);
        cam.target = Vec3(0, 1.4f, 0);
        cam.update((f32)plat::screenW() / plat::screenH());

        GX_SetCopyClear(gfx::env().skyHorizon, 0x00FFFFFF);
        gfx::beginScene(cam);
        drawGround();
        gfx::drawModel(house, Mat34::place(Vec3(-9, 0, -10), 0.4f, 1.0f));
        gfx::drawModel(tavern, Mat34::place(Vec3(8, 0, -14), -0.3f, 1.0f));
        gfx::drawModel(tree, Mat34::place(Vec3(-6, 0, 3), 0.0f, 1.0f));
        gfx::drawModel(pine, Mat34::place(Vec3(7, 0, 2), 0.0f, 1.0f));
        gfx::drawModel(barrel, Mat34::place(Vec3(2.5f, 0, -2), 0.0f, 0.6f));

        const Model *chars[3] = {knight, mage, skw};
        Vec3 cp[3] = {Vec3(0, 0, 0), Vec3(-2.6f, 0, -1.0f), Vec3(2.6f, 0, -1.2f)};
        for (int i = 0; i < 3; i++) {
            if (!chars[i] || !skel) continue;
            Pose pose;
            an[i].evaluate(skel, pose);
            Mat34 jm[MAX_JOINTS];
            anim::toModel(skel, pose, jm);
            static Mat34 dm[200];
            anim::drawMatrices(chars[i], skel, jm, dm);
            gfx::drawSkinned(chars[i], Mat34::place(cp[i], 0.2f - i * 0.3f, 1.0f), dm, gxc(255, 255, 255), SH_RIM);
        }
        plat::endFrame();
    }
#ifdef HV_PC
    GXEmuStats st;
    gxemu_get_stats(&st);
    hvLog("gxemu: prims %u tris %u drawn %u errors %u", st.prims, st.tris_in, st.tris_drawn, st.errors);
#endif
    plat::shutdown();
    return 0;
}

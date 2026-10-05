#include "game/actor.h"
#include "game/game.h"
#include <stdio.h>
#include "game/world.h"
#include "gfx/renderer.h"
#include "gfx/texture.h"

namespace {
const Skeleton *g_humanoid;
const Texture *g_shadowTex;
u32 g_handR, g_handL, g_head;
}  // namespace

void Actor::setModel(const char *modelName) {
    model = mdl::get(modelName);
    if (!model) {
        hvLog("actor: missing model %s", modelName);
        return;
    }
    if (model->flags & MF_SKINNED) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s#skel", modelName);
        skel = anim::skeleton(hvHash(buf));
        if (!skel && model->skelHash) skel = anim::skeleton(model->skelHash);
        if (!skel) skel = g_humanoid;
        if (dmCap < model->numDrawMtx) {
            delete[] dm;
            dmCap = model->numDrawMtx;
            dm = new Mat34[dmCap];
        }
        jHandR = skel->findJoint(g_handR);
        jHandL = skel->findJoint(g_handL);
        jHead = skel->findJoint(g_head);
    } else {
        skel = nullptr;
    }
    poseValid = false;
}

void Actor::setHeld(const char *right, const char *left) {
    heldR = right ? mdl::get(right) : nullptr;
    heldL = left ? mdl::get(left) : nullptr;
}

void Actor::play(const char *clip, f32 fade, bool restart, f32 speed) {
    char buf[48];
    snprintf(buf, sizeof(buf), "anim/%s", clip);
    const AnimClip *c = anim::clip(buf);
    if (c) anim.play(c, fade, restart, speed);
}

bool Actor::isPlaying(const char *clip) const {
    char buf[48];
    snprintf(buf, sizeof(buf), "anim/%s", clip);
    return anim.cur && anim.cur->hash == hvHash(buf);
}

void Actor::say(const char *text, f32 seconds) {
    snprintf(chat, sizeof(chat), "%s", text);
    chatTimer = seconds;
}

Vec3 Actor::headPos() const {
    if (skel && jHead >= 0 && poseValid) return world.point(jm[jHead].point(Vec3(0, 0.45f, 0)));
    return pos + Vec3(0, (model ? model->bmax.y : 2.0f) * scale + 0.2f, 0);
}

Vec3 Actor::handPos() const {
    if (skel && jHandR >= 0 && poseValid) return world.point(jm[jHandR].pos());
    return pos + Vec3(0, 1.2f * scale, 0);
}

namespace actors {

void init() {
    g_humanoid = anim::skeleton(hvHash("skel/humanoid"));
    g_shadowTex = tex::get("tx/shadow");
    g_handR = hvHash("handslot.r");
    g_handL = hvHash("handslot.l");
    g_head = hvHash("head");
}

void update(Actor &a, f32 dt, bool nearCamera) {
    a.anim.update(dt);
    if (a.hitFlash > 0) a.hitFlash = hvMax(0.0f, a.hitFlash - dt * 3.0f);
    if (a.chatTimer > 0) a.chatTimer -= dt;
    a.squash += dt;
    // world matrix
    if (a.skel) {
        a.world = Mat34::place(a.pos, a.yaw, a.scale);
        if (nearCamera) {
            Pose pose;
            a.anim.evaluate(a.skel, pose);
            anim::toModel(a.skel, pose, a.jm);
            anim::drawMatrices(a.model, a.skel, a.jm, a.dm);
            a.poseValid = true;
        }
    } else {
        // procedural squash & stretch for slimes / props
        f32 hop = a.dead ? 0 : fabsf(sinf(a.squash * 5.0f)) * hvMin(1.0f, a.vel.lenXZ() * 0.5f + 0.15f);
        f32 sy = 1.0f + hop * 0.25f - (a.dead ? hvMin(a.deadTimer * 2.0f, 0.85f) : 0);
        f32 sxz = 1.0f / sqrtf(hvMax(sy, 0.15f));
        Mat34 base = Mat34::place(a.pos + Vec3(0, hop * 0.35f, 0), a.yaw, a.scale);
        a.world = base * Mat34::scale(Vec3(sxz, sy, sxz));
    }
}

void draw(Actor &a) {
    if (!a.model || !a.active) return;
    GXColor tint = a.tint;
    if (a.hitFlash > 0) {
        f32 f = hvSaturate(a.hitFlash);
        tint = gxlerp(tint, gxc(255, 120, 110), f);
    }
    if (a.skel) {
        if (!a.poseValid) return;
        gfx::drawSkinned(a.model, a.world, a.dm, tint, SH_RIM);
        if (a.heldR && a.jHandR >= 0) gfx::drawModel(a.heldR, a.world * a.jm[a.jHandR], gxc(255, 255, 255), SH_RIM);
        if (a.heldL && a.jHandL >= 0) gfx::drawModel(a.heldL, a.world * a.jm[a.jHandL], gxc(255, 255, 255), SH_RIM);
    } else {
        gfx::drawModel(a.model, a.world, tint, SH_RIM);
    }
}

void drawShadow(const Actor &a) {
    if (!g_shadowTex || !a.active) return;
    f32 r = a.radius * 1.5f * a.scale;
    if (a.dead && !a.skel) return;
    gfx::setShade(SH_TEX | SH_VCOL | SH_BLEND | SH_DOUBLESIDED);
    tex::bind(g_shadowTex, GX_TEXMAP0);
    gfx::loadWorld(Mat34::identity());
    f32 gx[4] = {a.pos.x - r, a.pos.x + r, a.pos.x + r, a.pos.x - r};
    f32 gz[4] = {a.pos.z - r, a.pos.z - r, a.pos.z + r, a.pos.z + r};
    f32 uu[4] = {0, 1, 1, 0}, vv[4] = {0, 0, 1, 1};
    gfx::beginImm(GX_QUADS, 4, true);
    for (int k = 0; k < 4; k++) {
        f32 y = g_world.groundHeight(gx[k], gz[k], a.pos.y + 0.5f) + 0.06f;
        GX_Position3f32(gx[k], y, gz[k]);
        GX_Color4u8(0, 0, 0, 150);
        GX_TexCoord2f32(uu[k], vv[k]);
    }
    GX_End();
}

void move(Actor &a, const Vec3 &desired, f32 dt) {
    Vec3 p = a.pos;
    Vec3 np = p + desired * dt;
    // try full move, then axis-separated slides
    if (!g_world.walkable(np.x, np.z, p.y)) {
        Vec3 nx(np.x, p.y, p.z), nz(p.x, p.y, np.z);
        if (g_world.walkable(nx.x, nx.z, p.y)) np = nx;
        else if (g_world.walkable(nz.x, nz.z, p.y)) np = nz;
        else np = p;
    }
    g_world.resolveCircle(np, a.radius * a.scale);
    f32 g = g_world.groundHeight(np.x, np.z, p.y + 0.5f);
    if (!a.grounded) {
        // airborne: ballistic until we land
        a.vy -= 27.0f * dt;
        np.y = p.y + a.vy * dt;
        if (np.y <= g && a.vy <= 0) {
            np.y = g;
            a.vy = 0;
            a.grounded = true;
            if (a.kind == AK_PLAYER) {
                a.play("jump_land", 0.05f, true, 1.8f);
                audio::sfx(SFX_STEP, 0.9f, 0.8f);
            }
        }
        a.pos = np;
        return;
    }
    // gravity / step
    if (g >= np.y - 0.05f || a.grounded) {
        if (g - p.y > 1.6f) {
            np.x = p.x;
            np.z = p.z;
            g = g_world.groundHeight(p.x, p.z, p.y + 0.5f);
        }
        np.y = g;
        a.vy = 0;
        a.grounded = true;
    }
    a.pos = np;
}

}  // namespace actors

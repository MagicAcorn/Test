#include "game/world.h"
#include "core/pak.h"

World g_world;

static bool isFoliageName(const char *n) {
    return strncmp(n, "pr/oak", 6) == 0 || strncmp(n, "pr/birch", 8) == 0 || strncmp(n, "pr/autumn", 9) == 0 ||
           strncmp(n, "pr/pine", 7) == 0 || strncmp(n, "pr/willow", 9) == 0 || strncmp(n, "pr/darkoak", 10) == 0 ||
           strncmp(n, "pr/blossom", 10) == 0;
}

bool World::load(const char *asset) {
    const u8 *d = g_pak.find(asset, nullptr, ASSET_WORLD);
    if (!d || memcmp(d, "WRL1", 4) != 0) return false;
    m_n = rdU16(d + 4);
    m_cell = rdF32(d + 8);
    m_hscale = rdF32(d + 12);
    m_water = rdF32(d + 16);
    m_heights = d + rdU32(d + 20);
    m_colors = d + rdU32(d + 24);
    m_numObjects = (int)rdU32(d + 28);
    const u8 *objs = d + rdU32(d + 32);
    m_numMarkers = (int)rdU32(d + 36);
    const u8 *mks = d + rdU32(d + 40);
    m_numBridges = hvMin<int>((int)rdU32(d + 44), 16);
    const u8 *brs = d + rdU32(d + 48);
    m_numRiver = (int)rdU32(d + 52);
    const u8 *riv = d + rdU32(d + 56);

    m_objects = new WorldObject[m_numObjects];
    for (int i = 0; i < m_numObjects; i++) {
        const u8 *o = objs + i * 36;
        WorldObject &w = m_objects[i];
        u32 mh = rdU32(o);
        w.model = mdl::get(mh);
        w.pos = Vec3(rdF32(o + 4), rdF32(o + 8), rdF32(o + 12));
        w.yaw = rdF32(o + 16);
        w.scale = rdF32(o + 20);
        w.colType = o[24];
        w.flags = o[25];
        w.ca = rdF32(o + 28);
        w.cb = rdF32(o + 32);
        w.xf = Mat34::place(w.pos, w.yaw, w.scale);
        w.cosY = cosf(w.yaw);
        w.sinY = sinf(w.yaw);
        if (w.model) {
            w.cullCenter = w.xf.point(w.model->center);
            w.cullRadius = w.model->radius * w.scale;
        } else {
            w.cullCenter = w.pos;
            w.cullRadius = 1;
            hvLog("world: missing model %s", g_pak.nameOf(mh));
        }
        const char *nm = g_pak.nameOf(mh);
        w.foliage = isFoliageName(nm);
        w.small = w.cullRadius < 2.5f;
    }
    m_markers = new Marker[m_numMarkers];
    for (int i = 0; i < m_numMarkers; i++) {
        const u8 *m = mks + i * 28;
        Marker &k = m_markers[i];
        k.kind = rdU16(m);
        k.id = rdU16(m + 2);
        k.pos = Vec3(rdF32(m + 4), rdF32(m + 8), rdF32(m + 12));
        k.yaw = rdF32(m + 16);
        k.radius = rdF32(m + 20);
        k.extra = rdU32(m + 24);
    }
    for (int i = 0; i < m_numBridges; i++) {
        const u8 *b = brs + i * 24;
        Bridge &br = m_bridges[i];
        br.x = rdF32(b); br.z = rdF32(b + 4); br.halfLen = rdF32(b + 8); br.halfWidth = rdF32(b + 12);
        br.y = rdF32(b + 16); br.arch = rdF32(b + 20);
    }
    m_river = new Vec2[m_numRiver];
    for (int i = 0; i < m_numRiver; i++) m_river[i] = Vec2(rdF32(riv + i * 8), rdF32(riv + i * 8 + 4));

    // spatial grid (objects with collision or any object for culling)
    f32 gc = gridCell();
    int counts[GRID * GRID];
    memset(counts, 0, sizeof(counts));
    auto cellRange = [&](const WorldObject &w, int &x0, int &z0, int &x1, int &z1) {
        f32 r = hvMax(w.cullRadius, w.colType == 2 ? hvMax(w.ca, w.cb) * 1.42f : w.ca);
        x0 = hvClamp((int)((w.pos.x - r) / gc), 0, GRID - 1);
        x1 = hvClamp((int)((w.pos.x + r) / gc), 0, GRID - 1);
        z0 = hvClamp((int)((w.pos.z - r) / gc), 0, GRID - 1);
        z1 = hvClamp((int)((w.pos.z + r) / gc), 0, GRID - 1);
    };
    int total = 0;
    for (int i = 0; i < m_numObjects; i++) {
        int x0, z0, x1, z1;
        cellRange(m_objects[i], x0, z0, x1, z1);
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) {
                counts[z * GRID + x]++;
                total++;
            }
    }
    m_gridData = new u16[total + 1];
    u32 acc = 0;
    for (int c = 0; c < GRID * GRID; c++) {
        m_gridStart[c] = acc;
        acc += counts[c];
    }
    m_gridStart[GRID * GRID] = acc;
    int fill[GRID * GRID];
    memset(fill, 0, sizeof(fill));
    for (int i = 0; i < m_numObjects; i++) {
        int x0, z0, x1, z1;
        cellRange(m_objects[i], x0, z0, x1, z1);
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) {
                int c = z * GRID + x;
                m_gridData[m_gridStart[c] + fill[c]++] = (u16)i;
            }
    }
    hvLog("world: %d objects, %d markers, grid %d entries", m_numObjects, m_numMarkers, total);
    return true;
}

const u16 *World::gridItems(int gx, int gz, int *count) const {
    if (gx < 0 || gz < 0 || gx >= GRID || gz >= GRID) {
        *count = 0;
        return nullptr;
    }
    int c = gz * GRID + gx;
    *count = (int)(m_gridStart[c + 1] - m_gridStart[c]);
    return m_gridData + m_gridStart[c];
}

f32 World::vertexHeight(int ix, int iz) const {
    ix = hvClamp(ix, 0, m_n - 1);
    iz = hvClamp(iz, 0, m_n - 1);
    return rdU16(m_heights + ((size_t)iz * m_n + ix) * 2) * m_hscale;
}

f32 World::terrainHeight(f32 x, f32 z) const {
    f32 fx = x / m_cell, fz = z / m_cell;
    int ix = hvClamp((int)floorf(fx), 0, m_n - 2);
    int iz = hvClamp((int)floorf(fz), 0, m_n - 2);
    f32 tx = hvClamp(fx - ix, 0.0f, 1.0f), tz = hvClamp(fz - iz, 0.0f, 1.0f);
    f32 h00 = vertexHeight(ix, iz), h10 = vertexHeight(ix + 1, iz);
    f32 h01 = vertexHeight(ix, iz + 1), h11 = vertexHeight(ix + 1, iz + 1);
    // match the triangle split used by the terrain mesh (diagonal 00-11)
    if (tx >= tz) return h00 + (h10 - h00) * tx + (h11 - h10) * tz;
    return h00 + (h11 - h01) * tx + (h01 - h00) * tz;
}

Vec3 World::terrainNormal(f32 x, f32 z) const {
    f32 e = m_cell;
    f32 dx = terrainHeight(x + e, z) - terrainHeight(x - e, z);
    f32 dz = terrainHeight(x, z + e) - terrainHeight(x, z - e);
    return normalize(Vec3(-dx, 2 * e, -dz));
}

f32 World::groundHeight(f32 x, f32 z, f32 refY) const {
    f32 h = terrainHeight(x, z);
    for (int i = 0; i < m_numBridges; i++) {
        const Bridge &b = m_bridges[i];
        f32 dx = x - b.x, dz = z - b.z;
        if (fabsf(dx) <= b.halfLen && fabsf(dz) <= b.halfWidth) {
            f32 t = dx / b.halfLen;
            f32 y = b.y + b.arch * (1.0f - t * t);
            // only stand on the bridge if we are not far below it (e.g. swimming under)
            if (refY > y - 1.5f) h = hvMax(h, y);
        }
    }
    return h;
}

// True if p lies inside the collision volume of a solid object (box or
// cylinder footprint up to the model's top), grown by `pad`.
bool World::insideObject(const Vec3 &p, f32 pad) const {
    for (int i = 0; i < m_numBridges; i++) {
        // bridge decks are solid slabs for the camera
        const Bridge &b = m_bridges[i];
        f32 dx = p.x - b.x, dz = p.z - b.z;
        if (fabsf(dx) > b.halfLen || fabsf(dz) > b.halfWidth) continue;
        f32 t = dx / b.halfLen;
        f32 y = b.y + b.arch * (1.0f - t * t);
        if (p.y < y + 0.1f && p.y > y - 0.6f - pad) return true;
    }
    f32 gc = gridCell();
    int gx = (int)(p.x / gc), gz = (int)(p.z / gc);
    for (int cz = gz - 1; cz <= gz + 1; cz++)
        for (int cx = gx - 1; cx <= gx + 1; cx++) {
            int n;
            const u16 *items = gridItems(cx, cz, &n);
            for (int k = 0; k < n; k++) {
                const WorldObject &o = m_objects[items[k]];
                if (!o.colType || !o.model) continue;
                f32 top = o.pos.y + o.model->bmax.y * o.scale;
                if (p.y > top + pad || p.y < o.pos.y - 1.0f) continue;
                f32 dx = p.x - o.pos.x, dz = p.z - o.pos.z;
                if (o.colType == 1) {
                    f32 r = o.ca + pad;
                    if (dx * dx + dz * dz < r * r) return true;
                } else {
                    f32 lx = dx * o.cosY - dz * o.sinY;
                    f32 lz = dx * o.sinY + dz * o.cosY;
                    if (fabsf(lx) < o.ca + pad && fabsf(lz) < o.cb + pad) return true;
                    // above head height, roofs overhang the walls: use the model's own extent
                    if (p.y > o.pos.y + 2.2f) {
                        f32 ex = hvMax(fabsf(o.model->bmin.x), fabsf(o.model->bmax.x)) * o.scale;
                        f32 ez = hvMax(fabsf(o.model->bmin.z), fabsf(o.model->bmax.z)) * o.scale;
                        f32 r = hvMax(ex, ez);
                        if (r > 2.5f && dx * dx + dz * dz < r * r) {
                            f32 ux = fabsf(lx), uz = fabsf(lz);
                            if ((ux < ex + pad && uz < ez + pad) || (ux < ez + pad && uz < ex + pad)) return true;
                        }
                    }
                }
            }
        }
    return false;
}

bool World::resolveCircle(Vec3 &p, f32 radius) const {
    bool hit = false;
    f32 gc = gridCell();
    int gx = (int)(p.x / gc), gz = (int)(p.z / gc);
    for (int cz = gz - 1; cz <= gz + 1; cz++)
        for (int cx = gx - 1; cx <= gx + 1; cx++) {
            int n;
            const u16 *items = gridItems(cx, cz, &n);
            for (int k = 0; k < n; k++) {
                const WorldObject &o = m_objects[items[k]];
                if (o.colType == 1) {
                    f32 r = o.ca + radius;
                    f32 dx = p.x - o.pos.x, dz = p.z - o.pos.z;
                    f32 d2 = dx * dx + dz * dz;
                    if (d2 < r * r && d2 > 1e-6f) {
                        f32 d = sqrtf(d2);
                        p.x = o.pos.x + dx / d * r;
                        p.z = o.pos.z + dz / d * r;
                        hit = true;
                    }
                } else if (o.colType == 2) {
                    // into box local space (yaw rotation), half extents scaled
                    f32 dx = p.x - o.pos.x, dz = p.z - o.pos.z;
                    f32 lx = dx * o.cosY - dz * o.sinY;
                    f32 lz = dx * o.sinY + dz * o.cosY;
                    f32 hx = o.ca, hz = o.cb;
                    f32 cx2 = hvClamp(lx, -hx, hx), cz2 = hvClamp(lz, -hz, hz);
                    f32 ex = lx - cx2, ez = lz - cz2;
                    f32 d2 = ex * ex + ez * ez;
                    if (d2 < radius * radius) {
                        if (d2 > 1e-8f) {
                            f32 d = sqrtf(d2);
                            lx = cx2 + ex / d * radius;
                            lz = cz2 + ez / d * radius;
                        } else {
                            // inside: push out along the shallowest axis
                            f32 px = hx - fabsf(lx), pz = hz - fabsf(lz);
                            if (px < pz) lx = (lx < 0 ? -1 : 1) * (hx + radius);
                            else lz = (lz < 0 ? -1 : 1) * (hz + radius);
                        }
                        p.x = o.pos.x + lx * o.cosY + lz * o.sinY;
                        p.z = o.pos.z - lx * o.sinY + lz * o.cosY;
                        hit = true;
                    }
                }
            }
        }
    // world bounds
    f32 lim = size() - 6.0f;
    if (p.x < 6) { p.x = 6; hit = true; }
    if (p.z < 6) { p.z = 6; hit = true; }
    if (p.x > lim) { p.x = lim; hit = true; }
    if (p.z > lim) { p.z = lim; hit = true; }
    return hit;
}

bool World::walkable(f32 x, f32 z, f32 fromY) const {
    // railings: from a bridge deck you can only leave by its ends
    for (int i = 0; i < m_numBridges; i++) {
        const Bridge &br = m_bridges[i];
        f32 dx = x - br.x, dz = z - br.z;
        if (fabsf(dx) > br.halfLen - 0.3f || fabsf(dz) <= br.halfWidth) continue;
        if (fabsf(dz) > br.halfWidth + 1.5f) continue;
        f32 t = dx / br.halfLen;
        f32 deck = br.y + br.arch * (1.0f - t * t);
        if (fabsf(fromY - deck) < 0.5f && deck - terrainHeight(x, z) > 0.8f) return false;
    }
    f32 g = groundHeight(x, z, fromY);
    if (g < m_water - 1.1f) return false;            // deep water
    if (g - fromY > 1.6f) return false;              // cliff up
    if (g > terrainHeight(x, z) + 0.05f) return true;  // on a bridge or dock deck: the bank below doesn't matter
    Vec3 n = terrainNormal(x, z);
    if (n.y < 0.62f && g > fromY + 0.15f) return false;   // too steep to climb
    return true;
}

bool World::lineOfSight(const Vec3 &a, const Vec3 &b) const {
    Vec3 d = b - a;
    f32 len = d.len();
    int steps = (int)(len / 2.0f) + 1;
    for (int i = 1; i < steps; i++) {
        Vec3 p = a + d * ((f32)i / steps);
        if (terrainHeight(p.x, p.z) > p.y) return false;
    }
    return true;
}

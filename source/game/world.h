// The Vale: terrain heightfield, placed objects, markers and collision.
#pragma once
#include "core/hmath.h"
#include "gfx/model.h"

enum MarkerKind { MK_NODE = 1, MK_SPAWN, MK_NPC, MK_REGION, MK_LIGHT, MK_STATION, MK_PLAYER, MK_FISH, MK_POI };

struct WorldObject {
    const Model *model;
    Mat34 xf;
    Vec3 pos;
    f32 yaw, scale;
    f32 cullRadius;
    Vec3 cullCenter;
    u8 colType;      // 0 none, 1 circle, 2 box
    u8 flags;
    f32 ca, cb;      // circle radius | box half extents (local x, z)
    f32 cosY, sinY;
    bool foliage, small;
};

struct Marker {
    u16 kind, id;
    Vec3 pos;
    f32 yaw, radius;
    u32 extra;
};

struct Bridge {
    f32 x, z, halfLen, halfWidth, y, arch;
};

class World {
public:
    bool load(const char *asset);

    // terrain
    int gridN() const { return m_n; }
    f32 cell() const { return m_cell; }
    f32 size() const { return (m_n - 1) * m_cell; }
    f32 waterLevel() const { return m_water; }
    f32 terrainHeight(f32 x, f32 z) const;
    f32 vertexHeight(int ix, int iz) const;
    const u8 *vertexColor(int ix, int iz) const { return m_colors + ((size_t)iz * m_n + ix) * 4; }
    Vec3 terrainNormal(f32 x, f32 z) const;
    // walkable ground: terrain, bridges and docks
    f32 groundHeight(f32 x, f32 z, f32 refY = 1e9f) const;
    bool isWater(f32 x, f32 z) const { return groundHeight(x, z) < m_water - 0.15f; }
    f32 waterDepth(f32 x, f32 z) const { return m_water - groundHeight(x, z); }

    // collision: pushes a circle out of objects; returns true if blocked
    bool resolveCircle(Vec3 &p, f32 radius) const;
    bool insideObject(const Vec3 &p, f32 pad) const;
    bool walkable(f32 x, f32 z, f32 fromY) const;
    bool lineOfSight(const Vec3 &a, const Vec3 &b) const;

    // objects / markers
    int numObjects() const { return m_numObjects; }
    const WorldObject &object(int i) const { return m_objects[i]; }
    int numMarkers() const { return m_numMarkers; }
    const Marker &marker(int i) const { return m_markers[i]; }
    int numBridges() const { return m_numBridges; }
    const Bridge &bridge(int i) const { return m_bridges[i]; }
    int numRiver() const { return m_numRiver; }
    Vec2 riverPoint(int i) const { return m_river[i]; }

    // spatial grid of objects (for collision + culling)
    static const int GRID = 24;
    f32 gridCell() const { return size() / GRID; }
    const u16 *gridItems(int gx, int gz, int *count) const;

private:
    int m_n = 0;
    f32 m_cell = 2, m_hscale = 1, m_water = 3;
    const u8 *m_heights = nullptr;
    const u8 *m_colors = nullptr;
    WorldObject *m_objects = nullptr;
    int m_numObjects = 0;
    Marker *m_markers = nullptr;
    int m_numMarkers = 0;
    Bridge m_bridges[16];
    int m_numBridges = 0;
    Vec2 *m_river = nullptr;
    int m_numRiver = 0;
    u16 *m_gridData = nullptr;
    u32 m_gridStart[GRID * GRID + 1];
};

extern World g_world;

// Read-only asset archive (HVPK) produced by tools/build_assets.py.
#pragma once
#include "core/types.h"

#define HV_FOURCC(a, b, c, d) (((u32)(a) << 24) | ((u32)(b) << 16) | ((u32)(c) << 8) | (u32)(d))

enum AssetType : u32 {
    ASSET_TEX = HV_FOURCC('T', 'E', 'X', ' '),
    ASSET_MDL = HV_FOURCC('M', 'D', 'L', ' '),
    ASSET_SKEL = HV_FOURCC('S', 'K', 'E', 'L'),
    ASSET_ANIM = HV_FOURCC('A', 'N', 'I', 'M'),
    ASSET_WORLD = HV_FOURCC('W', 'R', 'L', 'D'),
    ASSET_FONT = HV_FOURCC('F', 'O', 'N', 'T'),
    ASSET_DATA = HV_FOURCC('D', 'A', 'T', 'A'),
    ASSET_SOUND = HV_FOURCC('S', 'N', 'D', ' '),
};

class Pak {
public:
    bool init(const u8 *data, u32 size);
    // Returns asset bytes (32-byte aligned) or null.
    const u8 *find(u32 hash, u32 *size = nullptr, u32 type = 0) const;
    const u8 *find(const char *name, u32 *size = nullptr, u32 type = 0) const { return find(hvHash(name), size, type); }
    const char *nameOf(u32 hash) const;
    u32 count() const { return m_count; }
    // iterate entries: returns false when i out of range
    bool entry(u32 i, u32 *hash, u32 *type, const u8 **data, u32 *size) const;

private:
    const u8 *m_base = nullptr;
    u32 m_size = 0;
    u32 m_count = 0;
    const u8 *m_dir = nullptr;
    const u8 *m_names = nullptr;
};

extern Pak g_pak;

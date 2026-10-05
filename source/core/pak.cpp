#include "core/pak.h"

Pak g_pak;

bool Pak::init(const u8 *data, u32 size) {
    if (size < 32 || memcmp(data, "HVPK", 4) != 0) return false;
    if (((uintptr_t)data) & 31) {
        hvLog("pak: data not 32-byte aligned!");
        return false;
    }
    m_base = data;
    m_size = size;
    m_count = rdU32(data + 8);
    m_dir = data + rdU32(data + 12);
    m_names = data + rdU32(data + 16);
    return true;
}

const u8 *Pak::find(u32 hash, u32 *size, u32 type) const {
    int lo = 0, hi = (int)m_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        const u8 *e = m_dir + mid * 16;
        u32 h = rdU32(e);
        if (h == hash) {
            if (type && rdU32(e + 4) != type) return nullptr;
            if (size) *size = rdU32(e + 12);
            return m_base + rdU32(e + 8);
        }
        if (h < hash) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

const char *Pak::nameOf(u32 hash) const {
    int lo = 0, hi = (int)m_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        u32 h = rdU32(m_dir + mid * 16);
        if (h == hash) return (const char *)(m_names + m_count * 4 + rdU32(m_names + mid * 4));
        if (h < hash) lo = mid + 1;
        else hi = mid - 1;
    }
    return "?";
}

bool Pak::entry(u32 i, u32 *hash, u32 *type, const u8 **data, u32 *size) const {
    if (i >= m_count) return false;
    const u8 *e = m_dir + i * 16;
    if (hash) *hash = rdU32(e);
    if (type) *type = rdU32(e + 4);
    if (data) *data = m_base + rdU32(e + 8);
    if (size) *size = rdU32(e + 12);
    return true;
}

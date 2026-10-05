// Basic types shared by all Hearthvale code.
#pragma once

#include "platform/gx.h"   // u8..f32, Mtx, GX API (real or emulated)

#include <stddef.h>
#include <string.h>

#define HV_ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define HV_ALIGN32 __attribute__((aligned(32)))

template <typename T>
inline T hvMin(T a, T b) { return a < b ? a : b; }
template <typename T>
inline T hvMax(T a, T b) { return a > b ? a : b; }
template <typename T>
inline T hvClamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// 32-bit FNV-1a of a lowercase name; must match tools/meshbuild.py fnv1a()
inline u32 hvHash(const char *s) {
    u32 h = 0x811C9DC5u;
    while (*s) {
        u8 c = (u8)*s++;
        if (c >= 'A' && c <= 'Z') c = (u8)(c - 'A' + 'a');
        h ^= c;
        h *= 0x01000193u;
    }
    return h;
}

// compile-time variant for constants
constexpr u32 hvHashC(const char *s, u32 h = 0x811C9DC5u) {
    return *s ? hvHashC(s + 1, (h ^ (u32)(u8)((*s >= 'A' && *s <= 'Z') ? (*s - 'A' + 'a') : *s)) * 0x01000193u) : h;
}

// Big-endian accessors for asset data (CPU side). GX itself reads asset
// memory directly, which is big-endian on the GameCube and emulated as such
// on PC.
inline u16 rdU16(const void *p) { const u8 *b = (const u8 *)p; return (u16)((b[0] << 8) | b[1]); }
inline s16 rdS16(const void *p) { return (s16)rdU16(p); }
inline u32 rdU32(const void *p) {
    const u8 *b = (const u8 *)p;
    return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | b[3];
}
inline f32 rdF32(const void *p) {
    u32 v = rdU32(p);
    f32 f;
    memcpy(&f, &v, 4);
    return f;
}
inline void wrU16(void *p, u16 v) { u8 *b = (u8 *)p; b[0] = (u8)(v >> 8); b[1] = (u8)v; }
inline void wrU32(void *p, u32 v) { u8 *b = (u8 *)p; b[0] = (u8)(v >> 24); b[1] = (u8)(v >> 16); b[2] = (u8)(v >> 8); b[3] = (u8)v; }
inline void wrF32(void *p, f32 f) { u32 v; memcpy(&v, &f, 4); wrU32(p, v); }

// memory for GX consumption (32-byte aligned)
void *hvAlignedAlloc(size_t size);
void hvAlignedFree(void *p);

void hvLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

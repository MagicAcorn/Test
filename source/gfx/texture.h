#pragma once
#include "core/types.h"

struct Texture {
    GXTexObj obj;
    u32 hash;
    u16 w, h;
    u8 fmt, levels;
};

namespace tex {
void initAll();                 // creates GXTexObjs for every texture asset in the pak
const Texture *get(u32 hash);   // null if missing
inline const Texture *get(const char *name) { return get(hvHash(name)); }
void bind(const Texture *t, u8 map);
void resetBindings();      // forget cached bindings (call when GX state was reset)
u32 takeLoadCount();       // texture loads since the last call (profiling)
}  // namespace tex

// Time of day, sky dome, sun/moon/stars and drifting 3D clouds.
#pragma once
#include "gfx/renderer.h"

namespace sky {
// hours 0..24 -> sets gfx environment (light ramp, fog, sky colours)
void setTime(f32 hours);
f32 time();
f32 dayFactor();      // 1 at noon, 0 at night
void init();
void update(f32 dt);
void draw();           // dome + celestial bodies (call first, after beginScene)
void drawClouds();     // after opaque world
}  // namespace sky

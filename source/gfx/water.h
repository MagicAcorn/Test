// Lake and river surfaces: depth-tinted, shoreline foam, two scrolling wave layers.
#pragma once
#include "gfx/renderer.h"

class World;

namespace water {
void build(const World &w);
void draw(f32 time);
// rings/splashes on the water surface (fishing spots, footsteps)
void drawRipple(const Vec3 &p, f32 radius, f32 alpha);
}  // namespace water

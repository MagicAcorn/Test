// Terrain (chunked, 2 LODs with skirts) and wind-blown grass.
#pragma once
#include "gfx/renderer.h"

class World;

namespace terrain {
void build(const World &w);
void draw();
void drawGrass(f32 time);
int chunksDrawn();
}  // namespace terrain

// Draws the static world (terrain, water, sky, placed objects, lights).
#pragma once
#include "gfx/renderer.h"

namespace scene {
void init();
void drawSkyAndTerrain(f32 time);
void drawObjects();
void drawWaterAndGrass(f32 time);
void drawLightGlows(f32 time);
u32 objectsDrawn();
}  // namespace scene

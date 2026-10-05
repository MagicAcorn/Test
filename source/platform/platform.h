// Platform abstraction: video/frame pacing, controller input, saves, audio
// output and asset access. GameCube (libogc) and PC (test harness) versions.
#pragma once
#include "core/types.h"

// Button bits match libogc's PAD_BUTTON_* values.
enum {
    BTN_LEFT = 0x0001,
    BTN_RIGHT = 0x0002,
    BTN_DOWN = 0x0004,
    BTN_UP = 0x0008,
    BTN_Z = 0x0010,
    BTN_R = 0x0020,
    BTN_L = 0x0040,
    BTN_A = 0x0100,
    BTN_B = 0x0200,
    BTN_X = 0x0400,
    BTN_Y = 0x0800,
    BTN_START = 0x1000,
};

struct PadState {
    u16 held = 0, pressed = 0, released = 0;
    f32 sx = 0, sy = 0;   // main stick, -1..1 (deadzoned)
    f32 cx = 0, cy = 0;   // C-stick
    f32 l = 0, r = 0;     // analog triggers 0..1
    bool connected = false;
};

namespace plat {

void setArgs(int argc, char **argv);   // PC harness options (no-op on console)
void init();
void shutdown();

// Screen (EFB) dimensions in pixels.
int screenW();
int screenH();
bool widescreen();

// Called after all GX drawing for a frame; copies the EFB to the TV and waits for vsync.
void endFrame();
// Seconds since last frame (clamped).
f32 frameDelta();
u32 frameCount();
u64 timeMicros();

void pollInput(PadState &p);
void rumble(bool on);

// Single save slot.
bool saveWrite(const void *data, u32 size);
s32 saveRead(void *data, u32 maxSize);
bool saveExists();

// Asset archive embedded in the executable (GC) or loaded from data/ (PC).
const u8 *assetData(u32 *size);

// Audio: the game mixes stereo s16 samples at AUDIO_RATE via a callback.
static const int AUDIO_RATE = 48000;
typedef void (*AudioCallback)(s16 *stereo, u32 frames, void *user);
void audioStart(AudioCallback cb, void *user);

// For the PC harness: true when the scripted run is finished.
bool quitRequested();

// Fast-forward hint: PC harness may skip rasterization on frames nobody looks at.
bool wantsFrameRendered();

}  // namespace plat

// Selects the real libogc GX (GameCube/Wii) or the PC software emulation.
#pragma once

#ifdef HV_PC
#include "platform/pc/gxemu.h"
// The emulator pads display lists itself in GX_EndDispList.
static inline void hvDispListPad(void) {}
#else
#include <gccore.h>
// The write-gather pipe only flushes whole 32-byte bursts; push NOPs so the
// tail of a display list being recorded reaches memory before
// GX_EndDispList() switches the CPU FIFO back.
static inline void hvDispListPad(void) {
    for (int i = 0; i < 32; i++) wgPipe->U8 = 0;
}
#endif

// Records a display list into buf (32-byte aligned, size multiple of 32).
// Returns the recorded size (0 on overflow).
#define HV_BEGIN_DL(buf, size) do { DCInvalidateRange((buf), (size)); GX_BeginDispList((buf), (size)); } while (0)
static inline u32 hvEndDispList(void) {
    hvDispListPad();
    return GX_EndDispList();
}

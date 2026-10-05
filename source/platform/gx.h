// Selects the real libogc GX (GameCube/Wii) or the PC software emulation.
#pragma once

#ifdef HV_PC
#include "platform/pc/gxemu.h"
#else
#include <gccore.h>
#endif

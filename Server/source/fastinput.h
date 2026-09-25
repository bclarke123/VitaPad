#pragma once

#include <stdint.h>
#include "fastbuttons.h"

// Fast buttons (see kernel/fastbuttons.h): the 12 Vita buttons read straight from the system
// controller, about 10 ms sooner than SceCtrl. Needs the kernel module.

// Call once per frame from the main loop: keeps the kernel thread running (it stops by itself
// a moment after the app stops calling, e.g. when it's suspended)
void fast_update(void);

// Replaces the buttons of a SceCtrl mask with the fast ones when they're available
uint32_t fast_apply(uint32_t buttons);

int fast_enabled(void);
void fast_set_enabled(int enabled);

// Returns 0 when fast buttons aren't available (no kernel module)
int fast_stats(FastButtonsStats *stats);

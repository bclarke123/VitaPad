#pragma once

// Fast buttons: SceCtrl refreshes the buttons once per frame (60/s), but the system controller
// (syscon) knows about a press about 10 ms sooner on average. A kernel thread reads the buttons
// from syscon every few ms and gives them to the app in the SceCtrl layout.

#include <stdint.h>

typedef struct {
	int available;       // 0 if the syscon read function wasn't found
	int running;
	int learned;         // Buttons whose syscon polarity has been learned (12 = all)
	int fallbacks;       // Times a button disagreed with SceCtrl for too long (SceCtrl used for it meanwhile)
	int matched;         // Button changes seen by both syscon and SceCtrl
	int avg_us;          // Average of (SceCtrl time - syscon time): how much sooner syscon saw them
	int min_us, max_us;
	int read_us;         // Average time one syscon read takes
	int read_max_us;
	int reads_per_s;
} FastButtonsStats;

// SceCtrl buttons that fast buttons provide
#define FAST_BUTTONS_MASK (SCE_CTRL_UP | SCE_CTRL_RIGHT | SCE_CTRL_DOWN | SCE_CTRL_LEFT | \
	SCE_CTRL_TRIANGLE | SCE_CTRL_CIRCLE | SCE_CTRL_CROSS | SCE_CTRL_SQUARE | \
	SCE_CTRL_SELECT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_START)

#ifdef VITAPAD_KERNEL
void fast_module_start(void);
void fast_module_stop(void);
#endif

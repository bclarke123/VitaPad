#pragma once

// Experiment: does the system controller (syscon) report buttons sooner than SceCtrl, which the
// system refreshes once per frame? A kernel thread reads both very often and times each button
// change seen by both.

#include <stdint.h>

typedef struct {
	int available;       // 0 if the syscon read function wasn't found
	int matched;         // Button changes seen by both
	int unmatched;       // Changes seen by only one of them (within 200 ms)
	int avg_us;          // Average of (SceCtrl time - syscon time): > 0 means syscon saw it first
	int min_us, max_us;
	int read_us;         // Average time one syscon read takes
	int read_max_us;
} LatencyStats;

#ifdef VITAPAD_KERNEL
void latency_module_start(void);
void latency_module_stop(void);
#endif

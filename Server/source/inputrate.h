#pragma once

#include <stdint.h>

// Measures how often the Vita really updates its inputs, by counting new sample timestamps,
// and how many of the PC's polls get new data. Shown on the main screen, to study input latency.

typedef struct {
	int ctrl;      // Buttons and sticks updates per second
	int touch;     // Front touch panel updates per second
	int motion;    // Motion sensor updates per second
	int polls;     // PC polls per second (Wi-Fi)
	int fresh;     // PC polls per second that carried a buttons/sticks update the previous poll didn't have
} InputRates;

// Starts the measuring thread
void input_rate_init(void);

// Rates over the last full second
void input_rate_get(InputRates *rates);

// Called by the network thread on every PC poll, with the buttons/sticks sample timestamp it sent
void input_rate_poll(uint64_t ctrl_timestamp);

// Syscon button experiment (needs the kernel module): runs while enabled
#include "latency.h"
void input_rate_experiment(int enable);
// Returns 0 if the experiment isn't available
int input_rate_experiment_get(LatencyStats *stats);

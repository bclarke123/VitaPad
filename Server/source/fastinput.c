#include <string.h>
#include <psp2/ctrl.h>

#include "fastinput.h"
#include "psbutton.h"

// From the kernel module (weak imports: resolved only once the module is loaded)
int vitapadKernelFastStart(void);
int vitapadKernelFastStop(void);
int vitapadKernelFastGetButtons(uint32_t *buttons);
int vitapadKernelFastGetStats(FastButtonsStats *stats);

static volatile int enabled = 1;
static int started = 0;

void fast_update(void){
	if (!ps_available()) return;
	if (enabled){
		vitapadKernelFastStart();
		started = 1;
	}else if (started){
		vitapadKernelFastStop();
		started = 0;
	}
}

uint32_t fast_apply(uint32_t buttons){
	if (!enabled || !ps_available()) return buttons;
	uint32_t fast;
	if (vitapadKernelFastGetButtons(&fast) < 0) return buttons;
	return (buttons & ~FAST_BUTTONS_MASK) | (fast & FAST_BUTTONS_MASK);
}

int fast_enabled(void){
	return enabled;
}

void fast_set_enabled(int value){
	enabled = value != 0;
}

int fast_stats(FastButtonsStats *stats){
	memset(stats, 0, sizeof(*stats));
	if (!ps_available()) return 0;
	return vitapadKernelFastGetStats(stats) >= 0;
}

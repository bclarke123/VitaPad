#include <stdint.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "inputrate.h"

// Checked much more often than any input updates, so every update is seen
#define CHECK_US 500
#define WINDOW_US (1000 * 1000)

static InputRates last;
static volatile int polls = 0, fresh = 0, streamed = 0;
static uint64_t last_poll_timestamp = 0;

static int rate_thread(SceSize args, void *argp){
	uint64_t ctrl_ts = 0, touch_ts = 0, motion_ts = 0;
	int ctrl = 0, touch = 0, motion = 0;
	uint64_t window_start = sceKernelGetProcessTimeWide();
	for (;;){
		SceCtrlData pad;
		SceTouchData front;
		SceMotionSensorState state;
		if (sceCtrlPeekBufferPositive(0, &pad, 1) >= 0 && pad.timeStamp != ctrl_ts){
			ctrl_ts = pad.timeStamp;
			ctrl++;
		}
		if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1) >= 0 && front.timeStamp != touch_ts){
			touch_ts = front.timeStamp;
			touch++;
		}
		if (sceMotionGetSensorState(&state, 1) == 0 && state.hostTimestamp != motion_ts){
			motion_ts = state.hostTimestamp;
			motion++;
		}

		uint64_t now = sceKernelGetProcessTimeWide();
		if (now - window_start >= WINDOW_US){
			// Scaled to exactly one second, the window can run slightly long
			uint64_t span = now - window_start;
			last.ctrl = ctrl * (uint64_t)WINDOW_US / span;
			last.touch = touch * (uint64_t)WINDOW_US / span;
			last.motion = motion * (uint64_t)WINDOW_US / span;
			last.polls = polls * (uint64_t)WINDOW_US / span;
			last.fresh = fresh * (uint64_t)WINDOW_US / span;
			last.streamed = streamed * (uint64_t)WINDOW_US / span;
			ctrl = touch = motion = 0;
			polls = fresh = streamed = 0;
			window_start = now;
		}
		sceKernelDelayThread(CHECK_US);
	}
	return 0;
}

void input_rate_init(void){
	memset(&last, 0, sizeof(last));
	SceUID thread = sceKernelCreateThread("VitaPad Rate Thread", rate_thread, 0x10000100, 0x4000, 0, 0, NULL);
	if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
}

void input_rate_get(InputRates *rates){
	*rates = last;
}

void input_rate_poll(uint64_t ctrl_timestamp){
	polls++;
	if (ctrl_timestamp != last_poll_timestamp) fresh++;
	last_poll_timestamp = ctrl_timestamp;
}

void input_rate_stream(void){
	streamed++;
}

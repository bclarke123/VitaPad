#include <psp2kern/ctrl.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/cpu.h>

#define VITAPAD_KERNEL
#include "latency.h"

// From taiHEN: looked up at runtime, so the module still loads if the function isn't there
int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);
#define KERNEL_PID 0x10005
#define SCE_SYSCON_FOR_DRIVER_NID 0x60A35F64
#define SCE_SYSCON_GET_CONTROLS_INFO_NID 0x145F59A4

#define CHECK_US 250
// A change seen by one side and not the other within this long doesn't count
#define MATCH_WINDOW_US (200 * 1000)

static int (*syscon_get_controls)(SceUInt32 *ctrl) = NULL;

// Syscon bit, SceCtrl bit
static const uint32_t buttons[][2] = {
	{ 0x1, SCE_CTRL_UP }, { 0x2, SCE_CTRL_RIGHT }, { 0x4, SCE_CTRL_DOWN }, { 0x8, SCE_CTRL_LEFT },
	{ 0x10, SCE_CTRL_TRIANGLE }, { 0x20, SCE_CTRL_CIRCLE }, { 0x40, SCE_CTRL_CROSS }, { 0x80, SCE_CTRL_SQUARE },
	{ 0x100, SCE_CTRL_SELECT }, { 0x200, SCE_CTRL_LTRIGGER }, { 0x400, SCE_CTRL_RTRIGGER }, { 0x800, SCE_CTRL_START },
};
#define BUTTONS_NUM (sizeof(buttons) / sizeof(buttons[0]))

static SceUID thread = -1;
static volatile int running = 0;
static LatencyStats stats;
static int64_t sum_us, read_sum_us;
static int reads;

static void record(int64_t delta){
	if (stats.matched == 0 || delta < stats.min_us) stats.min_us = delta;
	if (stats.matched == 0 || delta > stats.max_us) stats.max_us = delta;
	stats.matched++;
	sum_us += delta;
	stats.avg_us = sum_us / stats.matched;
}

static int latency_thread(SceSize args, void *argp){
	uint64_t syscon_edge[BUTTONS_NUM], ctrl_edge[BUTTONS_NUM];
	uint32_t last_syscon = 0, last_ctrl = 0;
	int first = 1;
	memset(syscon_edge, 0, sizeof(syscon_edge));
	memset(ctrl_edge, 0, sizeof(ctrl_edge));

	while (running){
		SceUInt32 raw = 0;
		SceCtrlData pad;
		uint64_t before = ksceKernelGetSystemTimeWide();
		int ok = syscon_get_controls(&raw) >= 0;
		uint64_t now = ksceKernelGetSystemTimeWide();
		memset(&pad, 0, sizeof(pad));
		ok = ok && ksceCtrlPeekBufferPositive(0, &pad, 1) >= 0;

		if (ok){
			int took = now - before;
			reads++;
			read_sum_us += took;
			stats.read_us = read_sum_us / reads;
			if (took > stats.read_max_us) stats.read_max_us = took;

			if (!first){
				// Only changes matter, so it doesn't matter whether syscon bits are active high or low
				for (int i = 0; i < (int)BUTTONS_NUM; i++){
					int syscon_changed = ((raw ^ last_syscon) & buttons[i][0]) != 0;
					int ctrl_changed = ((pad.buttons ^ last_ctrl) & buttons[i][1]) != 0;
					if (syscon_changed && ctrl_changed){
						record(0);
						syscon_edge[i] = ctrl_edge[i] = 0;
						continue;
					}
					if (syscon_changed){
						if (ctrl_edge[i]){ record(-(int64_t)(now - ctrl_edge[i])); ctrl_edge[i] = 0; }
						else syscon_edge[i] = now;
					}
					if (ctrl_changed){
						if (syscon_edge[i]){ record(now - syscon_edge[i]); syscon_edge[i] = 0; }
						else ctrl_edge[i] = now;
					}
					if (syscon_edge[i] && now - syscon_edge[i] > MATCH_WINDOW_US){ stats.unmatched++; syscon_edge[i] = 0; }
					if (ctrl_edge[i] && now - ctrl_edge[i] > MATCH_WINDOW_US){ stats.unmatched++; ctrl_edge[i] = 0; }
				}
			}
			first = 0;
			last_syscon = raw;
			last_ctrl = pad.buttons;
		}
		ksceKernelDelayThread(CHECK_US);
	}
	return ksceKernelExitDeleteThread(0);
}

static int start(void){
	if (!syscon_get_controls) return -1;
	if (running) return 0;
	memset(&stats, 0, sizeof(stats));
	stats.available = 1;
	sum_us = read_sum_us = 0;
	reads = 0;
	running = 1;
	thread = ksceKernelCreateThread("vitapad_latency_thread", latency_thread, 0x3C, 0x2000, 0, SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
	if (thread < 0){
		running = 0;
		return thread;
	}
	return ksceKernelStartThread(thread, 0, NULL);
}

static void stop(void){
	if (!running) return;
	running = 0;
	ksceKernelWaitThreadEnd(thread, NULL, NULL);
	thread = -1;
}

// Syscall: starts the experiment (statistics start from zero)
int vitapadKernelLatencyStart(void){
	uint32_t state;
	ENTER_SYSCALL(state);
	int res = start();
	EXIT_SYSCALL(state);
	return res;
}

// Syscall: stops the experiment, statistics are kept
int vitapadKernelLatencyStop(void){
	uint32_t state;
	ENTER_SYSCALL(state);
	stop();
	EXIT_SYSCALL(state);
	return 0;
}

// Syscall: copies the statistics to a user pointer
int vitapadKernelLatencyGet(LatencyStats *out){
	uint32_t state;
	ENTER_SYSCALL(state);
	LatencyStats copy = stats;
	copy.available = syscon_get_controls != NULL;
	int res = ksceKernelMemcpyKernelToUser(out, &copy, sizeof(copy));
	EXIT_SYSCALL(state);
	return res;
}

void latency_module_start(void){
	uintptr_t func = 0;
	if (module_get_export_func(KERNEL_PID, "SceSyscon", SCE_SYSCON_FOR_DRIVER_NID, SCE_SYSCON_GET_CONTROLS_INFO_NID, &func) >= 0 && func)
		syscon_get_controls = (int (*)(SceUInt32 *))func;
}

void latency_module_stop(void){
	stop();
}

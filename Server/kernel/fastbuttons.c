#include <psp2kern/ctrl.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/cpu.h>

#define VITAPAD_KERNEL
#include "fastbuttons.h"

// From taiHEN: looked up at runtime, so the module still loads if the function isn't there
int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);
#define KERNEL_PID 0x10005
#define SCE_SYSCON_FOR_DRIVER_NID 0x60A35F64
#define SCE_SYSCON_GET_CONTROLS_INFO_NID 0x145F59A4

// One syscon read takes about 1.2 ms (measured), and syscon also serves power, battery and SceCtrl:
// reading every 4 ms keeps it about 30% busy
#define PERIOD_US 4000
#define MIN_DELAY_US 500
// The app calls in every frame; if it stops (suspended, closed), stop reading syscon
#define APP_TIMEOUT_US (2 * 1000 * 1000)
// Buttons from a read older than this aren't used (a read can occasionally take ~10 ms)
#define STALE_US (30 * 1000)
// Polarity: learned from moments when a button is stable on both sides
#define STABLE_US (50 * 1000)
#define VOTES_NEEDED 20
// If syscon disagrees with SceCtrl for longer than this, SceCtrl is used for that button
#define DISAGREE_US (100 * 1000)
// For the statistics: a change seen by one side and not the other within this long doesn't count
#define MATCH_WINDOW_US (200 * 1000)

static int (*syscon_get_controls)(SceUInt32 *ctrl) = NULL;

// Syscon bit, SceCtrl bit
static const uint32_t buttons[][2] = {
	{ 0x1, SCE_CTRL_UP }, { 0x2, SCE_CTRL_RIGHT }, { 0x4, SCE_CTRL_DOWN }, { 0x8, SCE_CTRL_LEFT },
	{ 0x10, SCE_CTRL_TRIANGLE }, { 0x20, SCE_CTRL_CIRCLE }, { 0x40, SCE_CTRL_CROSS }, { 0x80, SCE_CTRL_SQUARE },
	{ 0x100, SCE_CTRL_SELECT }, { 0x200, SCE_CTRL_LTRIGGER }, { 0x400, SCE_CTRL_RTRIGGER }, { 0x800, SCE_CTRL_START },
};
#define BUTTONS_NUM ((int)(sizeof(buttons) / sizeof(buttons[0])))

typedef struct {
	int polarity;             // 0 = unknown, 1 = syscon bit set when pressed, -1 = bit clear when pressed
	int agree, disagree;      // Polarity votes
	uint64_t syscon_changed;  // When the syscon bit last changed
	uint64_t ctrl_changed;    // When the SceCtrl bit last changed
	uint64_t disagree_since;  // 0 while both agree
	int fallback;             // Disagreed for too long: SceCtrl is used for this button
	uint64_t syscon_edge, ctrl_edge; // Unmatched changes, for the statistics
} ButtonState;

static SceUID thread = -1;
static volatile int running = 0;
static volatile uint64_t app_seen = 0;
static volatile uint64_t last_read = 0;
static volatile uint32_t fast_state = 0;
static ButtonState state[BUTTONS_NUM];
static FastButtonsStats stats;
static int64_t sum_us, read_sum_us;
static int reads, window_reads;
static uint64_t window_start;

static void record(int64_t delta){
	if (stats.matched == 0 || delta < stats.min_us) stats.min_us = delta;
	if (stats.matched == 0 || delta > stats.max_us) stats.max_us = delta;
	stats.matched++;
	sum_us += delta;
	stats.avg_us = sum_us / stats.matched;
}

// One read of both sources; updates fast_state and the statistics
static void update(uint32_t *last_raw, uint32_t *last_ctrl, int *first){
	SceUInt32 raw = 0;
	SceCtrlData pad;
	uint64_t before = ksceKernelGetSystemTimeWide();
	if (syscon_get_controls(&raw) < 0) return;
	uint64_t now = ksceKernelGetSystemTimeWide();
	memset(&pad, 0, sizeof(pad));
	if (ksceCtrlPeekBufferPositive(0, &pad, 1) < 0) return;

	int took = now - before;
	reads++;
	window_reads++;
	read_sum_us += took;
	stats.read_us = read_sum_us / reads;
	if (took > stats.read_max_us) stats.read_max_us = took;
	if (now - window_start >= 1000 * 1000){
		stats.reads_per_s = window_reads * 1000000ULL / (now - window_start);
		window_reads = 0;
		window_start = now;
	}

	uint32_t out = 0;
	int learned = 0;
	for (int i = 0; i < BUTTONS_NUM; i++){
		ButtonState *b = &state[i];
		int bit = (raw & buttons[i][0]) != 0;
		int ctrl = (pad.buttons & buttons[i][1]) != 0;
		int syscon_changed = !*first && (((raw ^ *last_raw) & buttons[i][0]) != 0);
		int ctrl_changed = !*first && (((pad.buttons ^ *last_ctrl) & buttons[i][1]) != 0);
		if (syscon_changed || *first) b->syscon_changed = now;
		if (ctrl_changed || *first) b->ctrl_changed = now;

		// Learning the polarity while nothing moves
		if (!b->polarity && now - b->syscon_changed >= STABLE_US && now - b->ctrl_changed >= STABLE_US){
			if (bit == ctrl) b->agree++;
			else b->disagree++;
			if (b->agree >= VOTES_NEEDED) b->polarity = 1;
			else if (b->disagree >= VOTES_NEEDED) b->polarity = -1;
		}

		int pressed = ctrl;
		if (b->polarity){
			learned++;
			int fast = b->polarity > 0 ? bit : !bit;
			// Syscon normally leads SceCtrl by up to a frame or so; much longer means something is off
			// with this button: trust SceCtrl until they agree again
			if (fast == ctrl){
				b->disagree_since = 0;
				b->fallback = 0;
			}else if (!b->disagree_since) b->disagree_since = now;
			else if (!b->fallback && now - b->disagree_since > DISAGREE_US){
				b->fallback = 1;
				stats.fallbacks++;
			}
			pressed = b->fallback ? ctrl : fast;
		}
		if (pressed) out |= buttons[i][1];

		// Statistics: how much sooner syscon saw each change
		if (syscon_changed && ctrl_changed){
			record(0);
			b->syscon_edge = b->ctrl_edge = 0;
		}else{
			if (syscon_changed){
				if (b->ctrl_edge){ record(-(int64_t)(now - b->ctrl_edge)); b->ctrl_edge = 0; }
				else b->syscon_edge = now;
			}
			if (ctrl_changed){
				if (b->syscon_edge){ record(now - b->syscon_edge); b->syscon_edge = 0; }
				else b->ctrl_edge = now;
			}
		}
		if (b->syscon_edge && now - b->syscon_edge > MATCH_WINDOW_US) b->syscon_edge = 0;
		if (b->ctrl_edge && now - b->ctrl_edge > MATCH_WINDOW_US) b->ctrl_edge = 0;
	}
	stats.learned = learned;
	fast_state = out;
	last_read = now;
	*first = 0;
	*last_raw = raw;
	*last_ctrl = pad.buttons;
}

static int fast_thread(SceSize args, void *argp){
	uint32_t last_raw = 0, last_ctrl = 0;
	int first = 1;
	while (running){
		uint64_t start = ksceKernelGetSystemTimeWide();
		if (start - app_seen > APP_TIMEOUT_US) break;
		update(&last_raw, &last_ctrl, &first);
		int elapsed = ksceKernelGetSystemTimeWide() - start;
		ksceKernelDelayThread(elapsed < PERIOD_US - MIN_DELAY_US ? PERIOD_US - elapsed : MIN_DELAY_US);
	}
	running = 0;
	last_read = 0;
	return ksceKernelExitDeleteThread(0);
}

static int start(void){
	if (!syscon_get_controls) return -1;
	app_seen = ksceKernelGetSystemTimeWide();
	if (running) return 0;
	// A thread that stopped on its own may still be finishing
	if (thread >= 0){
		SceUInt timeout = 100 * 1000;
		ksceKernelWaitThreadEnd(thread, NULL, &timeout);
		thread = -1;
	}
	memset(state, 0, sizeof(state));
	memset(&stats, 0, sizeof(stats));
	sum_us = read_sum_us = 0;
	reads = window_reads = 0;
	window_start = app_seen;
	last_read = 0;
	running = 1;
	thread = ksceKernelCreateThread("vitapad_fast_buttons", fast_thread, 0x3C, 0x2000, 0, SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
	if (thread < 0){
		running = 0;
		return thread;
	}
	return ksceKernelStartThread(thread, 0, NULL);
}

static void stop(void){
	if (thread < 0) return;
	running = 0;
	ksceKernelWaitThreadEnd(thread, NULL, NULL);
	thread = -1;
}

// Syscall: starts fast buttons, or keeps them running. Call it every frame: they stop by themselves
// when the app stops calling
int vitapadKernelFastStart(void){
	uint32_t state;
	ENTER_SYSCALL(state);
	int res = start();
	EXIT_SYSCALL(state);
	return res;
}

int vitapadKernelFastStop(void){
	uint32_t state;
	ENTER_SYSCALL(state);
	stop();
	EXIT_SYSCALL(state);
	return 0;
}

// Syscall: copies the fast buttons (SceCtrl layout, FAST_BUTTONS_MASK only) to a user pointer.
// Returns < 0 when they aren't usable right now: use SceCtrl instead
int vitapadKernelFastGetButtons(uint32_t *out){
	uint32_t state;
	ENTER_SYSCALL(state);
	int res = -1;
	uint64_t read = last_read;
	if (running && read && ksceKernelGetSystemTimeWide() - read < STALE_US && stats.learned == BUTTONS_NUM){
		uint32_t value = fast_state;
		res = ksceKernelMemcpyKernelToUser(out, &value, sizeof(value));
	}
	EXIT_SYSCALL(state);
	return res;
}

int vitapadKernelFastGetStats(FastButtonsStats *out){
	uint32_t state;
	ENTER_SYSCALL(state);
	FastButtonsStats copy = stats;
	copy.available = syscon_get_controls != NULL;
	copy.running = running;
	int res = ksceKernelMemcpyKernelToUser(out, &copy, sizeof(copy));
	EXIT_SYSCALL(state);
	return res;
}

void fast_module_start(void){
	uintptr_t func = 0;
	if (module_get_export_func(KERNEL_PID, "SceSyscon", SCE_SYSCON_FOR_DRIVER_NID, SCE_SYSCON_GET_CONTROLS_INFO_NID, &func) >= 0 && func)
		syscon_get_controls = (int (*)(SceUInt32 *))func;
}

void fast_module_stop(void){
	stop();
}

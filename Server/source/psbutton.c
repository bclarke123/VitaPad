#include <stdio.h>
#include <string.h>
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/shellutil.h>
#include <psp2/vshbridge.h>
#include <taihen.h>

#include "psbutton.h"

#define TITLE_ID "VPAD00001"
#define KERNEL_MODULE_NAME "VitaPadKernel"
#define KERNEL_MODULE_PATH "ux0:app/" TITLE_ID "/module/vitapad_kernel.skprx"
// Written right before restarting after loading the module, so a failure can't cause a restart loop
#define RESTART_FLAG "ux0:data/VitaPad/restarted.flag"

// Two presses within this time go back to the LiveArea (like Adrenaline)
#define DOUBLE_TAP_US (300 * 1000)
// After a double tap the PS button stays unlocked this long, so the system can act on it
#define UNLOCK_US (3 * 1000 * 1000)
// A gap this long between frames means the Vita slept or VitaPad was in the background
#define RESUME_GAP_US (1 * 1000 * 1000)

// From the kernel module (weak import: resolved only once the module is loaded)
int vitapadKernelGetButtons(uint32_t *buttons);

#define PS_LOCK (SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2)

static int available = 0;
static volatile int enabled = 1;
static int locked = 0;
static volatile uint32_t ps_state = 0;
static uint64_t last_press = 0;
static uint64_t unlocked_until = 0;
static uint64_t last_frame = 0;
static uint32_t old_ps = 0;

static void lock(void){
	sceShellUtilLock(PS_LOCK);
	locked = 1;
}

static void unlock(void){
	sceShellUtilUnlock(PS_LOCK);
	locked = 0;
}

void ps_init(void){
	int search_buf[2];
	if (_vshKernelSearchModuleByName(KERNEL_MODULE_NAME, search_buf) < 0){
		// Already restarted once and the module still isn't there: give up, the PS button stays native
		SceUID flag = sceIoOpen(RESTART_FLAG, SCE_O_RDONLY, 0);
		if (flag >= 0){
			sceIoClose(flag);
			sceIoRemove(RESTART_FLAG);
			return;
		}
		// Fails without "Enable Unsafe Homebrew": the PS button stays native
		if (taiLoadStartKernelModule(KERNEL_MODULE_PATH, 0, NULL, 0) < 0) return;
		// Our imports of the module only resolve when the app starts: restart
		sceIoMkdir("ux0:data", 0777);
		sceIoMkdir("ux0:data/VitaPad", 0777);
		flag = sceIoOpen(RESTART_FLAG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
		if (flag >= 0) sceIoClose(flag);
		sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
		return;
	}
	sceIoRemove(RESTART_FLAG);
	available = 1;
	sceShellUtilInitEvents(0);
	if (enabled) lock();
}

void ps_update(void){
	if (!available) return;
	uint64_t now = sceKernelGetProcessTimeWide();

	// The lock doesn't survive sleep or going to the LiveArea: take it again
	if (last_frame && now - last_frame > RESUME_GAP_US){
		unlocked_until = 0;
		if (enabled) lock();
	}
	last_frame = now;

	if (!enabled){
		if (locked) unlock();
		ps_state = 0;
		return;
	}

	uint32_t buttons = 0;
	if (vitapadKernelGetButtons(&buttons) < 0) buttons = 0;
	uint32_t ps = buttons & SCE_CTRL_PSBUTTON;
	ps_state = ps;

	// Double tap: give the PS button back to the system and go to the LiveArea
	if (ps && !old_ps){
		if (last_press && now - last_press < DOUBLE_TAP_US){
			last_press = 0;
			unlock();
			unlocked_until = now + UNLOCK_US;
			sceAppMgrLaunchAppByName2(TITLE_ID, NULL, NULL);
		}else last_press = now;
	}
	old_ps = ps;

	if (unlocked_until && now > unlocked_until){
		unlocked_until = 0;
		lock();
	}
}

uint32_t ps_buttons(void){
	return (available && enabled) ? ps_state : 0;
}

int ps_status(void){
	if (!available) return PS_UNAVAILABLE;
	return enabled ? PS_ON : PS_OFF;
}

void ps_set_enabled(int value){
	enabled = value;
	if (!available) return;
	if (enabled && !locked) lock();
	if (!enabled && locked) unlock();
}

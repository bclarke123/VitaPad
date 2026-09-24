// VitaPad kernel module: lets the app read the PS button, which normal apps never see,
// and provides USB mode (see usb.c).
// Loaded at runtime by the app (needs "Enable Unsafe Homebrew" in HENkaku settings).
//
// Technique learned from Adrenaline by TheOfficialFloW (https://github.com/TheOfficialFloW/Adrenaline):
// reading the controller from kernel with the CPU offset (TPIDRPRW) cleared reports the PS button.

#include <psp2kern/ctrl.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/sysclib.h>

#define VITAPAD_KERNEL
#include "usb.h"

// Syscall: copies the current buttons, PS button included, to a user pointer
int vitapadKernelGetButtons(uint32_t *buttons) {
	uint32_t state;
	uint32_t cpu_offset;
	SceCtrlData pad;
	ENTER_SYSCALL(state);

	memset(&pad, 0, sizeof(pad));
	asm volatile ("mrc p15, 0, %0, c13, c0, 4" : "=r" (cpu_offset));
	asm volatile ("mcr p15, 0, %0, c13, c0, 4" :: "r" (0));
	int res = ksceCtrlPeekBufferPositive(0, &pad, 1);
	asm volatile ("mcr p15, 0, %0, c13, c0, 4" :: "r" (cpu_offset));

	if (res >= 0) res = ksceKernelMemcpyKernelToUser(buttons, &pad.buttons, sizeof(uint32_t));

	EXIT_SYSCALL(state);
	return res;
}

int _start(SceSize args, void *argp) __attribute__ ((weak, alias("module_start")));
int module_start(SceSize args, void *argp) {
	// USB mode is optional: the PS button keeps working if it can't start
	usb_module_start();
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
	usb_module_stop();
	return SCE_KERNEL_STOP_SUCCESS;
}

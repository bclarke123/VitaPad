#pragma once

#include <stdint.h>

// PS button capture: presses go to the PC (DualShock 4 PS button) and a double tap or a hold
// goes back to the LiveArea like the native PS button.
// Needs the kernel module, which needs "Enable Unsafe Homebrew" in HENkaku settings.

enum {
	PS_MODE_DOUBLE_TAP = 0, // Sent to the PC, double tap for the LiveArea
	PS_MODE_HOLD,           // Sent to the PC, hold for the LiveArea
	PS_MODE_NORMAL,         // Not captured, the system handles it
	PS_MODES_NUM
};

// Loads the kernel module. The first time, this restarts the app so it can use the module.
void ps_init(void);

// Call once per frame from the main loop: tap/hold detection, relocking after sleep
void ps_update(void);

// Current PS button state for the packets sent to the PC (0 or SCE_CTRL_PSBUTTON)
uint32_t ps_buttons(void);

// Nonzero if the kernel module is loaded (otherwise the PS button always works normally)
int ps_available(void);

int ps_mode(void);
void ps_set_mode(int mode);

// Names used in the settings file and the menu
const char *ps_mode_id(int mode);
const char *ps_mode_label(int mode);

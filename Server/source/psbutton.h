#pragma once

#include <stdint.h>

// PS button capture: a single press goes to the PC (DualShock 4 PS button),
// a double press goes back to the LiveArea like the native PS button.
// Needs the kernel module, which needs "Enable Unsafe Homebrew" in HENkaku settings.

enum {
	PS_UNAVAILABLE = 0, // Kernel module couldn't be loaded, the PS button works as usual
	PS_OFF,             // Available but disabled in the remap menu
	PS_ON,              // Sent to the PC
};

// Loads the kernel module. The first time, this restarts the app so it can use the module.
void ps_init(void);

// Call once per frame from the main loop: double tap detection, relocking after sleep
void ps_update(void);

// Current PS button state for the packets sent to the PC (0 or SCE_CTRL_PSBUTTON)
uint32_t ps_buttons(void);

// PS_UNAVAILABLE, PS_OFF or PS_ON
int ps_status(void);

// Enables/disables sending the PS button to the PC (no effect if unavailable)
void ps_set_enabled(int enabled);

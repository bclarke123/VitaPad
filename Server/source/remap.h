#pragma once

#include <stdint.h>
#include <vita2d.h>

// Button remapping, configured from the on-screen menu and saved on the memory card

// Nonzero while the remap menu is open: the PC receives a neutral pad meanwhile
extern volatile int remap_menu_open;

// Loads the saved mapping (defaults if there's none)
void remap_load(void);

// Applies the mapping to a SceCtrlData buttons mask
uint32_t remap_buttons(uint32_t buttons);

// Number of buttons that don't map to themselves
int remap_changed_count(void);

void remap_menu_open_now(void);

// Handles menu navigation, returns nonzero when the menu was closed
int remap_menu_update(uint32_t buttons);

void remap_menu_draw(vita2d_pgf *font);

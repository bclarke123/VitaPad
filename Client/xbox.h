#pragma once

// Xbox 360 controller output: turns what the Vita sends into an Xbox 360 pad state, shared by the
// Windows (ViGEm) and Linux (uinput) backends. Touch and motion have nowhere to go on an Xbox pad,
// except the front/rear touch corners, which can act as the shoulder and stick buttons.

#include <stdint.h>

#include "main.h"

// Same values as XInput / ViGEm's XUSB_GAMEPAD_*
enum {
	XBOX_DPAD_UP    = 0x0001,
	XBOX_DPAD_DOWN  = 0x0002,
	XBOX_DPAD_LEFT  = 0x0004,
	XBOX_DPAD_RIGHT = 0x0008,
	XBOX_START      = 0x0010,
	XBOX_BACK       = 0x0020,
	XBOX_LS         = 0x0040, // Left stick click
	XBOX_RS         = 0x0080, // Right stick click
	XBOX_LB         = 0x0100,
	XBOX_RB         = 0x0200,
	XBOX_GUIDE      = 0x0400,
	XBOX_A          = 0x1000,
	XBOX_B          = 0x2000,
	XBOX_X          = 0x4000,
	XBOX_Y          = 0x8000,
};

typedef struct {
	uint16_t buttons;  // XBOX_*
	uint8_t lt, rt;    // 0 - 255
	int16_t lx, ly;    // XInput convention: up is positive
	int16_t rx, ry;
} XboxState;

typedef struct {
	bool front_touch_buttons; // Front touch corners: upper = LB/RB, lower = LS/RS
	bool rear_touch_buttons;  // Same for the rear panel
	bool swap_shoulders;      // Vita L/R = LB/RB and upper touch corners = LT/RT
} XboxOptions;

void vitaToXbox(const PadPacketV2* packet, const XboxOptions* options, XboxState* state);

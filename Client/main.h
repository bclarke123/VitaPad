#pragma once

#include <stdint.h>

#include "protocol.h"

// PSVITA related stuffs
#define SCREEN_WIDTH TOUCH_WIDTH
#define SCREEN_HEIGHT TOUCH_HEIGHT

enum {
	SCE_CTRL_SELECT     = 0x000001,	//!< Select button.
	SCE_CTRL_START      = 0x000008,	//!< Start button.
	SCE_CTRL_UP         = 0x000010,	//!< Up D-Pad button.
	SCE_CTRL_RIGHT      = 0x000020,	//!< Right D-Pad button.
	SCE_CTRL_DOWN       = 0x000040,	//!< Down D-Pad button.
	SCE_CTRL_LEFT       = 0x000080,	//!< Left D-Pad button.
	SCE_CTRL_LTRIGGER   = 0x000100,	//!< Left trigger.
	SCE_CTRL_RTRIGGER   = 0x000200,	//!< Right trigger.
	SCE_CTRL_TRIANGLE   = 0x001000,	//!< Triangle button.
	SCE_CTRL_CIRCLE     = 0x002000,	//!< Circle button.
	SCE_CTRL_CROSS      = 0x004000,	//!< Cross button.
	SCE_CTRL_SQUARE     = 0x008000,	//!< Square button.
	// Not on the Vita itself: sent by remapped buttons or by a PS TV controller
	SCE_CTRL_L3         = 0x000002,	//!< L3 button.
	SCE_CTRL_R3         = 0x000004,	//!< R3 button.
	SCE_CTRL_L1         = 0x000400,	//!< L1 button.
	SCE_CTRL_R1         = 0x000800,	//!< R1 button.
	SCE_CTRL_PSBUTTON   = 0x010000	//!< PS button (sent when the Vita captures it).
};

#include <string.h>

#include "xbox.h"

// Vita stick (0 - 255, 128 = center) to XInput (-32768 - 32767)
static int16_t stick(uint8_t value)
{
	if (value >= 128) return (int16_t)((value - 128) * 32767 / 127);
	return (int16_t)((value - 128) * 256);
}

// Vita Y grows downwards, XInput Y upwards
static int16_t stickY(uint8_t value)
{
	int v = -(int)stick(value);
	return (int16_t)(v > 32767 ? 32767 : v);
}

static void touchToButtons(XboxState* state, const TouchPoint* points, int num, bool swap_shoulders)
{
	for (int i = 0; i < num; i++)
	{
		bool left = points[i].x < TOUCH_WIDTH / 2;
		if (points[i].y < TOUCH_HEIGHT / 2)
		{
			if (!swap_shoulders) state->buttons |= left ? XBOX_LB : XBOX_RB;
			else if (left) state->lt = 255;
			else state->rt = 255;
		}
		else state->buttons |= left ? XBOX_LS : XBOX_RS;
	}
}

void vitaToXbox(const PadPacketV2* packet, const XboxOptions* options, XboxState* state)
{
	static const struct { uint32_t vita; uint16_t xbox; } map[] = {
		// Same positions: Cross = A (bottom), Circle = B (right), Square = X (left), Triangle = Y (top)
		{ SCE_CTRL_CROSS, XBOX_A }, { SCE_CTRL_CIRCLE, XBOX_B }, { SCE_CTRL_SQUARE, XBOX_X }, { SCE_CTRL_TRIANGLE, XBOX_Y },
		{ SCE_CTRL_UP, XBOX_DPAD_UP }, { SCE_CTRL_DOWN, XBOX_DPAD_DOWN }, { SCE_CTRL_LEFT, XBOX_DPAD_LEFT }, { SCE_CTRL_RIGHT, XBOX_DPAD_RIGHT },
		{ SCE_CTRL_START, XBOX_START }, { SCE_CTRL_SELECT, XBOX_BACK }, { SCE_CTRL_PSBUTTON, XBOX_GUIDE },
		// Buttons the Vita lacks, sent by remapped buttons or by a PS TV controller
		{ SCE_CTRL_L1, XBOX_LB }, { SCE_CTRL_R1, XBOX_RB }, { SCE_CTRL_L3, XBOX_LS }, { SCE_CTRL_R3, XBOX_RS },
	};
	memset(state, 0, sizeof(*state));
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (packet->buttons & map[i].vita) state->buttons |= map[i].xbox;

	// Vita L/R: triggers (fully pressed or released), or bumpers when swapped
	if (packet->buttons & SCE_CTRL_LTRIGGER)
	{
		if (options->swap_shoulders) state->buttons |= XBOX_LB;
		else state->lt = 255;
	}
	if (packet->buttons & SCE_CTRL_RTRIGGER)
	{
		if (options->swap_shoulders) state->buttons |= XBOX_RB;
		else state->rt = 255;
	}

	if (options->front_touch_buttons) touchToButtons(state, packet->front, packet->front_num, options->swap_shoulders);
	if (options->rear_touch_buttons) touchToButtons(state, packet->rear, packet->rear_num, options->swap_shoulders);

	state->lx = stick(packet->lx);
	state->ly = stickY(packet->ly);
	state->rx = stick(packet->rx);
	state->ry = stickY(packet->ry);
}

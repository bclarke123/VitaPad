#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/uinput.h>

#include "uinput.h"

static int fd = -1;
static XboxState last;
static bool first = true;

// Buttons as the Linux Xbox 360 driver (xpad) reports them, so games and SDL map it like a real pad
static const struct { uint16_t xbox; int code; } buttons[] = {
	{ XBOX_A, BTN_A }, { XBOX_B, BTN_B }, { XBOX_X, BTN_X }, { XBOX_Y, BTN_Y },
	{ XBOX_LB, BTN_TL }, { XBOX_RB, BTN_TR }, { XBOX_BACK, BTN_SELECT }, { XBOX_START, BTN_START },
	{ XBOX_GUIDE, BTN_MODE }, { XBOX_LS, BTN_THUMBL }, { XBOX_RS, BTN_THUMBR },
};

static bool setupAxis(int code, int min, int max, int fuzz, int flat)
{
	struct uinput_abs_setup abs;
	memset(&abs, 0, sizeof(abs));
	abs.code = code;
	abs.absinfo.minimum = min;
	abs.absinfo.maximum = max;
	abs.absinfo.fuzz = fuzz;
	abs.absinfo.flat = flat;
	return ioctl(fd, UI_SET_ABSBIT, code) == 0 && ioctl(fd, UI_ABS_SETUP, &abs) == 0;
}

bool uiInit()
{
	fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0)
	{
		printf("ERROR: Unable to open /dev/uinput (%s).\n", strerror(errno));
		if (errno == EACCES)
			printf("Allow your user to create virtual controllers (see README), or run the client with sudo.\n");
		return false;
	}

	bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0;
	for (size_t i = 0; ok && i < sizeof(buttons) / sizeof(buttons[0]); i++)
		ok = ioctl(fd, UI_SET_KEYBIT, buttons[i].code) == 0;
	ok = ok && setupAxis(ABS_X, -32768, 32767, 16, 128) && setupAxis(ABS_Y, -32768, 32767, 16, 128);
	ok = ok && setupAxis(ABS_RX, -32768, 32767, 16, 128) && setupAxis(ABS_RY, -32768, 32767, 16, 128);
	ok = ok && setupAxis(ABS_Z, 0, 255, 0, 0) && setupAxis(ABS_RZ, 0, 255, 0, 0);
	ok = ok && setupAxis(ABS_HAT0X, -1, 1, 0, 0) && setupAxis(ABS_HAT0Y, -1, 1, 0, 0);

	struct uinput_setup setup;
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_USB;
	setup.id.vendor = 0x045E;  // Microsoft
	setup.id.product = 0x028E; // Xbox 360 Controller
	setup.id.version = 0x0110;
	snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "Microsoft X-Box 360 pad");
	ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0 && ioctl(fd, UI_DEV_CREATE) == 0;
	if (!ok)
	{
		printf("ERROR: Unable to create the virtual controller (%s).\n", strerror(errno));
		close(fd);
		fd = -1;
		return false;
	}
	first = true;
	return true;
}

static bool emit(int type, int code, int value)
{
	struct input_event event;
	memset(&event, 0, sizeof(event));
	event.type = type;
	event.code = code;
	event.value = value;
	return write(fd, &event, sizeof(event)) == (ssize_t)sizeof(event);
}

bool uiSubmit(const XboxState* state)
{
	if (fd < 0) return false;
	bool ok = true;
	for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
	{
		bool down = (state->buttons & buttons[i].xbox) != 0;
		if (first || down != ((last.buttons & buttons[i].xbox) != 0)) ok &= emit(EV_KEY, buttons[i].code, down);
	}
	// Linux reports Y growing downwards, like the Vita (XboxState uses the XInput convention)
	int ly = -(int)state->ly, ry = -(int)state->ry;
	if (ly > 32767) ly = 32767;
	if (ry > 32767) ry = 32767;
	int hat_x = (state->buttons & XBOX_DPAD_RIGHT ? 1 : 0) - (state->buttons & XBOX_DPAD_LEFT ? 1 : 0);
	int hat_y = (state->buttons & XBOX_DPAD_DOWN ? 1 : 0) - (state->buttons & XBOX_DPAD_UP ? 1 : 0);
	ok &= emit(EV_ABS, ABS_X, state->lx) && emit(EV_ABS, ABS_Y, ly);
	ok &= emit(EV_ABS, ABS_RX, state->rx) && emit(EV_ABS, ABS_RY, ry);
	ok &= emit(EV_ABS, ABS_Z, state->lt) && emit(EV_ABS, ABS_RZ, state->rt);
	ok &= emit(EV_ABS, ABS_HAT0X, hat_x) && emit(EV_ABS, ABS_HAT0Y, hat_y);
	ok &= emit(EV_SYN, SYN_REPORT, 0);
	last = *state;
	first = false;
	return ok;
}

void uiDestroy()
{
	if (fd < 0) return;
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
	fd = -1;
}

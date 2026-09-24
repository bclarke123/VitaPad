#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "usbmode.h"
#include "psbutton.h"
#include "remap.h"
#include "usb.h"

// From the kernel module (weak imports: resolved only once the module is loaded)
int vitapadKernelUsbStart(void);
int vitapadKernelUsbStop(void);
int vitapadKernelUsbSetReport(const UsbGamepadReport *report);
int vitapadKernelUsbGetState(void);

// The report is refreshed this often (the computer polls every 1 ms)
#define UPDATE_US (4 * 1000)
// After a failed start (or after the kernel gave USB back while we were in the background), retry this often
#define RETRY_US (1000 * 1000)

static volatile int enabled = 0;

// Front touchscreen corners act as the buttons the Vita lacks, like the ViGEm default
static uint16_t touch_buttons(void){
	SceTouchData touch;
	uint16_t buttons = 0;
	if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) < 0) return 0;
	for (int i = 0; i < (int)touch.reportNum && i < 2; i++){
		int left = touch.report[i].x < 960;
		int top = touch.report[i].y < 544;
		if (top) buttons |= left ? USB_BTN_L1 : USB_BTN_R1;
		else buttons |= left ? USB_BTN_L3 : USB_BTN_R3;
	}
	return buttons;
}

static uint8_t hat(uint32_t buttons){
	int up = buttons & SCE_CTRL_UP, down = buttons & SCE_CTRL_DOWN;
	int left = buttons & SCE_CTRL_LEFT, right = buttons & SCE_CTRL_RIGHT;
	if (up && right) return 1;
	if (right && down) return 3;
	if (down && left) return 5;
	if (left && up) return 7;
	if (up) return 0;
	if (right) return 2;
	if (down) return 4;
	if (left) return 6;
	return USB_HAT_NEUTRAL;
}

static void build_report(UsbGamepadReport *report){
	static const struct { uint32_t vita; uint16_t usb; } map[] = {
		{ SCE_CTRL_SQUARE, USB_BTN_SQUARE }, { SCE_CTRL_CROSS, USB_BTN_CROSS },
		{ SCE_CTRL_CIRCLE, USB_BTN_CIRCLE }, { SCE_CTRL_TRIANGLE, USB_BTN_TRIANGLE },
		{ SCE_CTRL_L1, USB_BTN_L1 }, { SCE_CTRL_R1, USB_BTN_R1 },
		{ SCE_CTRL_LTRIGGER, USB_BTN_L2 }, { SCE_CTRL_RTRIGGER, USB_BTN_R2 },
		{ SCE_CTRL_SELECT, USB_BTN_SHARE }, { SCE_CTRL_START, USB_BTN_OPTIONS },
		{ SCE_CTRL_L3, USB_BTN_L3 }, { SCE_CTRL_R3, USB_BTN_R3 },
		{ SCE_CTRL_PSBUTTON, USB_BTN_PS },
	};
	memset(report, 0, sizeof(*report));
	report->hat = USB_HAT_NEUTRAL;
	report->lx = report->ly = report->rx = report->ry = 128;
	// While the remap menu is open the computer gets a neutral pad
	if (remap_menu_open) return;

	SceCtrlData pad;
	sceCtrlPeekBufferPositive(0, &pad, 1);
	uint32_t buttons = remap_buttons(pad.buttons) | ps_buttons();
	for (int i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
		if (buttons & map[i].vita) report->buttons |= map[i].usb;
	report->buttons |= touch_buttons();
	report->hat = hat(buttons);
	report->lx = pad.lx;
	report->ly = pad.ly;
	report->rx = pad.rx;
	report->ry = pad.ry;
}

static int usb_thread(SceSize args, void *argp){
	uint64_t last_try = 0;
	for (;;){
		if (enabled && ps_available()){
			uint64_t now = sceKernelGetProcessTimeWide();
			if (vitapadKernelUsbGetState() == USB_STATE_OFF && now - last_try > RETRY_US){
				last_try = now;
				vitapadKernelUsbStart();
			}
			UsbGamepadReport report;
			build_report(&report);
			vitapadKernelUsbSetReport(&report);
		}
		sceKernelDelayThread(UPDATE_US);
	}
	return 0;
}

void usb_init(void){
	SceUID thread = sceKernelCreateThread("VitaPad USB Thread", usb_thread, 0x10000100, 0x4000, 0, 0, NULL);
	if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
}

int usb_enabled(void){
	return enabled;
}

void usb_set_enabled(int value){
	enabled = value;
	if (!enabled && ps_available()) vitapadKernelUsbStop();
}

int usb_state(void){
	if (!enabled || !ps_available()) return USB_STATE_OFF;
	return vitapadKernelUsbGetState();
}

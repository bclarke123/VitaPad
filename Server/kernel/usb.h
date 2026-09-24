#pragma once

// USB gamepad report, shared by the kernel module and the app

#include <stdint.h>

// Buttons, in DualShock 4 DirectInput order (button 1 = Square, ...)
#define USB_BTN_SQUARE   (1 << 0)
#define USB_BTN_CROSS    (1 << 1)
#define USB_BTN_CIRCLE   (1 << 2)
#define USB_BTN_TRIANGLE (1 << 3)
#define USB_BTN_L1       (1 << 4)
#define USB_BTN_R1       (1 << 5)
#define USB_BTN_L2       (1 << 6)
#define USB_BTN_R2       (1 << 7)
#define USB_BTN_SHARE    (1 << 8)
#define USB_BTN_OPTIONS  (1 << 9)
#define USB_BTN_L3       (1 << 10)
#define USB_BTN_R3       (1 << 11)
#define USB_BTN_PS       (1 << 12)
#define USB_BTN_TOUCHPAD (1 << 13)

// Hat switch: 0 = up, clockwise in 45 degree steps, anything above 7 = centered
#define USB_HAT_NEUTRAL 8

typedef struct __attribute__((packed)) {
	uint16_t buttons;
	uint8_t hat;
	uint8_t lx;
	uint8_t ly;
	uint8_t rx;
	uint8_t ry;
} UsbGamepadReport;

#ifdef VITAPAD_KERNEL
int usb_module_start(void);
void usb_module_stop(void);
#endif

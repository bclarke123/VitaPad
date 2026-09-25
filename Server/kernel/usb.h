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

// What the Vita presents to the computer
enum {
	USB_MODE_GENERIC = 0, // Standard HID gamepad (UsbGamepadReport)
	USB_MODE_DS4 = 1,     // DualShock 4 (Ds4Report): gyro, touchpad, PS button, recognized natively by macOS/Steam/SDL
};

// Largest report any mode uses
#define USB_REPORT_MAX 64

// Generic gamepad report (no report ID)
typedef struct __attribute__((packed)) {
	uint16_t buttons;
	uint8_t hat;
	uint8_t lx;
	uint8_t ly;
	uint8_t rx;
	uint8_t ry;
} UsbGamepadReport;

// DualShock 4 touchpad finger: bit 7 of contact set = not touching, bits 0-6 = tracking id;
// x and y are 12 bits each (touchpad is 1920 x 943)
typedef struct __attribute__((packed)) {
	uint8_t contact;
	uint8_t x_lo;
	uint8_t x_hi_y_lo; // x bits 8-11 in the low nibble, y bits 0-3 in the high nibble
	uint8_t y_hi;
} Ds4TouchPoint;

typedef struct __attribute__((packed)) {
	uint8_t timestamp;
	Ds4TouchPoint points[2];
} Ds4TouchReport;

// DualShock 4 USB input report 0x01 (layout as in Linux's hid-playstation driver)
typedef struct __attribute__((packed)) {
	uint8_t report_id;     // 0x01
	uint8_t lx, ly, rx, ry;
	uint8_t buttons[3];    // [0]: hat (low nibble), Square, Cross, Circle, Triangle
	                       // [1]: L1, R1, L2, R2, Share, Options, L3, R3
	                       // [2]: PS, touchpad click, 6-bit counter
	uint8_t l2, r2;        // Analog triggers
	uint16_t timestamp;    // Units of 16/3 microseconds
	uint8_t temperature;
	int16_t gyro[3];       // 16 per degree/second
	int16_t accel[3];      // 8192 per G
	uint8_t reserved1[5];
	uint8_t status[2];     // status[0]: battery 0-10 (low nibble), cable connected (bit 4)
	uint8_t reserved2;
	uint8_t num_touch_reports;
	Ds4TouchReport touch[3];
	uint8_t reserved3[3];
} Ds4Report;

_Static_assert(sizeof(UsbGamepadReport) == 7, "generic report must be 7 bytes");
_Static_assert(sizeof(Ds4Report) == 64, "DualShock 4 report must be 64 bytes");

#define DS4_TOUCHPAD_WIDTH 1920
#define DS4_TOUCHPAD_HEIGHT 943
#define DS4_HAT_NEUTRAL 8

#ifdef VITAPAD_KERNEL
int usb_module_start(void);
void usb_module_stop(void);
#endif

#pragma once

// USB mode: the Vita becomes a USB gamepad for the computer it's plugged into, no PC client needed.
// Needs the kernel module (same as the PS button capture).

enum {
	USB_STATE_OFF = 0,     // Wi-Fi mode (or USB mode unavailable)
	USB_STATE_WAITING,     // USB mode on, no computer yet
	USB_STATE_CONNECTED,   // USB mode on, the computer is using the gamepad
};

// Starts the thread feeding the USB gamepad
void usb_init(void);

int usb_enabled(void);
void usb_set_enabled(int enabled);

// One of USB_STATE_*
int usb_state(void);

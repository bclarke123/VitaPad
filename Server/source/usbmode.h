#pragma once

// USB mode: the Vita becomes a USB gamepad for the computer it's plugged into, no PC client needed.
// Needs the kernel module (same as the PS button capture).

// How the Vita talks to the computer (the "Connection" row of the remap menu)
enum {
	CONNECTION_WIFI = 0,    // VitaPad PC client over Wi-Fi
	CONNECTION_USB_DS4,     // USB, as a DualShock 4 (gyro, touchpad; recognized natively)
	CONNECTION_USB_GENERIC, // USB, as a standard gamepad
	CONNECTIONS_NUM
};

enum {
	USB_STATE_OFF = 0,     // Wi-Fi mode (or USB mode unavailable)
	USB_STATE_WAITING,     // USB mode on, no computer yet
	USB_STATE_CONNECTED,   // USB mode on, the computer is using the gamepad
};

// Starts the thread feeding the USB gamepad
void usb_init(void);

int usb_connection(void);
void usb_set_connection(int connection);

// Names used in the settings file and the menu
const char *usb_connection_id(int connection);
const char *usb_connection_label(int connection);

// One of USB_STATE_*
int usb_state(void);

// USB mode: the Vita shows up on the computer as a USB HID gamepad (generic or DualShock 4),
// no PC client needed.
//
// The app builds the report (so remapping and the PS button settings apply) and hands it to us
// with vitapadKernelUsbSetReport(); we present it to the host on an interrupt IN endpoint.
// Written from the SDK's SceUdcd API and the USB HID specification; xerpi's vitastick
// (https://github.com/xerpi/vitastick) showed that SceUdcd can do this.

#include <psp2kern/ctrl.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/cpu/cache.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/udcd.h>

#define VITAPAD_KERNEL
#include "usb.h"

#define DRIVER_NAME "VITAPAD_HID"
// The vendor ID is filled in by the system (Sony, 0x054C)
#define GENERIC_PRODUCT_ID 0x5650 // Arbitrary
#define DS4_PRODUCT_ID 0x09CC     // DualShock 4 (second revision)
#define MTP_PRODUCT_ID 0x04E4 // What the Vita normally presents (media transfer)

// Without updates from the app for this long, the gamepad goes neutral and normal USB comes back
#define APP_TIMEOUT_US (2 * 1000 * 1000)

#define EVF_CONNECTED    (1 << 0)
#define EVF_DISCONNECTED (1 << 1)
#define EVF_SENT         (1 << 2)
#define EVF_EXIT         (1 << 3)
#define EVF_ALL          (EVF_CONNECTED | EVF_DISCONNECTED | EVF_SENT | EVF_EXIT)

// Gamepad: 14 buttons (DualShock 4 DirectInput order), a hat switch, 4 axes (sticks)
static unsigned char report_descriptor_generic[] = {
	0x05, 0x01,       // Usage Page (Generic Desktop)
	0x09, 0x05,       // Usage (Game Pad)
	0xA1, 0x01,       // Collection (Application)
	0x05, 0x09,       //   Usage Page (Button)
	0x19, 0x01,       //   Usage Minimum (1)
	0x29, 0x0E,       //   Usage Maximum (14)
	0x15, 0x00,       //   Logical Minimum (0)
	0x25, 0x01,       //   Logical Maximum (1)
	0x75, 0x01,       //   Report Size (1)
	0x95, 0x0E,       //   Report Count (14)
	0x81, 0x02,       //   Input (Data, Var, Abs)
	0x95, 0x02,       //   Report Count (2)
	0x81, 0x03,       //   Input (Const): padding to 16 bits
	0x05, 0x01,       //   Usage Page (Generic Desktop)
	0x09, 0x39,       //   Usage (Hat Switch)
	0x15, 0x00,       //   Logical Minimum (0)
	0x25, 0x07,       //   Logical Maximum (7)
	0x35, 0x00,       //   Physical Minimum (0)
	0x46, 0x3B, 0x01, //   Physical Maximum (315)
	0x65, 0x14,       //   Unit (Degrees)
	0x75, 0x04,       //   Report Size (4)
	0x95, 0x01,       //   Report Count (1)
	0x81, 0x42,       //   Input (Data, Var, Abs, Null State)
	0x65, 0x00,       //   Unit (None)
	0x81, 0x03,       //   Input (Const): padding to 8 bits
	0x35, 0x00,       //   Physical Minimum (0): reset the hat's physical range for the axes
	0x46, 0xFF, 0x00, //   Physical Maximum (255)
	0x09, 0x30,       //   Usage (X): left stick
	0x09, 0x31,       //   Usage (Y)
	0x09, 0x32,       //   Usage (Z): right stick
	0x09, 0x35,       //   Usage (Rz)
	0x15, 0x00,       //   Logical Minimum (0)
	0x26, 0xFF, 0x00, //   Logical Maximum (255)
	0x75, 0x08,       //   Report Size (8)
	0x95, 0x04,       //   Report Count (4)
	0x81, 0x02,       //   Input (Data, Var, Abs)
	0xC0,             // End Collection
};

// DualShock 4: input report 0x01 (63 bytes: sticks, hat, 14 buttons, counter, triggers, then motion,
// battery and touchpad as vendor data), output report 0x05 (rumble / light bar), and the feature
// reports drivers read at startup (0x02 calibration, 0x12 pairing info, 0xA3 firmware info).
// Written to match the report layout Linux's hid-playstation and SDL expect.
static unsigned char report_descriptor_ds4[] = {
	0x05, 0x01,       // Usage Page (Generic Desktop)
	0x09, 0x05,       // Usage (Game Pad)
	0xA1, 0x01,       // Collection (Application)
	0x85, 0x01,       //   Report ID (1)
	0x09, 0x30,       //   Usage (X): left stick
	0x09, 0x31,       //   Usage (Y)
	0x09, 0x32,       //   Usage (Z): right stick
	0x09, 0x35,       //   Usage (Rz)
	0x15, 0x00,       //   Logical Minimum (0)
	0x26, 0xFF, 0x00, //   Logical Maximum (255)
	0x75, 0x08,       //   Report Size (8)
	0x95, 0x04,       //   Report Count (4)
	0x81, 0x02,       //   Input (Data, Var, Abs)
	0x09, 0x39,       //   Usage (Hat Switch)
	0x15, 0x00,       //   Logical Minimum (0)
	0x25, 0x07,       //   Logical Maximum (7)
	0x35, 0x00,       //   Physical Minimum (0)
	0x46, 0x3B, 0x01, //   Physical Maximum (315)
	0x65, 0x14,       //   Unit (Degrees)
	0x75, 0x04,       //   Report Size (4)
	0x95, 0x01,       //   Report Count (1)
	0x81, 0x42,       //   Input (Data, Var, Abs, Null State)
	0x65, 0x00,       //   Unit (None)
	0x05, 0x09,       //   Usage Page (Button)
	0x19, 0x01,       //   Usage Minimum (1)
	0x29, 0x0E,       //   Usage Maximum (14)
	0x15, 0x00,       //   Logical Minimum (0)
	0x25, 0x01,       //   Logical Maximum (1)
	0x75, 0x01,       //   Report Size (1)
	0x95, 0x0E,       //   Report Count (14)
	0x81, 0x02,       //   Input (Data, Var, Abs)
	0x06, 0x00, 0xFF, //   Usage Page (Vendor Defined)
	0x09, 0x20,       //   Usage (0x20): report counter
	0x75, 0x06,       //   Report Size (6)
	0x95, 0x01,       //   Report Count (1)
	0x25, 0x3F,       //   Logical Maximum (63)
	0x81, 0x02,       //   Input (Data, Var, Abs)
	0x05, 0x01,       //   Usage Page (Generic Desktop)
	0x09, 0x33,       //   Usage (Rx): L2
	0x09, 0x34,       //   Usage (Ry): R2
	0x35, 0x00,       //   Physical Minimum (0)
	0x46, 0xFF, 0x00, //   Physical Maximum (255)
	0x26, 0xFF, 0x00, //   Logical Maximum (255)
	0x75, 0x08,       //   Report Size (8)
	0x95, 0x02,       //   Report Count (2)
	0x81, 0x02,       //   Input (Data, Var, Abs)
	0x06, 0x00, 0xFF, //   Usage Page (Vendor Defined)
	0x09, 0x21,       //   Usage (0x21): motion, battery, touchpad
	0x95, 0x36,       //   Report Count (54)
	0x81, 0x02,       //   Input (Data, Var, Abs)
	0x85, 0x05,       //   Report ID (5)
	0x09, 0x22,       //   Usage (0x22): rumble / light bar
	0x95, 0x1F,       //   Report Count (31)
	0x91, 0x02,       //   Output (Data, Var, Abs)
	0x85, 0x02,       //   Report ID (2)
	0x09, 0x24,       //   Usage (0x24): calibration
	0x95, 0x24,       //   Report Count (36)
	0xB1, 0x02,       //   Feature (Data, Var, Abs)
	0x85, 0x12,       //   Report ID (0x12)
	0x09, 0x25,       //   Usage (0x25): pairing info
	0x95, 0x0F,       //   Report Count (15)
	0xB1, 0x02,       //   Feature (Data, Var, Abs)
	0x85, 0xA3,       //   Report ID (0xA3)
	0x09, 0x26,       //   Usage (0x26): firmware info
	0x95, 0x30,       //   Report Count (48)
	0xB1, 0x02,       //   Feature (Data, Var, Abs)
	0xC0,             // End Collection
};

// Feature 0x02: calibration chosen so drivers pass our values through unchanged
// (gyro: speed 540 with +-8640 swings = 16 per deg/s; accelerometer: +-8192 per G)
static unsigned char ds4_calibration[37] = {
	0x02,
	0, 0, 0, 0, 0, 0,                       // gyro pitch/yaw/roll bias
	0xC0, 0x21, 0x40, 0xDE,                 // pitch +8640 / -8640
	0xC0, 0x21, 0x40, 0xDE,                 // yaw
	0xC0, 0x21, 0x40, 0xDE,                 // roll
	0x1C, 0x02, 0x1C, 0x02,                 // gyro speed +540 / -540
	0x00, 0x20, 0x00, 0xE0,                 // accel x +8192 / -8192
	0x00, 0x20, 0x00, 0xE0,                 // accel y
	0x00, 0x20, 0x00, 0xE0,                 // accel z
	0, 0,
};

// Feature 0x12: pairing info, bytes 1-6 are the controller's (made-up) Bluetooth address
static unsigned char ds4_pairing[16] = {
	0x12, 0x01, 0x00, 0x00, 0x00, 0x50, 0x56, 0x08, 0x25, 0x00, 0, 0, 0, 0, 0, 0
};

// Feature 0xA3: firmware info (build date/time strings, then hardware and firmware versions)
static unsigned char ds4_firmware[49] = {
	0xA3,
	'S', 'e', 'p', ' ', '2', '5', ' ', '2', '0', '2', '6', 0, 0, 0, 0, 0,
	'0', '0', ':', '0', '0', ':', '0', '0', 0, 0, 0, 0, 0, 0, 0, 0,
	0x01, 0x00, // (unused)
	0x00, 0x01, // hardware version at byte 35 (little endian: 0x0100)
	0, 0, 0, 0,
	0x1A, 0xA0, // firmware version at byte 41 (0xA01A)
	0, 0, 0, 0, 0, 0,
};

// HID class descriptor, placed after the interface descriptor
static unsigned char hid_descriptor[] = {
	0x09,                               // bLength
	HID_DESCRIPTOR_HID,                 // bDescriptorType
	0x11, 0x01,                         // bcdHID 1.11
	0x00,                               // bCountryCode
	0x01,                               // bNumDescriptors
	HID_DESCRIPTOR_REPORT,              // bDescriptorType (report)
	0, 0,                               // wDescriptorLength: set by start_usb() for the mode
};

static SceUdcdEndpoint endpoints[2] = {
	{ 0x00, 0, 0, 0 },            // Control
	{ USB_ENDPOINT_IN, 1, 0, 0 }, // Interrupt IN: the gamepad reports
};

static SceUdcdInterface interfaces[1] = {
	{ -1, 0, 1 },
};

// The descriptor arrays end with an empty entry: SceUdcd walks them until it finds one.
// bInterval: high speed counts 2^(n-1) microframes of 125 us (4 = 1 ms), full speed counts ms
static SceUdcdEndpointDescriptor endpoint_hi[2] = {
	{ USB_DT_ENDPOINT_SIZE, USB_DT_ENDPOINT, USB_ENDPOINT_IN | 0x01, USB_ENDPOINT_TYPE_INTERRUPT, 64, 4, NULL, 0 },
	{ 0 },
};
static SceUdcdEndpointDescriptor endpoint_full[2] = {
	{ USB_DT_ENDPOINT_SIZE, USB_DT_ENDPOINT, USB_ENDPOINT_IN | 0x01, USB_ENDPOINT_TYPE_INTERRUPT, 64, 1, NULL, 0 },
	{ 0 },
};

static SceUdcdInterfaceDescriptor interface_hi[2] = {
	{ USB_DT_INTERFACE_SIZE, USB_DT_INTERFACE, 0, 0, 1, USB_CLASS_HID, 0, 0, 0, endpoint_hi, hid_descriptor, sizeof(hid_descriptor) },
	{ 0 },
};
static SceUdcdInterfaceDescriptor interface_full[2] = {
	{ USB_DT_INTERFACE_SIZE, USB_DT_INTERFACE, 0, 0, 1, USB_CLASS_HID, 0, 0, 0, endpoint_full, hid_descriptor, sizeof(hid_descriptor) },
	{ 0 },
};

static SceUdcdInterfaceSettings settings_hi[1] = { { interface_hi, 0, 1 } };
static SceUdcdInterfaceSettings settings_full[1] = { { interface_full, 0, 1 } };

#define CONFIG_TOTAL_LENGTH (USB_DT_CONFIG_SIZE + USB_DT_INTERFACE_SIZE + sizeof(hid_descriptor) + USB_DT_ENDPOINT_SIZE)

static SceUdcdConfigDescriptor config_desc_hi = {
	USB_DT_CONFIG_SIZE, USB_DT_CONFIG, CONFIG_TOTAL_LENGTH, 1, 1, 0, 0xC0, 0, settings_hi, NULL, 0
};
static SceUdcdConfigDescriptor config_desc_full = {
	USB_DT_CONFIG_SIZE, USB_DT_CONFIG, CONFIG_TOTAL_LENGTH, 1, 1, 0, 0xC0, 0, settings_full, NULL, 0
};

static SceUdcdConfiguration config_hi = { &config_desc_hi, settings_hi, interface_hi, endpoint_hi };
static SceUdcdConfiguration config_full = { &config_desc_full, settings_full, interface_full, endpoint_full };

static SceUdcdDeviceDescriptor device_hi = {
	USB_DT_DEVICE_SIZE, USB_DT_DEVICE, 0x0200, 0, 0, 0, 64, 0, 0, 0x0160, 0, 2, 0, 1
};
static SceUdcdDeviceDescriptor device_full = {
	USB_DT_DEVICE_SIZE, USB_DT_DEVICE, 0x0200, 0, 0, 0, 64, 0, 0, 0x0160, 0, 2, 0, 1
};

// "VitaPad" as a UTF-16 string descriptor
static SceUdcdStringDescriptor string_product = {
	2 + 7 * 2, USB_DT_STRING, { 'V', 'i', 't', 'a', 'P', 'a', 'd' }
};
// String 0 lists the supported languages: US English
static SceUdcdStringDescriptor string_languages = {
	4, USB_DT_STRING, { 0x0409 }
};
static SceUdcdStringDescriptor string_default = {
	2 + 7 * 2, USB_DT_STRING, { 'V', 'i', 't', 'a', 'P', 'a', 'd' }
};

// Report buffers: the one being sent must stay untouched until the transfer completes
static unsigned char current_report[USB_REPORT_MAX];
static unsigned char send_buffer[USB_REPORT_MAX] __attribute__((aligned(64)));
// Data the host sends us on the control endpoint (SET_REPORT: rumble, light bar...), ignored
static unsigned char receive_buffer[USB_REPORT_MAX] __attribute__((aligned(64)));
// Control answers are copied here: big enough for every descriptor we send
static unsigned char descriptor_buffer[256] __attribute__((aligned(64)));

// An answer shorter than requested that ends on a full 64-byte packet needs a zero-length packet to
// tell the host it's complete; our answers never do, so none is needed. Keep it that way.
_Static_assert(sizeof(report_descriptor_generic) % 64 != 0, "report descriptor size must not be a multiple of 64");
_Static_assert(sizeof(report_descriptor_generic) <= 256, "report descriptor must fit descriptor_buffer");
_Static_assert(sizeof(report_descriptor_ds4) % 64 != 0, "report descriptor size must not be a multiple of 64");
_Static_assert(sizeof(report_descriptor_ds4) <= 256, "report descriptor must fit descriptor_buffer");
_Static_assert(sizeof(ds4_calibration) % 64 != 0 && sizeof(ds4_pairing) % 64 != 0 && sizeof(ds4_firmware) % 64 != 0,
	"feature report sizes must not be a multiple of 64");

static SceUID event_flag = -1;
static SceUID thread = -1;
static volatile int registered = 0;
static volatile int active = 0;
static volatile int connected = 0;
static volatile uint64_t last_update = 0;
static int mode = USB_MODE_GENERIC;

static const unsigned char *report_descriptor(void){
	return mode == USB_MODE_DS4 ? report_descriptor_ds4 : report_descriptor_generic;
}

static int report_descriptor_size(void){
	return mode == USB_MODE_DS4 ? sizeof(report_descriptor_ds4) : sizeof(report_descriptor_generic);
}

static int report_size(void){
	return mode == USB_MODE_DS4 ? sizeof(Ds4Report) : sizeof(UsbGamepadReport);
}

// Nothing pressed, sticks centered
static void neutral_report(unsigned char *buffer){
	memset(buffer, 0, USB_REPORT_MAX);
	if (mode == USB_MODE_DS4){
		Ds4Report *report = (Ds4Report *)buffer;
		report->report_id = 0x01;
		report->lx = report->ly = report->rx = report->ry = 128;
		report->buttons[0] = DS4_HAT_NEUTRAL;
		report->accel[1] = 8192; // Lying flat
		report->status[0] = 0x10 | 10; // Cable connected, battery full
		report->num_touch_reports = 1;
		for (int i = 0; i < 3; i++) report->touch[i].points[0].contact = report->touch[i].points[1].contact = 0x80;
	}else{
		UsbGamepadReport *report = (UsbGamepadReport *)buffer;
		report->hat = USB_HAT_NEUTRAL;
		report->lx = report->ly = report->rx = report->ry = 128;
	}
}

// Sends data on the control endpoint in answer to a request, at most wLength bytes
static int send_control(const void *data, int size, int max){
	static SceUdcdDeviceRequest req;
	if (size > max) size = max;
	if (size > (int)sizeof(descriptor_buffer)) size = sizeof(descriptor_buffer);
	memcpy(descriptor_buffer, data, size);
	ksceKernelDcacheCleanRange(descriptor_buffer, sizeof(descriptor_buffer));
	memset(&req, 0, sizeof(req));
	req.endpoint = &endpoints[0];
	req.data = descriptor_buffer;
	req.size = size;
	return ksceUdcdReqSend(&req);
}

static void on_report_sent(SceUdcdDeviceRequest *req){
	ksceKernelSetEventFlag(event_flag, EVF_SENT);
}

static int send_report(void){
	static SceUdcdDeviceRequest req;
	int stale = ksceKernelGetSystemTimeWide() - last_update > APP_TIMEOUT_US;
	if (stale) neutral_report(send_buffer);
	else memcpy(send_buffer, current_report, sizeof(send_buffer));
	ksceKernelDcacheCleanRange(send_buffer, sizeof(send_buffer));
	memset(&req, 0, sizeof(req));
	req.endpoint = &endpoints[1];
	req.data = send_buffer;
	req.size = report_size();
	req.onComplete = on_report_sent;
	return ksceUdcdReqSend(&req);
}

static int process_request(int recipient, int arg, SceUdcdEP0DeviceRequest *req, void *user_data){
	if (arg < 0) return -1;
	int dir = req->bmRequestType & USB_CTRLTYPE_DIR_MASK;
	int type = req->bmRequestType & USB_CTRLTYPE_TYPE_MASK;
	int rec = req->bmRequestType & USB_CTRLTYPE_REC_MASK;
	int descriptor = req->wValue >> 8;

	if (dir == USB_CTRLTYPE_DIR_DEVICE2HOST){
		if (type == USB_CTRLTYPE_TYPE_STANDARD && req->bRequest == USB_REQ_GET_DESCRIPTOR){
			if (rec == USB_CTRLTYPE_REC_DEVICE && descriptor == USB_DT_STRING){
				if ((req->wValue & 0xFF) == 0) return send_control(&string_languages, string_languages.bLength, req->wLength);
				return send_control(&string_product, string_product.bLength, req->wLength);
			}
			if (rec == USB_CTRLTYPE_REC_INTERFACE && descriptor == HID_DESCRIPTOR_REPORT)
				return send_control(report_descriptor(), report_descriptor_size(), req->wLength);
			if (rec == USB_CTRLTYPE_REC_INTERFACE && descriptor == HID_DESCRIPTOR_HID)
				return send_control(hid_descriptor, sizeof(hid_descriptor), req->wLength);
		}
		if (type == USB_CTRLTYPE_TYPE_CLASS && rec == USB_CTRLTYPE_REC_INTERFACE && req->bRequest == HID_REQUEST_GET_REPORT){
			int report_type = req->wValue >> 8; // 1 = input, 3 = feature
			int report_id = req->wValue & 0xFF;
			if (report_type == 1) return send_control(current_report, report_size(), req->wLength);
			if (report_type == 3 && mode == USB_MODE_DS4){
				if (report_id == 0x02) return send_control(ds4_calibration, sizeof(ds4_calibration), req->wLength);
				if (report_id == 0x12) return send_control(ds4_pairing, sizeof(ds4_pairing), req->wLength);
				if (report_id == 0xA3) return send_control(ds4_firmware, sizeof(ds4_firmware), req->wLength);
			}
		}
	}else{
		// SET_IDLE / SET_PROTOCOL / SET_REPORT (rumble, light bar, LEDs): nothing to change, accept them.
		// When the request carries data we must still receive it, or the host waits for us forever.
		if (req->wLength > 0){
			static SceUdcdDeviceRequest recv;
			memset(&recv, 0, sizeof(recv));
			recv.endpoint = &endpoints[0];
			recv.data = receive_buffer;
			recv.size = req->wLength > sizeof(receive_buffer) ? sizeof(receive_buffer) : req->wLength;
			return ksceUdcdReqRecv(&recv);
		}
		return 0;
	}
	return -1;
}

static int change_setting(int interfaceNumber, int alternateSetting, int bus){
	return 0;
}

static int attach(int usb_version, void *user_data){
	ksceUdcdReqCancelAll(&endpoints[1]);
	ksceUdcdClearFIFO(&endpoints[1]);
	ksceKernelSetEventFlag(event_flag, EVF_CONNECTED);
	return 0;
}

static void detach(void *user_data){
	ksceKernelSetEventFlag(event_flag, EVF_DISCONNECTED);
}

static void configure(int usb_version, int desc_count, SceUdcdInterfaceSettings *settings, void *user_data){
}

static int driver_start(int size, void *args, void *user_data){
	return 0;
}

static int driver_stop(int size, void *args, void *user_data){
	return 0;
}

static SceUdcdDriver driver = {
	DRIVER_NAME,
	2,
	endpoints,
	interfaces,
	&device_hi,
	&config_hi,
	&device_full,
	&config_full,
	&string_default,
	&string_product,
	&string_default,
	process_request,
	change_setting,
	attach,
	detach,
	configure,
	driver_start,
	driver_stop,
	NULL,
	0,
	NULL
};

// Gives the USB port back to the system's normal driver (file transfer / charging)
static void restore_system_usb(void){
	ksceUdcdDeactivate();
	ksceUdcdStop(DRIVER_NAME, 0, NULL);
	ksceUdcdStop("USBDeviceControllerDriver", 0, NULL);
	ksceUdcdStart("USBDeviceControllerDriver", 0, NULL);
	ksceUdcdStart("USB_MTP_Driver", 0, NULL);
	ksceUdcdActivate(MTP_PRODUCT_ID);
}

static int start_usb(int new_mode){
	if (!registered) return -1;
	if (active) return 0;
	mode = new_mode == USB_MODE_DS4 ? USB_MODE_DS4 : USB_MODE_GENERIC;
	int size = report_descriptor_size();
	hid_descriptor[7] = size & 0xFF;
	hid_descriptor[8] = size >> 8;
	neutral_report(current_report);
	int ret = ksceUdcdDeactivate();
	if (ret < 0 && ret != SCE_UDCD_ERROR_INVALID_ARGUMENT) return ret;
	// Take the port from whatever the system is using it for
	ksceUdcdStop("USB_MTP_Driver", 0, NULL);
	ksceUdcdStop("USBPSPCommunicationDriver", 0, NULL);
	ksceUdcdStop("USBSerDriver", 0, NULL);
	ksceUdcdStop("USBDeviceControllerDriver", 0, NULL);
	ret = ksceUdcdStart("USBDeviceControllerDriver", 0, NULL);
	if (ret < 0) goto fail;
	ret = ksceUdcdStart(DRIVER_NAME, 0, NULL);
	if (ret < 0) goto fail;
	ret = ksceUdcdActivate(mode == USB_MODE_DS4 ? DS4_PRODUCT_ID : GENERIC_PRODUCT_ID);
	if (ret < 0) goto fail;
	last_update = ksceKernelGetSystemTimeWide();
	active = 1;
	return 0;
fail:
	restore_system_usb();
	return ret;
}

static void stop_usb(void){
	if (!active) return;
	active = 0;
	connected = 0;
	restore_system_usb();
}

static int usb_thread(SceSize args, void *argp){
	for (;;){
		unsigned int bits = 0;
		SceUInt timeout = 500 * 1000;
		int ret = ksceKernelWaitEventFlagCB(event_flag, EVF_ALL, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &bits, &timeout);
		if (ret >= 0 && (bits & EVF_EXIT)) break;
		if (ret >= 0 && (bits & EVF_CONNECTED)){
			connected = 1;
			send_report();
		}
		if (ret >= 0 && (bits & EVF_DISCONNECTED)) connected = 0;
		if (ret >= 0 && (bits & EVF_SENT) && connected) send_report();
		// VitaPad closed or suspended: give USB back to the system
		if (active && ksceKernelGetSystemTimeWide() - last_update > APP_TIMEOUT_US) stop_usb();
	}
	return 0;
}

int usb_module_start(void){
	neutral_report(current_report);
	event_flag = ksceKernelCreateEventFlag("vitapad_usb_evf", 0, 0, NULL);
	if (event_flag < 0) return event_flag;
	thread = ksceKernelCreateThread("vitapad_usb_thread", usb_thread, 0x3C, 0x1000, 0, SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, NULL);
	if (thread < 0) return thread;
	if (ksceUdcdRegister(&driver) >= 0) registered = 1;
	return ksceKernelStartThread(thread, 0, NULL);
}

void usb_module_stop(void){
	stop_usb();
	if (event_flag >= 0) ksceKernelSetEventFlag(event_flag, EVF_EXIT);
	if (thread >= 0){
		ksceKernelWaitThreadEnd(thread, NULL, NULL);
		ksceKernelDeleteThread(thread);
	}
	if (event_flag >= 0) ksceKernelDeleteEventFlag(event_flag);
	if (registered) ksceUdcdUnregister(&driver);
}

// Syscalls

int vitapadKernelUsbStart(int new_mode){
	uint32_t state;
	ENTER_SYSCALL(state);
	int ret = start_usb(new_mode);
	EXIT_SYSCALL(state);
	return ret;
}

int vitapadKernelUsbStop(void){
	uint32_t state;
	ENTER_SYSCALL(state);
	stop_usb();
	EXIT_SYSCALL(state);
	return 0;
}

// size must be the report size of the current mode
int vitapadKernelUsbSetReport(const void *report, unsigned int size){
	uint32_t state;
	unsigned char copy[USB_REPORT_MAX];
	ENTER_SYSCALL(state);
	int ret = -1;
	if (size == (unsigned int)report_size()){
		ret = ksceKernelMemcpyUserToKernel(copy, report, size);
		if (ret >= 0){
			memcpy(current_report, copy, size);
			last_update = ksceKernelGetSystemTimeWide();
		}
	}
	EXIT_SYSCALL(state);
	return ret;
}

// 0 = USB mode off, 1 = on but no computer yet, 2 = connected to a computer
int vitapadKernelUsbGetState(void){
	return active ? (connected ? 2 : 1) : 0;
}

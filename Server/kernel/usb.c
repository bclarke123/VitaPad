// USB mode: the Vita shows up on the computer as a USB HID gamepad, no PC client needed.
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
#define USB_PRODUCT_ID 0x5650 // Arbitrary, the vendor ID is filled in by the system
#define MTP_PRODUCT_ID 0x04E4 // What the Vita normally presents (media transfer)

// Without updates from the app for this long, the gamepad goes neutral and normal USB comes back
#define APP_TIMEOUT_US (2 * 1000 * 1000)

#define EVF_CONNECTED    (1 << 0)
#define EVF_DISCONNECTED (1 << 1)
#define EVF_SENT         (1 << 2)
#define EVF_EXIT         (1 << 3)
#define EVF_ALL          (EVF_CONNECTED | EVF_DISCONNECTED | EVF_SENT | EVF_EXIT)

// Gamepad: 14 buttons (DualShock 4 DirectInput order), a hat switch, 4 axes (sticks)
static unsigned char report_descriptor[] = {
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

// HID class descriptor, placed after the interface descriptor
static unsigned char hid_descriptor[] = {
	0x09,                               // bLength
	HID_DESCRIPTOR_HID,                 // bDescriptorType
	0x11, 0x01,                         // bcdHID 1.11
	0x00,                               // bCountryCode
	0x01,                               // bNumDescriptors
	HID_DESCRIPTOR_REPORT,              // bDescriptorType (report)
	sizeof(report_descriptor) & 0xFF,   // wDescriptorLength
	sizeof(report_descriptor) >> 8,
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
static UsbGamepadReport current_report;
static UsbGamepadReport send_buffer __attribute__((aligned(64)));
static UsbGamepadReport control_buffer __attribute__((aligned(64)));
// Control answers are copied here: big enough for every descriptor we send
static unsigned char descriptor_buffer[256] __attribute__((aligned(64)));

// An answer shorter than requested that ends on a full 64-byte packet needs a zero-length packet to
// tell the host it's complete; our answers never do, so none is needed. Keep it that way.
_Static_assert(sizeof(report_descriptor) % 64 != 0, "report descriptor size must not be a multiple of 64");
_Static_assert(sizeof(report_descriptor) <= 256, "report descriptor must fit descriptor_buffer");

static SceUID event_flag = -1;
static SceUID thread = -1;
static volatile int registered = 0;
static volatile int active = 0;
static volatile int connected = 0;
static volatile uint64_t last_update = 0;

static void neutral_report(UsbGamepadReport *report){
	memset(report, 0, sizeof(*report));
	report->hat = USB_HAT_NEUTRAL;
	report->lx = report->ly = report->rx = report->ry = 128;
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
	if (stale) neutral_report(&send_buffer);
	else memcpy(&send_buffer, &current_report, sizeof(send_buffer));
	ksceKernelDcacheCleanRange(&send_buffer, sizeof(send_buffer));
	memset(&req, 0, sizeof(req));
	req.endpoint = &endpoints[1];
	req.data = &send_buffer;
	req.size = sizeof(send_buffer);
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
				return send_control(report_descriptor, sizeof(report_descriptor), req->wLength);
			if (rec == USB_CTRLTYPE_REC_INTERFACE && descriptor == HID_DESCRIPTOR_HID)
				return send_control(hid_descriptor, sizeof(hid_descriptor), req->wLength);
		}
		if (type == USB_CTRLTYPE_TYPE_CLASS && rec == USB_CTRLTYPE_REC_INTERFACE && req->bRequest == HID_REQUEST_GET_REPORT){
			memcpy(&control_buffer, &current_report, sizeof(control_buffer));
			return send_control(&control_buffer, sizeof(control_buffer), req->wLength);
		}
	}else{
		// SET_IDLE / SET_PROTOCOL / SET_REPORT (e.g. keyboard LEDs): nothing to change, accept them
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

static int start_usb(void){
	if (!registered) return -1;
	if (active) return 0;
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
	ret = ksceUdcdActivate(USB_PRODUCT_ID);
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
	neutral_report(&current_report);
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

int vitapadKernelUsbStart(void){
	uint32_t state;
	ENTER_SYSCALL(state);
	int ret = start_usb();
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

int vitapadKernelUsbSetReport(const UsbGamepadReport *report){
	uint32_t state;
	UsbGamepadReport copy;
	ENTER_SYSCALL(state);
	int ret = ksceKernelMemcpyUserToKernel(&copy, report, sizeof(copy));
	if (ret >= 0){
		memcpy(&current_report, &copy, sizeof(copy));
		last_update = ksceKernelGetSystemTimeWide();
	}
	EXIT_SYSCALL(state);
	return ret;
}

// 0 = USB mode off, 1 = on but no computer yet, 2 = connected to a computer
int vitapadKernelUsbGetState(void){
	return active ? (connected ? 2 : 1) : 0;
}

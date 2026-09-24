#pragma once

#include "ViGEm/Client.h"

#include "main.h"

enum {
    VIGEM_DEVICE_NONE   = 0,
    VIGEM_DEVICE_DS4    = 1,
} VIGEM_DEVICE;

// What a Vita touch panel does on the emulated DualShock 4
enum {
    VIGEM_TOUCH_NONE           = 0, // Ignored
    VIGEM_TOUCH_BUTTONS        = 1, // Upper corners = L1/R1, lower corners = L3/R3
    VIGEM_TOUCH_TOUCHPAD       = 2, // DS4 touchpad
    VIGEM_TOUCH_TOUCHPAD_CLICK = 3, // Touching the panel clicks the DS4 touchpad button
};

typedef struct {
    unsigned int front_touch;  // One of VIGEM_TOUCH_*
    unsigned int rear_touch;   // One of VIGEM_TOUCH_*
    bool swap_shoulders;       // Vita L/R = L1/R1 and upper touch corners = L2/R2
    bool extended;             // Use extended reports (touchpad and motion, needs ViGEmBus 1.17+)
    bool motion;               // Send gyroscope and accelerometer data
} VigemOptions;

void vgDestroy();
bool vgInit();
bool vgSubmit(const PadPacketV2 *packet, const VigemOptions *options);

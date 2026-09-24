#include <Windows.h>
#include <stdio.h>
#include <string.h>

#include "ViGEm.h"

// DualShock 4 motion sensor resolution
#define DS4_GYRO_RES_PER_DEG_S 16.0f
#define DS4_ACCEL_RES_PER_G 8192.0f

// DualShock 4 touchpad resolution
#define DS4_TOUCHPAD_WIDTH 1920
#define DS4_TOUCHPAD_HEIGHT 943

PVIGEM_CLIENT client;
PVIGEM_TARGET target;
bool extended_supported = true;

void vgDestroy()
{
    if (client && target) vigem_target_remove(client, target);
    if (target) vigem_target_free(target);
    target = NULL;

    if (client)
    {
        vigem_disconnect(client);
        vigem_free(client);
    }
    client = NULL;
}

bool vgInit()
{
    do
    {
        if (NULL == (client = vigem_alloc())) break;
        if (!VIGEM_SUCCESS(vigem_connect(client))) break;
        if (NULL == (target = vigem_target_ds4_alloc())) break;
        if (!VIGEM_SUCCESS(vigem_target_add(client, target))) break;
        return true;
    } while (false);
    vgDestroy();
    return false;
}

static SHORT toShort(float value)
{
    if (value > 32767.0f) return 32767;
    if (value < -32768.0f) return -32768;
    return (SHORT)value;
}

// Upper corners = L1/R1 (L2/R2 when swapped), lower corners = L3/R3
static void touchToButtons(DS4_REPORT_EX *report, const TouchPoint *points, int num, bool swap_shoulders)
{
    for (int i = 0; i < num; i++)
    {
        bool left = points[i].x < TOUCH_WIDTH / 2;
        if (points[i].y < TOUCH_HEIGHT / 2)
        {
            if (swap_shoulders)
            {
                report->Report.wButtons |= left ? DS4_BUTTON_TRIGGER_LEFT : DS4_BUTTON_TRIGGER_RIGHT;
                if (left) report->Report.bTriggerL = 0xFF;
                else report->Report.bTriggerR = 0xFF;
            }
            else
            {
                report->Report.wButtons |= left ? DS4_BUTTON_SHOULDER_LEFT : DS4_BUTTON_SHOULDER_RIGHT;
            }
        }
        else
        {
            report->Report.wButtons |= left ? DS4_BUTTON_THUMB_LEFT : DS4_BUTTON_THUMB_RIGHT;
        }
    }
}

static void setTouch(BYTE *isUpTrackingNum, BYTE *data, const TouchPoint *point)
{
    if (point == NULL)
    {
        // Finger lifted: keep the last tracking number and position
        *isUpTrackingNum |= 0x80;
        return;
    }
    unsigned int x = point->x * (DS4_TOUCHPAD_WIDTH - 1) / (TOUCH_WIDTH - 1);
    unsigned int y = point->y * (DS4_TOUCHPAD_HEIGHT - 1) / (TOUCH_HEIGHT - 1);
    *isUpTrackingNum = point->id & 0x7F;
    data[0] = x & 0xFF;
    data[1] = ((x >> 8) & 0x0F) | ((y & 0x0F) << 4);
    data[2] = (y >> 4) & 0xFF;
}

static void setTouchpad(DS4_REPORT_EX *report, const PadPacketV2 *packet, const VigemOptions *options)
{
    static DS4_TOUCH touch = { 0, 0x80, { 0 }, 0x80, { 0 } };

    // Front panel fingers first, then rear panel ones, up to the 2 fingers a DS4 touchpad supports
    const TouchPoint *points[2] = { NULL, NULL };
    int num = 0;
    if (options->front_touch == VIGEM_TOUCH_TOUCHPAD)
        for (int i = 0; i < packet->front_num && num < 2; i++) points[num++] = &packet->front[i];
    if (options->rear_touch == VIGEM_TOUCH_TOUCHPAD)
        for (int i = 0; i < packet->rear_num && num < 2; i++) points[num++] = &packet->rear[i];

    touch.bPacketCounter++;
    setTouch(&touch.bIsUpTrackingNum1, touch.bTouchData1, points[0]);
    setTouch(&touch.bIsUpTrackingNum2, touch.bTouchData2, points[1]);
    report->Report.bTouchPacketsN = 1;
    report->Report.sCurrentTouch = touch;
}

static void setMotion(DS4_REPORT_EX *report, const PadPacketV2 *packet)
{
    // Vita axes: X right, Y towards the top of the screen, Z out of the screen.
    // DS4 axes: X right, Y out of the face, Z towards the player.
    // The Vita reports gravity in G and rotation in revolutions per second,
    // the DS4 reports the reaction to gravity and rotation in degrees per second.
    const float gyro = 360.0f * DS4_GYRO_RES_PER_DEG_S;
    report->Report.wGyroX = toShort(packet->gyro[0] * gyro);
    report->Report.wGyroY = toShort(packet->gyro[2] * gyro);
    report->Report.wGyroZ = toShort(-packet->gyro[1] * gyro);
    report->Report.wAccelX = toShort(-packet->accel[0] * DS4_ACCEL_RES_PER_G);
    report->Report.wAccelY = toShort(-packet->accel[2] * DS4_ACCEL_RES_PER_G);
    report->Report.wAccelZ = toShort(packet->accel[1] * DS4_ACCEL_RES_PER_G);
}

bool vgSubmit(const PadPacketV2 *packet, const VigemOptions *options)
{
    DS4_REPORT_EX report;
    memset(&report, 0, sizeof(report));

    report.Report.bThumbLX = packet->lx;
    report.Report.bThumbLY = packet->ly;
    report.Report.bThumbRX = packet->rx;
    report.Report.bThumbRY = packet->ry;

    if (packet->buttons & SCE_CTRL_SELECT)
    {
        report.Report.wButtons |= DS4_BUTTON_SHARE;
    }
    if (packet->buttons & SCE_CTRL_START)
    {
        report.Report.wButtons |= DS4_BUTTON_OPTIONS;
    }
    if (packet->buttons & SCE_CTRL_LTRIGGER)
    {
        if (options->swap_shoulders)
        {
            report.Report.wButtons |= DS4_BUTTON_SHOULDER_LEFT;
        }
        else
        {
            report.Report.wButtons |= DS4_BUTTON_TRIGGER_LEFT;
            report.Report.bTriggerL = 0xFF;
        }
    }
    if (packet->buttons & SCE_CTRL_RTRIGGER)
    {
        if (options->swap_shoulders)
        {
            report.Report.wButtons |= DS4_BUTTON_SHOULDER_RIGHT;
        }
        else
        {
            report.Report.wButtons |= DS4_BUTTON_TRIGGER_RIGHT;
            report.Report.bTriggerR = 0xFF;
        }
    }
    if (packet->buttons & SCE_CTRL_TRIANGLE)
    {
        report.Report.wButtons |= DS4_BUTTON_TRIANGLE;
    }
    if (packet->buttons & SCE_CTRL_CIRCLE)
    {
        report.Report.wButtons |= DS4_BUTTON_CIRCLE;
    }
    if (packet->buttons & SCE_CTRL_CROSS)
    {
        report.Report.wButtons |= DS4_BUTTON_CROSS;
    }
    if (packet->buttons & SCE_CTRL_SQUARE)
    {
        report.Report.wButtons |= DS4_BUTTON_SQUARE;
    }

    if (options->front_touch == VIGEM_TOUCH_BUTTONS)
        touchToButtons(&report, packet->front, packet->front_num, options->swap_shoulders);
    if (options->rear_touch == VIGEM_TOUCH_BUTTONS)
        touchToButtons(&report, packet->rear, packet->rear_num, options->swap_shoulders);
    if ((options->front_touch == VIGEM_TOUCH_TOUCHPAD_CLICK && packet->front_num > 0) ||
        (options->rear_touch == VIGEM_TOUCH_TOUCHPAD_CLICK && packet->rear_num > 0))
    {
        report.Report.bSpecial |= DS4_SPECIAL_BUTTON_TOUCHPAD;
    }

    DS4_DPAD_DIRECTIONS dpad = DS4_BUTTON_DPAD_NONE;
    if (packet->buttons & SCE_CTRL_UP) dpad = DS4_BUTTON_DPAD_NORTH;
    if (packet->buttons & SCE_CTRL_RIGHT) dpad = DS4_BUTTON_DPAD_EAST;
    if (packet->buttons & SCE_CTRL_DOWN) dpad = DS4_BUTTON_DPAD_SOUTH;
    if (packet->buttons & SCE_CTRL_LEFT) dpad = DS4_BUTTON_DPAD_WEST;

    if (packet->buttons & SCE_CTRL_UP
        && packet->buttons & SCE_CTRL_RIGHT) dpad = DS4_BUTTON_DPAD_NORTHEAST;
    if (packet->buttons & SCE_CTRL_RIGHT
        && packet->buttons & SCE_CTRL_DOWN) dpad = DS4_BUTTON_DPAD_SOUTHEAST;
    if (packet->buttons & SCE_CTRL_DOWN
        && packet->buttons & SCE_CTRL_LEFT) dpad = DS4_BUTTON_DPAD_SOUTHWEST;
    if (packet->buttons & SCE_CTRL_LEFT
        && packet->buttons & SCE_CTRL_UP) dpad = DS4_BUTTON_DPAD_NORTHWEST;
    report.Report.wButtons = (report.Report.wButtons & ~0xF) | (USHORT)dpad;

    if (options->extended && extended_supported)
    {
        // DS4 timestamps are in units of 16/3 microseconds
        report.Report.wTimestamp = (USHORT)(((uint64_t)packet->timestamp * 3 / 16) & 0xFFFF);
        report.Report.bBatteryLvl = packet->battery * 0xFF / 100;
        report.Report.bBatteryLvlSpecial = packet->battery / 10;
        if (options->motion) setMotion(&report, packet);
        setTouchpad(&report, packet, options);

        VIGEM_ERROR err = vigem_target_ds4_update_ex(client, target, report);
        if (err != VIGEM_ERROR_NOT_SUPPORTED) return VIGEM_SUCCESS(err);

        printf("\nWARNING: Your ViGEmBus driver is too old for touchpad and motion support, please update it to v1.17 or newer.\n");
        extended_supported = false;
    }

    DS4_REPORT basic;
    memcpy(&basic, &report.Report, sizeof(DS4_REPORT));
    return VIGEM_SUCCESS(vigem_target_ds4_update(client, target, basic));
}

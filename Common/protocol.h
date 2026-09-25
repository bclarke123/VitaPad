#pragma once

// Network protocol shared by the VitaPad server (PSVITA) and client (PC).
// Keep this file compatible with both the Vita toolchain (C) and the PC clients (C++).

#include <stdint.h>

#define GAMEPAD_PORT 5000 // TCP, input polling
#define DISCOVERY_PORT 5000 // UDP, automatic discovery of the Vita on the LAN
#define STREAM_PORT 5001 // UDP, input streaming (see below)

// Client -> server discovery request (UDP broadcast) and server reply
#define DISCOVERY_REQUEST "VITAPAD_DISCOVER"
#define DISCOVERY_REPLY "VITAPAD_HERE"

// Every poll the client sends an 8 byte request. Its content selects the reply format:
// - "request\0" (legacy clients) -> PadPacket
// - "VPAD2\0\0\0"                -> PadPacketV2
#define REQUEST_SIZE 8
#define REQUEST_V2 "VPAD2\0\0"

// Touch coordinates in PadPacketV2 are normalized to this range for both panels
#define TOUCH_WIDTH 1920
#define TOUCH_HEIGHT 1088

// Values for PadPacket.click
#define NO_INPUT 0x00
#define MOUSE_MOV 0x01
#define LEFT_CLICK 0x08
#define RIGHT_CLICK 0x10

// Legacy packet (v1)
typedef struct {
	uint32_t buttons;
	uint8_t lx;
	uint8_t ly;
	uint8_t rx;
	uint8_t ry;
	uint16_t tx;
	uint16_t ty;
	uint8_t click;
} PadPacket, *pPadPacket;

#pragma pack(push, 1)

typedef struct {
	uint16_t x; // 0 - (TOUCH_WIDTH - 1)
	uint16_t y; // 0 - (TOUCH_HEIGHT - 1)
	uint8_t id; // Changes every time a new finger touches the panel
	uint8_t reserved;
} TouchPoint;

typedef struct {
	uint32_t buttons;
	uint8_t lx;
	uint8_t ly;
	uint8_t rx;
	uint8_t ry;
	uint8_t front_num; // Number of valid entries in front (0 - 2)
	uint8_t rear_num;  // Number of valid entries in rear (0 - 2)
	uint16_t reserved;
	TouchPoint front[2];
	TouchPoint rear[2];
	float accel[3];     // Vita accelerometer (gravity vector, in G)
	float gyro[3];      // Vita gyroscope (radians per second)
	uint32_t timestamp; // Microseconds
	uint8_t battery;    // Vita battery percentage
	uint8_t reserved2[3];
} PadPacketV2;

// Streaming (Vita app 1.9+): instead of waiting for polls, the Vita sends a StreamPacket over UDP as soon
// as there's new input (a button change, a new stick/touch/motion sample), and at least every
// STREAM_MAX_INTERVAL_MS. The client keeps its TCP connection (version check, keepalive polls) and
// sends STREAM_HELLO to STREAM_PORT every STREAM_HELLO_MS from its UDP socket; the Vita streams to
// the sender of the latest hello and stops STREAM_TIMEOUT_MS after the hellos stop.
// Vita apps without streaming don't answer: the client then polls over TCP as before.
#define STREAM_HELLO "VPAD_STREAM" // Sent with its NUL: 12 bytes
#define STREAM_MAGIC "VPS1"
#define STREAM_HELLO_MS 250
#define STREAM_TIMEOUT_MS 3000
#define STREAM_MAX_INTERVAL_MS 20

typedef struct {
	char magic[4];      // STREAM_MAGIC
	uint32_t seq;       // +1 every packet: the client drops late (out of order) packets
	PadPacketV2 pad;
} StreamPacket;

#pragma pack(pop)

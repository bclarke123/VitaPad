#include <vita2d.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/types.h>
#include <psp2/motion.h>
#include <psp2/power.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "protocol.h"
#include "remap.h"

#define NET_INIT_SIZE 1*1024*1024

// A client that stops polling for this long is considered gone
#define CLIENT_TIMEOUT (3 * 1000 * 1000)

// Hold this combo to turn the screen off/on
#define SCREEN_TOGGLE_COMBO (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT)
#define SCREEN_TOGGLE_FRAMES 60

// Hold this combo to open/close the button remapping menu
#define REMAP_MENU_COMBO (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_START)

volatile int connected = 0;
volatile uint8_t battery = 100;

static SceTouchPanelInfo panel_info[2];

static int recv_all(int s, void *buf, unsigned int len){
	uint8_t *p = (uint8_t *)buf;
	while (len > 0){
		int ret = sceNetRecv(s, p, len, 0);
		if (ret <= 0) return -1;
		p += ret;
		len -= ret;
	}
	return 0;
}

static int send_all(int s, const void *buf, unsigned int len){
	const uint8_t *p = (const uint8_t *)buf;
	while (len > 0){
		int ret = sceNetSend(s, p, len, 0);
		if (ret <= 0) return -1;
		p += ret;
		len -= ret;
	}
	return 0;
}

// Maps a touch report to the TOUCH_WIDTH x TOUCH_HEIGHT range
static void fill_touch(TouchPoint *out, const SceTouchReport *report, const SceTouchPanelInfo *info){
	int x = report->x - info->minAaX;
	int y = report->y - info->minAaY;
	int w = info->maxAaX - info->minAaX;
	int h = info->maxAaY - info->minAaY;
	if (w > 0) x = x * (TOUCH_WIDTH - 1) / w;
	if (h > 0) y = y * (TOUCH_HEIGHT - 1) / h;
	if (x < 0) x = 0;
	if (x > TOUCH_WIDTH - 1) x = TOUCH_WIDTH - 1;
	if (y < 0) y = 0;
	if (y > TOUCH_HEIGHT - 1) y = TOUCH_HEIGHT - 1;
	out->x = x;
	out->y = y;
	out->id = report->id;
	out->reserved = 0;
}

static void fill_packet(PadPacket *pkg){
	SceCtrlData pad;
	SceTouchData front, retro;
	sceCtrlPeekBufferPositive(0, &pad, 1);
	sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1);
	sceTouchPeek(SCE_TOUCH_PORT_BACK, &retro, 1);
	// While the remap menu is open the PC gets a neutral pad
	if (remap_menu_open){
		memset(pkg, 0, sizeof(PadPacket));
		pkg->lx = pkg->ly = pkg->rx = pkg->ry = 128;
		return;
	}
	memcpy(pkg, &pad.buttons, 8); // Buttons + analogs state
	pkg->buttons = remap_buttons(pad.buttons);
	pkg->tx = front.report[0].x;
	pkg->ty = front.report[0].y;
	uint8_t flags = NO_INPUT;
	if (front.reportNum > 0) flags += MOUSE_MOV;
	if (retro.reportNum > 0){
		if (retro.report[0].x > 960) flags += RIGHT_CLICK;
		else flags += LEFT_CLICK;
	}
	pkg->click = flags;
}

static void fill_packet_v2(PadPacketV2 *pkg){
	SceCtrlData pad;
	SceTouchData front, retro;
	SceMotionSensorState motion;
	sceCtrlPeekBufferPositive(0, &pad, 1);
	sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1);
	sceTouchPeek(SCE_TOUCH_PORT_BACK, &retro, 1);
	memset(pkg, 0, sizeof(PadPacketV2));
	pkg->timestamp = (uint32_t)sceKernelGetProcessTimeWide();
	pkg->battery = battery;
	// While the remap menu is open the PC gets a neutral pad
	if (remap_menu_open){
		pkg->lx = pkg->ly = pkg->rx = pkg->ry = 128;
		return;
	}
	pkg->buttons = remap_buttons(pad.buttons);
	pkg->lx = pad.lx;
	pkg->ly = pad.ly;
	pkg->rx = pad.rx;
	pkg->ry = pad.ry;
	pkg->front_num = front.reportNum > 2 ? 2 : front.reportNum;
	pkg->rear_num = retro.reportNum > 2 ? 2 : retro.reportNum;
	for (int i = 0; i < pkg->front_num; i++) fill_touch(&pkg->front[i], &front.report[i], &panel_info[SCE_TOUCH_PORT_FRONT]);
	for (int i = 0; i < pkg->rear_num; i++) fill_touch(&pkg->rear[i], &retro.report[i], &panel_info[SCE_TOUCH_PORT_BACK]);
	if (sceMotionGetSensorState(&motion, 1) == 0){
		pkg->accel[0] = motion.accelerometer.x;
		pkg->accel[1] = motion.accelerometer.y;
		pkg->accel[2] = motion.accelerometer.z;
		pkg->gyro[0] = motion.gyro.x;
		pkg->gyro[1] = motion.gyro.y;
		pkg->gyro[2] = motion.gyro.z;
	}
}

static void handle_client(int client){
	int one = 1;
	int timeout = CLIENT_TIMEOUT;
	sceNetSetsockopt(client, SCE_NET_IPPROTO_TCP, SCE_NET_TCP_NODELAY, &one, sizeof(one));
	sceNetSetsockopt(client, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &timeout, sizeof(timeout));
	sceNetSetsockopt(client, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &timeout, sizeof(timeout));

	connected = 1;
	char request[REQUEST_SIZE];
	PadPacket pkg;
	PadPacketV2 pkg_v2;
	for (;;){
		if (recv_all(client, request, REQUEST_SIZE) < 0) break;
		int ret;
		if (memcmp(request, REQUEST_V2, REQUEST_SIZE) == 0){
			fill_packet_v2(&pkg_v2);
			ret = send_all(client, &pkg_v2, sizeof(PadPacketV2));
		}else{
			fill_packet(&pkg);
			ret = send_all(client, &pkg, sizeof(PadPacket));
		}
		if (ret < 0) break;
	}
	connected = 0;
}

static int create_socket(int type, int protocol, int port){
	int fd = sceNetSocket("VitaPad", SCE_NET_AF_INET, type, protocol);
	if (fd < 0) return fd;
	int one = 1;
	sceNetSetsockopt(fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR, &one, sizeof(one));
	SceNetSockaddrIn addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = SCE_NET_AF_INET;
	addr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
	addr.sin_port = sceNetHtons(port);
	if (sceNetBind(fd, (SceNetSockaddr *)&addr, sizeof(addr)) < 0){
		sceNetSocketClose(fd);
		return -1;
	}
	return fd;
}

// Server thread
static int server_thread(unsigned int args, void* argp){
	for (;;){
		int fd = create_socket(SCE_NET_SOCK_STREAM, 0, GAMEPAD_PORT);
		if (fd < 0 || sceNetListen(fd, 128) < 0){
			if (fd >= 0) sceNetSocketClose(fd);
			sceKernelDelayThread(1000 * 1000);
			continue;
		}
		for (;;){
			SceNetSockaddrIn clientaddr;
			unsigned int addrlen = sizeof(clientaddr);
			int client = sceNetAccept(fd, (SceNetSockaddr *)&clientaddr, &addrlen);
			// The listening socket can break (e.g. after the Vita resumes from sleep), recreate it
			if (client < 0) break;
			handle_client(client);
			sceNetSocketClose(client);
		}
		sceNetSocketClose(fd);
		sceKernelDelayThread(500 * 1000);
	}
	return 0;
}

// Answers discovery broadcasts so the PC client can find the Vita without typing its IP
static int discovery_thread(unsigned int args, void* argp){
	char buf[64];
	for (;;){
		int fd = create_socket(SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP, DISCOVERY_PORT);
		if (fd < 0){
			sceKernelDelayThread(1000 * 1000);
			continue;
		}
		for (;;){
			SceNetSockaddrIn from;
			unsigned int fromlen = sizeof(from);
			int len = sceNetRecvfrom(fd, buf, sizeof(buf), 0, (SceNetSockaddr *)&from, &fromlen);
			if (len < 0) break;
			if (len >= (int)strlen(DISCOVERY_REQUEST) && memcmp(buf, DISCOVERY_REQUEST, strlen(DISCOVERY_REQUEST)) == 0)
				sceNetSendto(fd, DISCOVERY_REPLY, strlen(DISCOVERY_REPLY), 0, (SceNetSockaddr *)&from, fromlen);
		}
		sceNetSocketClose(fd);
		sceKernelDelayThread(500 * 1000);
	}
	return 0;
}

// Returns 1 and fills ip if the Vita is connected to a network
static int get_ip(char *ip, size_t size){
	int state;
	if (sceNetCtlInetGetState(&state) < 0 || state != SCE_NETCTL_STATE_CONNECTED) return 0;
	SceNetCtlInfo info;
	if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) < 0) return 0;
	snprintf(ip, size, "%s", info.ip_address);
	return 1;
}

vita2d_pgf* debug_font;

int main(){

	// Enabling analog, touch and motion support
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, 1);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, 1);
	sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &panel_info[SCE_TOUCH_PORT_FRONT]);
	sceTouchGetPanelInfo(SCE_TOUCH_PORT_BACK, &panel_info[SCE_TOUCH_PORT_BACK]);
	sceMotionStartSampling();
	remap_load();

	// Initializing graphics stuffs
	vita2d_init();
	vita2d_set_clear_color(RGBA8(0x00, 0x00, 0x00, 0xFF));
	debug_font = vita2d_load_default_pgf();
	uint32_t text_color = RGBA8(0xFF, 0xFF, 0xFF, 0xFF);

	// Initializing network stuffs
	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	int ret = sceNetShowNetstat();
	if (ret == SCE_NET_ERROR_ENOTINIT) {
		SceNetInitParam initparam;
		initparam.memory = malloc(NET_INIT_SIZE);
		initparam.size = NET_INIT_SIZE;
		initparam.flags = 0;
		sceNetInit(&initparam);
	}
	sceNetCtlInit();

	// Starting server threads
	SceUID thread = sceKernelCreateThread("VitaPad Thread", &server_thread, 0x10000100, 0x10000, 0, 0, NULL);
	sceKernelStartThread(thread, 0, NULL);
	SceUID discovery = sceKernelCreateThread("VitaPad Discovery Thread", &discovery_thread, 0x10000100, 0x4000, 0, 0, NULL);
	sceKernelStartThread(discovery, 0, NULL);

	char vita_ip[32];
	int has_ip = 0;
	int screen_off = 0;
	int combo_frames = 0;
	int menu_combo_frames = 0;
	unsigned int frame = 0;
	for (;;){

		// Refreshing IP and battery once per second, the Wi-Fi connection may not be up yet or may change
		if (frame % 60 == 0){
			has_ip = get_ip(vita_ip, sizeof(vita_ip));
			battery = scePowerGetBatteryLifePercent();
		}
		frame++;

		// Keeping the Vita from going to sleep (and dropping the connection) while a client is connected
		if (connected) sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);

		// Screen toggle
		SceCtrlData pad;
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if ((pad.buttons & SCREEN_TOGGLE_COMBO) == SCREEN_TOGGLE_COMBO){
			if (++combo_frames == SCREEN_TOGGLE_FRAMES) screen_off = !screen_off;
		}else combo_frames = 0;

		// Remap menu toggle
		if ((pad.buttons & REMAP_MENU_COMBO) == REMAP_MENU_COMBO){
			if (++menu_combo_frames == SCREEN_TOGGLE_FRAMES){
				if (remap_menu_open) remap_menu_open = 0;
				else {
					remap_menu_open_now();
					screen_off = 0;
				}
			}
		}else menu_combo_frames = 0;
		if (remap_menu_open) remap_menu_update(pad.buttons);

		vita2d_start_drawing();
		vita2d_clear_screen();
		if (remap_menu_open){
			remap_menu_draw(debug_font);
		}
		// With the screen off we draw a plain black frame: OLED pixels are off, so no burn-in
		else if (!screen_off){
			vita2d_pgf_draw_text(debug_font, 2, 20, text_color, 1.0, "VitaPad v.1.5 by Rinnegatamante");
			if (has_ip) vita2d_pgf_draw_textf(debug_font, 2, 60, text_color, 1.0, "Listening on:\nIP: %s\nPort: %d", vita_ip, GAMEPAD_PORT);
			else vita2d_pgf_draw_text(debug_font, 2, 60, text_color, 1.0, "Waiting for Wi-Fi connection...");
			vita2d_pgf_draw_textf(debug_font, 2, 200, text_color, 1.0, "Status: %s", connected ? "Connected!" : "Waiting connection...");
			vita2d_pgf_draw_text(debug_font, 2, 240, text_color, 1.0, "Hold L + R + SELECT for 1 second to turn the screen off/on");
			vita2d_pgf_draw_textf(debug_font, 2, 260, text_color, 1.0, "Hold L + R + START for 1 second to remap buttons (%d remapped)", remap_changed_count());
			vita2d_pgf_draw_textf(debug_font, 2, 300, text_color, 1.0, "Thanks to MakiseKurisu & yuntiancherry for the ViGEm client support");
			vita2d_pgf_draw_textf(debug_font, 2, 320, text_color, 1.0, "Thanks to Evengard for the vJoy client support");
			vita2d_pgf_draw_textf(debug_font, 2, 340, text_color, 1.0, "Thanks to nyorem for the Linux client port");
			vita2d_pgf_draw_textf(debug_font, 2, 380, text_color, 1.0, "Thanks to my distinguished Patroners for their awesome support:");
			vita2d_pgf_draw_textf(debug_font, 2, 400, text_color, 1.0, "@Sarkies_Proxy - ArkSource - Freddy Parra");
			vita2d_pgf_draw_textf(debug_font, 2, 420, text_color, 1.0, "RaveHeart - Tain Sueiras - drd7of14 - psymu");
			vita2d_pgf_draw_textf(debug_font, 2, 440, text_color, 1.0, "The Vita3K project - nullobject - polytoad");
		}
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();

	}

}

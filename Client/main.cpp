#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <vector>

#if !(defined(__WIN32__) || defined(__CYGWIN__) || defined(__linux__))
#  error "Your target system is not yet supported by VitaPad"
#endif

// Sockets
#ifdef __WIN32__
# include <winsock2.h>
# include <ws2tcpip.h>
typedef SOCKET sock_t;
# define close_socket closesocket
#else
# include <sys/socket.h>
# include <sys/select.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <netdb.h>
# include <arpa/inet.h>
# include <ifaddrs.h>
# include <net/if.h>
# include <signal.h>
typedef int sock_t;
# define INVALID_SOCKET (-1)
# define close_socket close
#endif

#include "tinyxml2.h"
#include "main.h"
#include "viewer.h"

// Input
#if defined(__WIN32__) || defined(__CYGWIN__)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  ifndef INPUT // Patch for old(?) MinGW installations
#    include <winable.h>
#    define KEYEVENTF_SCANCODE 0x0008
#  endif
#else
#  include <X11/Xlib.h>
#  include <X11/Xutil.h>
#  include <X11/extensions/XTest.h>
#endif

#ifdef __linux__
#define CONFIG_FILE "linux.xml"
#else
#define CONFIG_FILE "windows.xml"
#endif

// File where the IP of the last Vita we connected to is remembered
#define SAVED_IP_FILE "vita_ip.txt"

#define CONNECT_TIMEOUT_MS 2000
#define SOCKET_TIMEOUT_MS 3000
#define DISCOVERY_TIMEOUT_MS 1500

// Keys
uint16_t KEY_DOWN, KEY_UP, KEY_LEFT, KEY_RIGHT, KEY_TRIANGLE, KEY_SQUARE, KEY_CROSS, KEY_CIRCLE;
uint16_t KEY_L, KEY_R, KEY_START, KEY_SELECT, KEY_LANALOG_UP, KEY_LANALOG_DOWN, KEY_LANALOG_LEFT;
uint16_t KEY_LANALOG_RIGHT, KEY_RANALOG_UP, KEY_RANALOG_DOWN, KEY_RANALOG_LEFT, KEY_RANALOG_RIGHT;

// Mouse
#if defined(__WIN32__) || defined(__CYGWIN__)
#define MOUSE_LEFT_DOWN MOUSEEVENTF_LEFTDOWN
#define MOUSE_LEFT_UP MOUSEEVENTF_LEFTUP
#define MOUSE_RIGHT_DOWN MOUSEEVENTF_RIGHTDOWN
#define MOUSE_RIGHT_UP MOUSEEVENTF_RIGHTUP
#elif __linux__
#define MOUSE_LEFT_DOWN 0
#define MOUSE_LEFT_UP 1
#define MOUSE_RIGHT_DOWN 2
#define MOUSE_RIGHT_UP 3
#endif

#ifdef __linux__
Display* display;
#endif

// VJoy
#ifdef __WIN32__
# include "VJoySDK/inc/public.h"
# include "VJoySDK/inc/vjoyinterface.h"
bool VJOY_MODE = false;
bool VJOY_ALTERNATE = false;
int VJOY_DEVID = 0;
int VJOY_BUTTONS = 0;

#include "ViGEm.h"
unsigned int VIGEM_MODE = VIGEM_DEVICE_NONE;
VigemOptions VIGEM_OPTIONS = { VIGEM_TOUCH_BUTTONS, VIGEM_TOUCH_TOUCHPAD, false, true, true };

enum {
	VJOY_CTRL_SELECT     = 1 << 6,	//!< Select button.
	VJOY_CTRL_START      = 1 << 7,	//!< Start button.
	VJOY_CTRL_LBUMPER   = 1 << 4,	//!< Left bumper.
	VJOY_CTRL_RBUMPER   = 1 << 5,	//!< Right bumper.
	VJOY_CTRL_TRIANGLE   = 1 << 3,	//!< Triangle button.
	VJOY_CTRL_CIRCLE     = 1 << 1,	//!< Circle button.
	VJOY_CTRL_CROSS      = 1 << 0,	//!< Cross button.
	VJOY_CTRL_SQUARE     = 1 << 2,	//!< Square button.
	VJOY_CTRL_L3         = 1 << 8,	//!< Left stick click (needs a vJoy device with 10+ buttons).
	VJOY_CTRL_R3         = 1 << 9	//!< Right stick click (needs a vJoy device with 10+ buttons).
};
#endif

// VJoy implementation
#ifdef __WIN32__
void abortVjoy()
{
	VJOY_MODE = false;
	VJOY_DEVID = 0;
	printf("\nERROR: An error occurred while initializing VJOY. Reverting back to keybinds.\n");
}
void initVjoy()
{
	if (!vJoyEnabled())
	{
		abortVjoy();
		return;
	}
	for (UINT devId = 1; devId <= 16; devId++)
	{
		if (VJD_STAT_FREE == GetVJDStatus(devId))
		{
			if (8 > GetVJDButtonNumber(devId))
			{
				printf("VJOY: ID:%u Buttons number insuffisent.\n", devId);
				continue;
			}
			if (!GetVJDAxisExist(devId, HID_USAGE_X) ||
				!GetVJDAxisExist(devId, HID_USAGE_Y) ||
				!GetVJDAxisExist(devId, HID_USAGE_Z) ||
				!GetVJDAxisExist(devId, HID_USAGE_RX) ||
				!GetVJDAxisExist(devId, HID_USAGE_RY) ||
				!GetVJDAxisExist(devId, HID_USAGE_RZ) ||
				!GetVJDAxisExist(devId, HID_USAGE_SL0) ||
				!GetVJDAxisExist(devId, HID_USAGE_SL1) ||
				!GetVJDAxisExist(devId, HID_USAGE_WHL) ||
				!GetVJDAxisExist(devId, HID_USAGE_POV))
			{
				printf("VJOY: ID:%u Some axis not defined.\n", devId);
				continue;
			}
			VJOY_DEVID = devId;
			VJOY_BUTTONS = GetVJDButtonNumber(devId);
			break;
		}
		else
		{
			printf("VJOY: ID:%u Not ready, status:%u.\n", devId, GetVJDStatus(devId));
		}
	}

	if (0 == VJOY_DEVID || !AcquireVJD(VJOY_DEVID))
	{
		abortVjoy();
		return;
	}

	printf("Acquired ID:%u VJOY device.\n", VJOY_DEVID);
	if (VJOY_BUTTONS < 10)
		printf("VJOY: Configure the device with 10 buttons or more to use L3/R3.\n");
}
#endif

static uint16_t readKey(tinyxml2::XMLDocument& doc, const char* name, uint16_t value)
{
	tinyxml2::XMLElement* k1 = doc.FirstChildElement(name);
	if (k1 == NULL || k1->GetText() == NULL)
	{
		printf("\nWARNING: %s missing from config file.", name);
		return value;
	}
	return strtoul(k1->GetText(), NULL, 16);
}

#ifdef __WIN32__
static bool readBool(tinyxml2::XMLDocument& doc, const char* name, bool value)
{
	tinyxml2::XMLElement* k1 = doc.FirstChildElement(name);
	bool tmp_bool;
	if (NULL != k1 && tinyxml2::XML_NO_ERROR == k1->QueryBoolText(&tmp_bool)) return tmp_bool;
	return value;
}

static unsigned int readUnsigned(tinyxml2::XMLDocument& doc, const char* name, unsigned int value)
{
	tinyxml2::XMLElement* k1 = doc.FirstChildElement(name);
	unsigned int tmp_int;
	if (NULL != k1 && tinyxml2::XML_NO_ERROR == k1->QueryUnsignedText(&tmp_int)) return tmp_int;
	return value;
}
#endif

void loadConfig(const char* path)
{

	// Loading XML file
	tinyxml2::XMLDocument doc;
	if (doc.LoadFile(path) != tinyxml2::XML_NO_ERROR){
		printf("\nERROR: An error occurred while opening config file.");
		return;
	}

	// Getting elements, missing ones keep their current value
	KEY_DOWN = readKey(doc, "KEY_DOWN", KEY_DOWN);
	KEY_UP = readKey(doc, "KEY_UP", KEY_UP);
	KEY_LEFT = readKey(doc, "KEY_LEFT", KEY_LEFT);
	KEY_RIGHT = readKey(doc, "KEY_RIGHT", KEY_RIGHT);
	KEY_TRIANGLE = readKey(doc, "KEY_TRIANGLE", KEY_TRIANGLE);
	KEY_SQUARE = readKey(doc, "KEY_SQUARE", KEY_SQUARE);
	KEY_CROSS = readKey(doc, "KEY_CROSS", KEY_CROSS);
	KEY_CIRCLE = readKey(doc, "KEY_CIRCLE", KEY_CIRCLE);
	KEY_L = readKey(doc, "KEY_L", KEY_L);
	KEY_R = readKey(doc, "KEY_R", KEY_R);
	KEY_START = readKey(doc, "KEY_START", KEY_START);
	KEY_SELECT = readKey(doc, "KEY_SELECT", KEY_SELECT);
	KEY_LANALOG_UP = readKey(doc, "KEY_LANALOG_UP", KEY_LANALOG_UP);
	KEY_LANALOG_DOWN = readKey(doc, "KEY_LANALOG_DOWN", KEY_LANALOG_DOWN);
	KEY_LANALOG_LEFT = readKey(doc, "KEY_LANALOG_LEFT", KEY_LANALOG_LEFT);
	KEY_LANALOG_RIGHT = readKey(doc, "KEY_LANALOG_RIGHT", KEY_LANALOG_RIGHT);
	KEY_RANALOG_UP = readKey(doc, "KEY_RANALOG_UP", KEY_RANALOG_UP);
	KEY_RANALOG_DOWN = readKey(doc, "KEY_RANALOG_DOWN", KEY_RANALOG_DOWN);
	KEY_RANALOG_LEFT = readKey(doc, "KEY_RANALOG_LEFT", KEY_RANALOG_LEFT);
	KEY_RANALOG_RIGHT = readKey(doc, "KEY_RANALOG_RIGHT", KEY_RANALOG_RIGHT);

#ifdef __WIN32__
	if (readBool(doc, "VJOY_MODE", false)) VJOY_MODE = true;
	if (readBool(doc, "VJOY_ALTERNATE", false)) VJOY_ALTERNATE = true;
	unsigned int tmp_int = readUnsigned(doc, "VIGEM_MODE", 0);
	if (0 != tmp_int) VIGEM_MODE = tmp_int;
	VIGEM_OPTIONS.front_touch = readUnsigned(doc, "VIGEM_FRONT_TOUCH", VIGEM_OPTIONS.front_touch);
	VIGEM_OPTIONS.rear_touch = readUnsigned(doc, "VIGEM_REAR_TOUCH", VIGEM_OPTIONS.rear_touch);
	VIGEM_OPTIONS.swap_shoulders = readBool(doc, "VIGEM_SWAP_SHOULDERS", VIGEM_OPTIONS.swap_shoulders);
	VIGEM_OPTIONS.extended = readBool(doc, "VIGEM_EXTENDED", VIGEM_OPTIONS.extended);
	VIGEM_OPTIONS.motion = readBool(doc, "VIGEM_MOTION", VIGEM_OPTIONS.motion);
#endif

}

#if defined(__WIN32__) || defined(__CYGWIN__)
void SendMoveMouse(int x, int y){
	INPUT ip = { 0 };
	ip.type = INPUT_MOUSE;
	float x_molt = (1.0 * x)/SCREEN_WIDTH;
	float y_molt = (1.0 * y)/SCREEN_HEIGHT;
	ip.mi.dx = (x_molt*65535);
	ip.mi.dy = (y_molt*65535);
	ip.mi.mouseData = 0;
	ip.mi.time = 0;
	ip.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
	SendInput(1, &ip, sizeof(INPUT));
#elif __linux__
void SendMoveMouse (Display* display, int x, int y) {
    // Get current mouse position
    int mouse_x, mouse_y;
    int root_x, root_y, win_x, win_y;
    unsigned int mask_return;
    Window widow_returned;
    XQueryPointer(display, DefaultRootWindow (display),
                  &widow_returned, &widow_returned, &root_x, &root_y,
                  &win_x, &win_y, &mask_return);
    mouse_x = root_x;
    mouse_y = root_y;

    // Set mouse position
    XWarpPointer(display, None, None, 0, 0, 0, 0, x - mouse_x, y - mouse_y);
    XFlush(display);
#endif
}

#if defined(__WIN32__) || defined(__CYGWIN__)
#define SEND_MOVE_MOUSE(x,y) SendMoveMouse(x,y)
#elif __linux__
#define SEND_MOVE_MOUSE(...) SendMoveMouse(display, __VA_ARGS__)
#endif

#if defined(__WIN32__) || defined(__CYGWIN__)
void SendMouseEvent(uint16_t event){
	INPUT ip = { 0 };
	ip.type = INPUT_MOUSE;
	ip.mi.dx = 0;
	ip.mi.dy = 0;
	ip.mi.mouseData = 0;
	ip.mi.time = 0;
	ip.mi.dwFlags = event;
	SendInput(1, &ip, sizeof(INPUT));
#elif __linux__
void SendMouseEvent (Display* display, uint16_t event) {
    Bool down;
    int button;

    switch (event) {
        case MOUSE_LEFT_DOWN:
            down = True;
            button = 1;
            break;

        case MOUSE_LEFT_UP:
            down = False;
            button = 1;
            break;

        case MOUSE_RIGHT_DOWN:
            down = True;
            button = 3;
            break;

        case MOUSE_RIGHT_UP:
            down = False;
            button = 3;
            break;
    }

    XTestFakeButtonEvent(display, button, down, CurrentTime);
    XFlush(display);
#endif
}

#if defined(__WIN32__) || defined(__CYGWIN__)
#define SEND_MOUSE_EVENT(x) SendMouseEvent(x)
#elif __linux__
#define SEND_MOUSE_EVENT(...) SendMouseEvent(display, __VA_ARGS__)
#endif

#if defined(__WIN32__) || defined(__CYGWIN__)
void SendButtonPress(int btn){
	INPUT ip = { 0 };
	ip.type = INPUT_KEYBOARD;
	ip.ki.time = 0;
	ip.ki.wVk = 0;
	ip.ki.dwExtraInfo = 0;
	ip.ki.wScan = btn;
	ip.ki.dwFlags = KEYEVENTF_SCANCODE;
	SendInput(1, &ip, sizeof(INPUT));
#elif __linux__
void SendButtonPress (Display* display, int btn) {
    // Find the window which has the current keyboard focus.
    Window win;
    int    revert;
    XGetInputFocus(display, &win, &revert);
    Window winRoot = XDefaultRootWindow(display);

    // Send event
    XKeyEvent event;

    event.display     = display;
    event.window      = win;
    event.root        = winRoot;
    event.subwindow   = None;
    event.time        = CurrentTime;
    event.x           = 1;
    event.y           = 1;
    event.x_root      = 1;
    event.y_root      = 1;
    event.same_screen = True;
    event.keycode     = XKeysymToKeycode(display, btn);
    event.state       = 0; //modifiers;
    event.type        = KeyPress;
    XSendEvent(event.display, event.window, True, KeyPressMask, (XEvent *)&event);
#endif
}

#if defined(__WIN32__) || defined(__CYGWIN__)
#define SEND_BUTTON_PRESS(x) SendButtonPress(x)
#elif __linux__
#define SEND_BUTTON_PRESS(...) SendButtonPress(display, __VA_ARGS__)
#endif

#if defined(__WIN32__) || defined(__CYGWIN__)
void SendButtonRelease(int btn){
	INPUT ip = { 0 };
	ip.type = INPUT_KEYBOARD;
	ip.ki.time = 0;
	ip.ki.wVk = 0;
	ip.ki.dwExtraInfo = 0;
	ip.ki.wScan = btn;
	ip.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
	SendInput(1, &ip, sizeof(INPUT));
#elif __linux__
void SendButtonRelease(Display* display, int btn){
    // Find the window which has the current keyboard focus.
    Window win;
    int    revert;
    XGetInputFocus(display, &win, &revert);
    Window winRoot = XDefaultRootWindow(display);

    // Send event
    XKeyEvent event;

    event.display     = display;
    event.window      = win;
    event.root        = winRoot;
    event.subwindow   = None;
    event.time        = CurrentTime;
    event.x           = 1;
    event.y           = 1;
    event.x_root      = 1;
    event.y_root      = 1;
    event.same_screen = True;
    event.keycode     = XKeysymToKeycode(display, btn);
    event.state       = 0; //modifiers;
    event.type         = KeyRelease;
    XSendEvent(event.display, event.window, True, KeyPressMask, (XEvent *)&event);
#endif
}

#if defined(__WIN32__) || defined(__CYGWIN__)
#define SEND_BUTTON_RELEASE(x) SendButtonRelease(x)
#elif __linux__
#define SEND_BUTTON_RELEASE(...) SendButtonRelease(display, __VA_ARGS__)
#endif

time_t getLastModifiedTime(const char *path) {
	#if defined(__MINGW32__) && defined(__stat64)
	struct __stat64 attr;
	__stat64(path, &attr);
	return attr.st_mtime;
	#else
	struct stat attr;
	stat(path, &attr);
	return attr.st_mtime;
	#endif
}


static void sleepMs(int ms)
{
	#ifdef __WIN32__
	Sleep(ms);
	#else
	usleep(ms * 1000);
	#endif
}

// Networking

static void setNonBlocking(sock_t sock, bool enabled)
{
	#ifdef __WIN32__
	u_long mode = enabled ? 1 : 0;
	ioctlsocket(sock, FIONBIO, &mode);
	#else
	int flags = fcntl(sock, F_GETFL, 0);
	fcntl(sock, F_SETFL, enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
	#endif
}

static void setTimeouts(sock_t sock, int ms)
{
	#ifdef __WIN32__
	DWORD timeout = ms;
	#else
	struct timeval timeout;
	timeout.tv_sec = ms / 1000;
	timeout.tv_usec = (ms % 1000) * 1000;
	#endif
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
	setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
}

// Waits up to ms for sock to be readable, returns true if it is
static bool waitReadable(sock_t sock, int ms)
{
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	struct timeval tv;
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	return select((int)sock + 1, &fds, NULL, NULL, &tv) > 0;
}

static sock_t connectTo(const char* host, int timeout_ms)
{
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(GAMEPAD_PORT);
	addr.sin_addr.s_addr = inet_addr(host);
	if (addr.sin_addr.s_addr == INADDR_NONE)
	{
		printf("\"%s\" is not a valid IP address.\n", host);
		return INVALID_SOCKET;
	}

	sock_t sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET) return INVALID_SOCKET;

	// Non blocking connect so that an unreachable IP doesn't hang for ages
	setNonBlocking(sock, true);
	if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0)
	{
		#ifdef __WIN32__
		bool pending = WSAGetLastError() == WSAEWOULDBLOCK;
		#else
		bool pending = errno == EINPROGRESS;
		#endif
		fd_set wfds, efds;
		FD_ZERO(&wfds);
		FD_ZERO(&efds);
		FD_SET(sock, &wfds);
		FD_SET(sock, &efds);
		struct timeval tv;
		tv.tv_sec = timeout_ms / 1000;
		tv.tv_usec = (timeout_ms % 1000) * 1000;
		int error = 0;
		socklen_t len = sizeof(error);
		if (!pending || select((int)sock + 1, NULL, &wfds, &efds, &tv) <= 0 || FD_ISSET(sock, &efds) ||
			getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&error, &len) != 0 || error != 0)
		{
			close_socket(sock);
			return INVALID_SOCKET;
		}
	}
	setNonBlocking(sock, false);

	int one = 1;
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
	setTimeouts(sock, SOCKET_TIMEOUT_MS);
	return sock;
}

static bool sendAll(sock_t sock, const void* buf, int len)
{
	const char* p = (const char*)buf;
	while (len > 0)
	{
		int ret = send(sock, p, len, 0);
		if (ret <= 0) return false;
		p += ret;
		len -= ret;
	}
	return true;
}

// Returns the number of bytes received before the connection was closed or timed out
static int recvAll(sock_t sock, void* buf, int len)
{
	char* p = (char*)buf;
	int got = 0;
	while (got < len)
	{
		int ret = recv(sock, p + got, len - got, 0);
		if (ret <= 0) break;
		got += ret;
	}
	return got;
}

// Broadcast addresses of every network interface, plus the global one
static std::vector<uint32_t> getBroadcastAddresses(sock_t sock)
{
	std::vector<uint32_t> result;
	result.push_back(INADDR_BROADCAST);
	#ifdef __WIN32__
	INTERFACE_INFO interfaces[32];
	DWORD bytes = 0;
	if (WSAIoctl(sock, SIO_GET_INTERFACE_LIST, NULL, 0, interfaces, sizeof(interfaces), &bytes, NULL, NULL) == 0)
	{
		for (DWORD i = 0; i < bytes / sizeof(INTERFACE_INFO); i++)
		{
			if (!(interfaces[i].iiFlags & IFF_UP) || (interfaces[i].iiFlags & IFF_LOOPBACK)) continue;
			uint32_t ip = interfaces[i].iiAddress.AddressIn.sin_addr.s_addr;
			uint32_t mask = interfaces[i].iiNetmask.AddressIn.sin_addr.s_addr;
			result.push_back(ip | ~mask);
		}
	}
	#else
	struct ifaddrs* ifaddr;
	if (getifaddrs(&ifaddr) == 0)
	{
		for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
		{
			if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) continue;
			if (!(ifa->ifa_flags & IFF_BROADCAST) || ifa->ifa_broadaddr == NULL) continue;
			result.push_back(((struct sockaddr_in*)ifa->ifa_broadaddr)->sin_addr.s_addr);
		}
		freeifaddrs(ifaddr);
	}
	#endif
	return result;
}

// Looks for Vitas running VitaPad on the local network, returns true and fills host if one was picked
static bool discoverVita(char* host, size_t size)
{
	printf("Searching for your Vita on the local network...\n");
	sock_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET) return false;
	int one = 1;
	setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof(one));

	std::vector<uint32_t> targets = getBroadcastAddresses(sock);
	for (size_t i = 0; i < targets.size(); i++)
	{
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(DISCOVERY_PORT);
		addr.sin_addr.s_addr = targets[i];
		sendto(sock, DISCOVERY_REQUEST, strlen(DISCOVERY_REQUEST), 0, (struct sockaddr*)&addr, sizeof(addr));
	}

	std::vector<std::string> found;
	time_t start = time(NULL);
	while (waitReadable(sock, DISCOVERY_TIMEOUT_MS) && time(NULL) - start < 3)
	{
		char buf[64];
		struct sockaddr_in from;
		socklen_t fromlen = sizeof(from);
		int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromlen);
		if (len < (int)strlen(DISCOVERY_REPLY) || memcmp(buf, DISCOVERY_REPLY, strlen(DISCOVERY_REPLY)) != 0) continue;
		std::string ip = inet_ntoa(from.sin_addr);
		bool known = false;
		for (size_t i = 0; i < found.size(); i++) known |= found[i] == ip;
		if (!known) found.push_back(ip);
	}
	close_socket(sock);

	if (found.empty())
	{
		printf("No Vita found. Make sure VitaPad is running on your Vita and both devices are on the same network.\n");
		return false;
	}

	size_t choice = 0;
	if (found.size() > 1)
	{
		printf("Multiple Vitas found:\n");
		for (size_t i = 0; i < found.size(); i++) printf("%d) %s\n", (int)i + 1, found[i].c_str());
		printf("Choose one: ");
		int n = 0;
		if (scanf("%d", &n) != 1 || n < 1 || n > (int)found.size()) return false;
		choice = n - 1;
	}
	printf("Found Vita at %s\n", found[choice].c_str());
	snprintf(host, size, "%s", found[choice].c_str());
	return true;
}

static bool loadSavedIp(char* host, size_t size)
{
	FILE* f = fopen(SAVED_IP_FILE, "r");
	if (f == NULL) return false;
	bool ok = fgets(host, size, f) != NULL;
	fclose(f);
	if (!ok) return false;
	host[strcspn(host, "\r\n ")] = 0;
	return host[0] != 0;
}

static void saveIp(const char* host)
{
	FILE* f = fopen(SAVED_IP_FILE, "w");
	if (f == NULL) return;
	fprintf(f, "%s\n", host);
	fclose(f);
}

// Input emulation

// Converts a packet to the format used by keyboard and vJoy emulation
static void toLegacyPacket(const PadPacketV2& in, PadPacket& out)
{
	memset(&out, 0, sizeof(PadPacket));
	out.buttons = in.buttons;
	out.lx = in.lx;
	out.ly = in.ly;
	out.rx = in.rx;
	out.ry = in.ry;
	uint8_t flags = NO_INPUT;
	if (in.front_num > 0)
	{
		out.tx = in.front[0].x;
		out.ty = in.front[0].y;
		flags += MOUSE_MOV;
	}
	if (in.rear_num > 0)
	{
		if (in.rear[0].x > TOUCH_WIDTH / 2) flags += RIGHT_CLICK;
		else flags += LEFT_CLICK;
	}
	out.click = flags;
}

// A packet with nothing pressed, used to release everything when the connection drops
static void neutralPacket(PadPacketV2& packet)
{
	memset(&packet, 0, sizeof(PadPacketV2));
	packet.lx = packet.ly = packet.rx = packet.ry = 128;
	packet.battery = 100;
}

#ifdef __WIN32__
static void processVjoy(const PadPacket& data)
{
	static JOYSTICK_POSITION_V3 joystickData;
	static JOYSTICK_POSITION_V3 joystickDataOld;

	joystickData.bDevice = VJOY_DEVID;
	joystickData.wAxisZ = 16384;
	joystickData.wAxisZRot = 16384;
	joystickData.wSlider = 16384;

	LONG buttons = 0;
	if (data.buttons & SCE_CTRL_LEFT)
	{
		joystickData.wAxisZRot = 0;
	}
	else if (data.buttons & SCE_CTRL_RIGHT)
	{
		joystickData.wAxisZRot = 32768;
	}
	if (data.buttons & SCE_CTRL_UP)
	{
		joystickData.wSlider = 32768;
	}
	else if (data.buttons & SCE_CTRL_DOWN)
	{
		joystickData.wSlider = 0;
	}
	if (data.buttons & SCE_CTRL_TRIANGLE)
	{
		buttons = buttons | VJOY_CTRL_TRIANGLE;
	}
	if (data.buttons & SCE_CTRL_SQUARE)
	{
		buttons = buttons | VJOY_CTRL_SQUARE;
	}
	if (data.buttons & SCE_CTRL_CROSS)
	{
		buttons = buttons | VJOY_CTRL_CROSS;
	}
	if (data.buttons & SCE_CTRL_CIRCLE)
	{
		buttons = buttons | VJOY_CTRL_CIRCLE;
	}
	if (data.buttons & SCE_CTRL_RTRIGGER)
	{
		joystickData.wAxisZ = 0;
	}
	if (data.buttons & SCE_CTRL_LTRIGGER)
	{
		joystickData.wAxisZ = 32768;
	}
	if (data.buttons & SCE_CTRL_START)
	{
		buttons = buttons | VJOY_CTRL_START;
	}
	if (data.buttons & SCE_CTRL_SELECT)
	{
		buttons = buttons | VJOY_CTRL_SELECT;
	}
	if (VJOY_ALTERNATE)
	{
		if (data.rx < 70)
		{
			buttons = buttons | VJOY_CTRL_LBUMPER;
		}
		else if (data.rx > 180)
		{
			buttons = buttons | VJOY_CTRL_RBUMPER;
		}
		joystickData.wAxisXRot = 16384;
		joystickData.wAxisYRot = 16384;
	}
	else
	{
		// Upper corners = bumpers, lower corners = L3/R3 (whole halves = bumpers if the device lacks buttons)
		if (data.click & MOUSE_MOV)
		{
			bool left = data.tx < SCREEN_WIDTH/2;
			if (VJOY_BUTTONS >= 10 && data.ty >= SCREEN_HEIGHT/2)
			{
				buttons = buttons | (left ? VJOY_CTRL_L3 : VJOY_CTRL_R3);
			}
			else
			{
				buttons = buttons | (left ? VJOY_CTRL_LBUMPER : VJOY_CTRL_RBUMPER);
			}
		}
		joystickData.wAxisXRot = data.rx*128;
		joystickData.wAxisYRot = data.ry*128;
	}
	joystickData.lButtons = buttons;
	joystickData.wAxisX = data.lx*128;
	joystickData.wAxisY = data.ly*128;

	if (0 != memcmp(&joystickDataOld, &joystickData, sizeof(JOYSTICK_POSITION_V3)))
	{
		if (!UpdateVJD(VJOY_DEVID, &joystickData))
		{
			printf("\nERROR: Feeding VJOY failed, please restart the app.\n");
			abortVjoy();
		}

		memcpy(&joystickDataOld,&joystickData,sizeof(JOYSTICK_POSITION_V3));
	}
}
#endif

static void processKeyboard(const PadPacket& data, const PadPacket& olddata)
{
	// Down
	if ((data.buttons & SCE_CTRL_DOWN) && (!(olddata.buttons & SCE_CTRL_DOWN))) SEND_BUTTON_PRESS(KEY_DOWN);
	else if ((olddata.buttons & SCE_CTRL_DOWN) && (!(data.buttons & SCE_CTRL_DOWN))) SEND_BUTTON_RELEASE(KEY_DOWN);

	// Up
	if ((data.buttons & SCE_CTRL_UP) && (!(olddata.buttons & SCE_CTRL_UP))) SEND_BUTTON_PRESS(KEY_UP);
	else if ((olddata.buttons & SCE_CTRL_UP) && (!(data.buttons & SCE_CTRL_UP))) SEND_BUTTON_RELEASE(KEY_UP);

	// Left
	if ((data.buttons & SCE_CTRL_LEFT) && (!(olddata.buttons & SCE_CTRL_LEFT))) SEND_BUTTON_PRESS(KEY_LEFT);
	else if ((olddata.buttons & SCE_CTRL_LEFT) && (!(data.buttons & SCE_CTRL_LEFT))) SEND_BUTTON_RELEASE(KEY_LEFT);

	// Right
	if ((data.buttons & SCE_CTRL_RIGHT) && (!(olddata.buttons & SCE_CTRL_RIGHT))) SEND_BUTTON_PRESS(KEY_RIGHT);
	else if ((olddata.buttons & SCE_CTRL_RIGHT) && (!(data.buttons & SCE_CTRL_RIGHT))) SEND_BUTTON_RELEASE(KEY_RIGHT);

	// Triangle
	if ((data.buttons & SCE_CTRL_TRIANGLE) && (!(olddata.buttons & SCE_CTRL_TRIANGLE))) SEND_BUTTON_PRESS(KEY_TRIANGLE);
	else if ((olddata.buttons & SCE_CTRL_TRIANGLE) && (!(data.buttons & SCE_CTRL_TRIANGLE))) SEND_BUTTON_RELEASE(KEY_TRIANGLE);

	// Square
	if ((data.buttons & SCE_CTRL_SQUARE) && (!(olddata.buttons & SCE_CTRL_SQUARE))) SEND_BUTTON_PRESS(KEY_SQUARE);
	else if ((olddata.buttons & SCE_CTRL_SQUARE) && (!(data.buttons & SCE_CTRL_SQUARE))) SEND_BUTTON_RELEASE(KEY_SQUARE);

	// Cross
	if ((data.buttons & SCE_CTRL_CROSS) && (!(olddata.buttons & SCE_CTRL_CROSS))) SEND_BUTTON_PRESS(KEY_CROSS);
	else if ((olddata.buttons & SCE_CTRL_CROSS) && (!(data.buttons & SCE_CTRL_CROSS))) SEND_BUTTON_RELEASE(KEY_CROSS);

	// Circle
	if ((data.buttons & SCE_CTRL_CIRCLE) && (!(olddata.buttons & SCE_CTRL_CIRCLE))) SEND_BUTTON_PRESS(KEY_CIRCLE);
	else if ((olddata.buttons & SCE_CTRL_CIRCLE) && (!(data.buttons & SCE_CTRL_CIRCLE))) SEND_BUTTON_RELEASE(KEY_CIRCLE);

	// L Trigger
	if ((data.buttons & SCE_CTRL_LTRIGGER) && (!(olddata.buttons & SCE_CTRL_LTRIGGER))) SEND_BUTTON_PRESS(KEY_L);
	else if ((olddata.buttons & SCE_CTRL_LTRIGGER) && (!(data.buttons & SCE_CTRL_LTRIGGER))) SEND_BUTTON_RELEASE(KEY_L);

	// R Trigger
	if ((data.buttons & SCE_CTRL_RTRIGGER) && (!(olddata.buttons & SCE_CTRL_RTRIGGER))) SEND_BUTTON_PRESS(KEY_R);
	else if ((olddata.buttons & SCE_CTRL_RTRIGGER) && (!(data.buttons & SCE_CTRL_RTRIGGER))) SEND_BUTTON_RELEASE(KEY_R);

	// Start
	if ((data.buttons & SCE_CTRL_START) && (!(olddata.buttons & SCE_CTRL_START))) SEND_BUTTON_PRESS(KEY_START);
	else if ((olddata.buttons & SCE_CTRL_START) && (!(data.buttons & SCE_CTRL_START))) SEND_BUTTON_RELEASE(KEY_START);

	// Select
	if ((data.buttons & SCE_CTRL_SELECT) && (!(olddata.buttons & SCE_CTRL_SELECT))) SEND_BUTTON_PRESS(KEY_SELECT);
	else if ((olddata.buttons & SCE_CTRL_SELECT) && (!(data.buttons & SCE_CTRL_SELECT))) SEND_BUTTON_RELEASE(KEY_SELECT);

	// Left Analog
	if ((data.ly < 50) && (!(olddata.ly < 50))) SEND_BUTTON_PRESS(KEY_LANALOG_UP);
	else if ((olddata.ly < 50) && (!(data.ly < 50))) SEND_BUTTON_RELEASE(KEY_LANALOG_UP);
	if ((data.lx < 50) && (!(olddata.lx < 50))) SEND_BUTTON_PRESS(KEY_LANALOG_LEFT);
	else if ((olddata.lx < 50) && (!(data.lx < 50))) SEND_BUTTON_RELEASE(KEY_LANALOG_LEFT);
	if ((data.lx > 170) && (!(olddata.lx > 170))) SEND_BUTTON_PRESS(KEY_LANALOG_RIGHT);
	else if ((olddata.lx > 170) && (!(data.lx > 170))) SEND_BUTTON_RELEASE(KEY_LANALOG_RIGHT);
	if ((data.ly > 170) && (!(olddata.ly > 170))) SEND_BUTTON_PRESS(KEY_LANALOG_DOWN);
	else if ((olddata.ly > 170) && (!(data.ly > 170))) SEND_BUTTON_RELEASE(KEY_LANALOG_DOWN);

	// Right Analog
	if ((data.ry < 50) && (!(olddata.ry < 50))) SEND_BUTTON_PRESS(KEY_RANALOG_UP);
	else if ((olddata.ry < 50) && (!(data.ry < 50))) SEND_BUTTON_RELEASE(KEY_RANALOG_UP);
	if ((data.rx < 50) && (!(olddata.rx < 50))) SEND_BUTTON_PRESS(KEY_RANALOG_LEFT);
	else if ((olddata.rx < 50) && (!(data.rx < 50))) SEND_BUTTON_RELEASE(KEY_RANALOG_LEFT);
	if ((data.rx > 170) && (!(olddata.rx > 170))) SEND_BUTTON_PRESS(KEY_RANALOG_RIGHT);
	else if ((olddata.rx > 170) && (!(data.rx > 170))) SEND_BUTTON_RELEASE(KEY_RANALOG_RIGHT);
	if ((data.ry > 170) && (!(olddata.ry > 170))) SEND_BUTTON_PRESS(KEY_RANALOG_DOWN);
	else if ((olddata.ry > 170) && (!(data.ry > 170))) SEND_BUTTON_RELEASE(KEY_RANALOG_DOWN);

	// Mouse emulation with touchscreen + retrotouch
	if (data.click & MOUSE_MOV) SEND_MOVE_MOUSE(data.tx, data.ty);
	if ((data.click & LEFT_CLICK) && (!(olddata.click & LEFT_CLICK))) SEND_MOUSE_EVENT(MOUSE_LEFT_DOWN);
	else if ((olddata.click & LEFT_CLICK) && (!(data.click & LEFT_CLICK))) SEND_MOUSE_EVENT(MOUSE_LEFT_UP);
	if ((data.click & RIGHT_CLICK) && (!(olddata.click & RIGHT_CLICK))) SEND_MOUSE_EVENT(MOUSE_RIGHT_DOWN);
	else if ((olddata.click & RIGHT_CLICK) && (!(data.click & RIGHT_CLICK))) SEND_MOUSE_EVENT(MOUSE_RIGHT_UP);
}

// Monitor mode: print what the Vita sends instead of emulating any input
bool MONITOR_MODE = false;

// Viewer mode: live 3D view of the Vita in the browser
bool VIEWER_MODE = false;

static uint64_t nowMs()
{
	#ifdef __WIN32__
	return GetTickCount64();
	#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
	#endif
}

static void printTouches(const char* name, const TouchPoint* points, int num)
{
	printf(" %s:", name);
	if (num == 0) printf(" -          ");
	for (int i = 0; i < num; i++) printf(" (%4d,%4d)", points[i].x, points[i].y);
}

static void printMonitor(const PadPacketV2& packet)
{
	static const struct { uint32_t mask; const char* name; } names[] = {
		{ SCE_CTRL_UP, "UP" }, { SCE_CTRL_DOWN, "DOWN" }, { SCE_CTRL_LEFT, "LEFT" }, { SCE_CTRL_RIGHT, "RIGHT" },
		{ SCE_CTRL_CROSS, "CROSS" }, { SCE_CTRL_CIRCLE, "CIRCLE" }, { SCE_CTRL_SQUARE, "SQUARE" }, { SCE_CTRL_TRIANGLE, "TRIANGLE" },
		{ SCE_CTRL_LTRIGGER, "L" }, { SCE_CTRL_RTRIGGER, "R" }, { SCE_CTRL_START, "START" }, { SCE_CTRL_SELECT, "SELECT" },
	};
	static uint64_t last = 0;
	static int packets = 0;
	static int rate = 0;
	static uint64_t rate_start = 0;
	uint64_t now = nowMs();
	packets++;
	if (now - rate_start >= 1000)
	{
		rate = packets;
		packets = 0;
		rate_start = now;
	}
	if (now - last < 200) return;
	last = now;

	printf("[%4d pkt/s] bat %3d%% | L(%3d,%3d) R(%3d,%3d) |", rate, packet.battery, packet.lx, packet.ly, packet.rx, packet.ry);
	printTouches("front", packet.front, packet.front_num);
	printTouches("rear", packet.rear, packet.rear_num);
	printf(" | accel(%5.2f,%5.2f,%5.2f)G gyro(%7.1f,%7.1f,%7.1f)deg/s |",
		packet.accel[0], packet.accel[1], packet.accel[2],
		packet.gyro[0] * 360.0f, packet.gyro[1] * 360.0f, packet.gyro[2] * 360.0f);
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (packet.buttons & names[i].mask) printf(" %s", names[i].name);
	printf("\n");
	fflush(stdout);
}

// Feeds a packet to the active emulation mode, returns false on unrecoverable errors
static bool processPacket(const PadPacketV2& packet)
{
	static PadPacket olddata;
	static bool firstScan = true;
	if (MONITOR_MODE)
	{
		printMonitor(packet);
		return true;
	}
	
	PadPacket data;
	toLegacyPacket(packet, data);
	if (firstScan)
	{
		firstScan = false;
		PadPacketV2 neutral;
		neutralPacket(neutral);
		toLegacyPacket(neutral, olddata);
	}

	#ifdef __WIN32__
	if (VJOY_MODE)
	{
		processVjoy(data);
	}
	else if (VIGEM_MODE == VIGEM_DEVICE_DS4)
	{
		if (!vgSubmit(&packet, &VIGEM_OPTIONS))
		{
			printf("\nERROR: Feeding VIGEM failed, please restart the app.\n");
			return false;
		}
	}
	else
	#endif
	{
		processKeyboard(data, olddata);
	}

	// Saving old pad status
	memcpy(&olddata,&data,sizeof(PadPacket));
	return true;
}

#ifdef __WIN32__
void ControllerCleanup()
{
    if (VJOY_MODE)
    {
        abortVjoy();
        VJOY_MODE = false;
    }
    else if (VIGEM_MODE == VIGEM_DEVICE_DS4)
    {
        vgDestroy();
        VIGEM_MODE = VIGEM_DEVICE_NONE;
    }
}

BOOL WINAPI HandlerRoutine(
    _In_ DWORD dwCtrlType
    )
{
   switch (dwCtrlType)
   {
       case CTRL_C_EVENT:
       {
            printf("\nQuitting...\n");
            ControllerCleanup();
			ExitProcess(0);
            return TRUE;
       }
       default:
       {
            return FALSE;
       }
   }
}
#endif

enum {
	SESSION_LOST,
	SESSION_OUTDATED_SERVER,
	SESSION_FATAL
};

// Polls the Vita until the connection drops
static int runSession(sock_t sock, time_t& life_tick)
{
	PadPacketV2 packet;
	for (;;){

		// Checking if we need a mapping reload
		time_t re_tick;
		if ((re_tick = getLastModifiedTime(CONFIG_FILE)) != life_tick){
			loadConfig(CONFIG_FILE);
			life_tick = re_tick;
			printf("\nConfig file reloaded since a modification has been detected.");
			fflush(stdout);
		}

		if (!sendAll(sock, REQUEST_V2, REQUEST_SIZE)) return SESSION_LOST;
		int count = recvAll(sock, &packet, sizeof(PadPacketV2));
		// Old Vita apps ignore the request type and reply with a legacy packet
		if (count == sizeof(PadPacket)) return SESSION_OUTDATED_SERVER;
		if (count != sizeof(PadPacketV2)) return SESSION_LOST;
		if (VIEWER_MODE) viewerUpdate(packet);
		if (!processPacket(packet)) return SESSION_FATAL;
	}
}

int main(int argc,char** argv){

	#ifdef __WIN32__
    SetConsoleCtrlHandler(HandlerRoutine, TRUE);

	WORD versionWanted = MAKEWORD(2, 2);
	WSADATA wsaData;
	WSAStartup(versionWanted, &wsaData);
	#endif

	// Usage: VitaPad [--monitor] [--viewer] [Vita IP]
	const char* ip_arg = NULL;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--monitor") == 0) MONITOR_MODE = true;
		else if (strcmp(argv[i], "--viewer") == 0) VIEWER_MODE = true;
		else ip_arg = argv[i];
	}

    #ifdef __linux__
    // A dropped connection must not kill the client while sending
    signal(SIGPIPE, SIG_IGN);

    if (!MONITOR_MODE)
    {
        display = XOpenDisplay(0);
        if (display == NULL)
            exit(1);
    }
    #endif

	// Loading mapping
	time_t life_tick = 0;
	loadConfig(CONFIG_FILE);
	life_tick = getLastModifiedTime(CONFIG_FILE);

	printf("VitaPad Client v1.4 by Rinnegatamante\n\n");
	if (MONITOR_MODE)
	{
		printf("MONITOR MODE: printing what the Vita sends, no input is emulated.\n\n");
		#ifdef __WIN32__
		VJOY_MODE = false;
		VIGEM_MODE = VIGEM_DEVICE_NONE;
		#endif
	}
	#ifdef __WIN32__
	if (VJOY_MODE && (VIGEM_MODE != VIGEM_DEVICE_NONE))
	{
        printf("!!!CONFLICTING!!!\nvJoy and ViGEm cannot be enabled at the same time.\nEdit the config and restart the application to disable at least one of them.\n");
	}
    else if (VJOY_MODE)
    {
        printf("!!!STARTING IN VJOY MODE!!!\nEdit the config and restart the application to disable it.\n");
		initVjoy();
    }
    else if (VIGEM_MODE == VIGEM_DEVICE_DS4)
    {
        printf("!!!STARTING IN VIGEM MODE!!!\nEdit the config and restart the application to disable it.\n");
        if (!vgInit())
        {
            printf("ERROR: An error occurred while initializing ViGEm. Reverting back to keybinds.\n");
            VIGEM_MODE = VIGEM_DEVICE_NONE;
        }
    }
	#endif

	if (VIEWER_MODE && !viewerStart(VIEWER_PORT))
	{
		printf("ERROR: Unable to start the 3D viewer, is port %d already in use?\n", VIEWER_PORT);
		VIEWER_MODE = false;
	}
	
	// The Vita IP comes from the command line, or the last Vita we connected to, or automatic discovery
	char host[64] = "";
	if (ip_arg) snprintf(host, sizeof(host), "%s", ip_arg);
	else if (loadSavedIp(host, sizeof(host))) printf("Using last Vita IP: %s (delete %s to forget it)\n", host, SAVED_IP_FILE);

	bool wasConnected = false;
	int result = SESSION_LOST;
	for (;;){
		sock_t sock = INVALID_SOCKET;
		if (host[0])
		{
			printf("Connecting to %s:%d...\n", host, GAMEPAD_PORT);
			fflush(stdout);
			sock = connectTo(host, CONNECT_TIMEOUT_MS);
		}
		if (sock == INVALID_SOCKET)
		{
			if (host[0]) printf("Unable to connect to %s.\n", host);
			// The Vita may have got a new IP from the router, look for it
			char found[64];
			if (discoverVita(found, sizeof(found)) && strcmp(found, host) != 0)
			{
				snprintf(host, sizeof(host), "%s", found);
			}
			else if (wasConnected)
			{
				// Keep trying to get back the Vita we lost
				sleepMs(1000);
			}
			else
			{
				printf("Insert Vita IP: ");
				if (scanf("%63s", host) != 1) break;
			}
			continue;
		}

		printf("Connection established!\n");
		fflush(stdout);
		saveIp(host);
		wasConnected = true;
		if (VIEWER_MODE) viewerSetConnected(true);

		result = runSession(sock, life_tick);
		close_socket(sock);
		if (VIEWER_MODE) viewerSetConnected(false);

		// Releasing everything that was pressed when the connection dropped
		PadPacketV2 neutral;
		neutralPacket(neutral);
		processPacket(neutral);

		if (result == SESSION_OUTDATED_SERVER)
		{
			printf("\nERROR: The VitaPad app on your Vita is outdated, please update it to use this client.\n");
			break;
		}
		if (result == SESSION_FATAL) break;
		printf("\nConnection lost, reconnecting...\n");
	}

    #ifdef __linux__
    if (display) XCloseDisplay(display);
    #elif defined(__WIN32__)
    ControllerCleanup();
    #endif

	return 1;
}

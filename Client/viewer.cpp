#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string>

#ifdef __WIN32__
# include <winsock2.h>
# include <ws2tcpip.h>
# include <windows.h>
# include <shellapi.h>
typedef SOCKET sock_t;
# define close_socket closesocket
# define SEND_FLAGS 0
#else
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <unistd.h>
# include <pthread.h>
# include <time.h>
typedef int sock_t;
# define INVALID_SOCKET (-1)
# define close_socket close
# define SEND_FLAGS MSG_NOSIGNAL
#endif

#include "viewer.h"
#include "viewer_html.h"

static PadPacketV2 latest;
static bool connected = false;
static int rate = 0;

#ifdef __WIN32__
static CRITICAL_SECTION lock;
static void lockState() { EnterCriticalSection(&lock); }
static void unlockState() { LeaveCriticalSection(&lock); }
#else
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void lockState() { pthread_mutex_lock(&lock); }
static void unlockState() { pthread_mutex_unlock(&lock); }
#endif

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

static void sleepMs(int ms)
{
	#ifdef __WIN32__
	Sleep(ms);
	#else
	usleep(ms * 1000);
	#endif
}

void viewerUpdate(const PadPacketV2& packet)
{
	static int packets = 0;
	static uint64_t rate_start = 0;
	uint64_t now = nowMs();
	lockState();
	latest = packet;
	packets++;
	if (now - rate_start >= 1000)
	{
		rate = packets;
		packets = 0;
		rate_start = now;
	}
	unlockState();
}

void viewerSetConnected(bool value)
{
	lockState();
	connected = value;
	if (!connected) rate = 0;
	unlockState();
}

static bool sendAll(sock_t sock, const char* data, size_t len)
{
	while (len > 0)
	{
		int ret = send(sock, data, (int)len, SEND_FLAGS);
		if (ret <= 0) return false;
		data += ret;
		len -= ret;
	}
	return true;
}

static std::string stateJson()
{
	lockState();
	PadPacketV2 p = latest;
	bool c = connected;
	int r = rate;
	unlockState();

	char buf[512];
	std::string json;
	snprintf(buf, sizeof(buf), "{\"connected\":%s,\"rate\":%d,\"buttons\":%u,\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d,\"battery\":%d,\"t\":%u,"
		"\"accel\":[%.4f,%.4f,%.4f],\"gyro\":[%.5f,%.5f,%.5f],",
		c ? "true" : "false", r, p.buttons, p.lx, p.ly, p.rx, p.ry, p.battery, p.timestamp,
		p.accel[0], p.accel[1], p.accel[2], p.gyro[0], p.gyro[1], p.gyro[2]);
	json = buf;
	const char* names[2] = { "front", "rear" };
	const TouchPoint* panels[2] = { p.front, p.rear };
	int nums[2] = { p.front_num, p.rear_num };
	for (int i = 0; i < 2; i++)
	{
		json += "\"";
		json += names[i];
		json += "\":[";
		for (int j = 0; j < nums[i] && j < 2; j++)
		{
			snprintf(buf, sizeof(buf), "%s[%d,%d]", j ? "," : "", panels[i][j].x, panels[i][j].y);
			json += buf;
		}
		json += i == 0 ? "]," : "]";
	}
	json += "}";
	return json;
}

static void serveClient(sock_t sock)
{
	// Reading the request headers
	char request[2048];
	int len = 0;
	while (len < (int)sizeof(request) - 1)
	{
		int ret = recv(sock, request + len, sizeof(request) - 1 - len, 0);
		if (ret <= 0) break;
		len += ret;
		request[len] = 0;
		if (strstr(request, "\r\n\r\n")) break;
	}
	request[len] = 0;

	if (strncmp(request, "GET /events", 11) == 0)
	{
		// Server-sent events: the page receives the Vita state about 60 times per second
		const char* header = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n";
		if (sendAll(sock, header, strlen(header)))
		{
			for (;;)
			{
				std::string event = "data: " + stateJson() + "\n\n";
				if (!sendAll(sock, event.c_str(), event.size())) break;
				sleepMs(16);
			}
		}
	}
	else if (strncmp(request, "GET / ", 6) == 0)
	{
		char header[256];
		size_t html_len = strlen(VIEWER_HTML);
		snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", (unsigned int)html_len);
		if (sendAll(sock, header, strlen(header))) sendAll(sock, VIEWER_HTML, html_len);
	}
	else
	{
		const char* notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		sendAll(sock, notFound, strlen(notFound));
	}
	close_socket(sock);
}

#ifdef __WIN32__
static DWORD WINAPI clientThread(LPVOID arg)
{
	serveClient((sock_t)(uintptr_t)arg);
	return 0;
}
#else
static void* clientThread(void* arg)
{
	serveClient((sock_t)(intptr_t)arg);
	return NULL;
}
#endif

#ifdef __WIN32__
static DWORD WINAPI acceptThread(LPVOID arg)
#else
static void* acceptThread(void* arg)
#endif
{
	sock_t server = (sock_t)(uintptr_t)arg;
	for (;;)
	{
		sock_t client = accept(server, NULL, NULL);
		if (client == INVALID_SOCKET)
		{
			sleepMs(100);
			continue;
		}
		#ifdef __WIN32__
		HANDLE thread = CreateThread(NULL, 0, clientThread, (LPVOID)(uintptr_t)client, 0, NULL);
		if (thread) CloseHandle(thread);
		else close_socket(client);
		#else
		pthread_t thread;
		if (pthread_create(&thread, NULL, clientThread, (void*)(intptr_t)client) == 0) pthread_detach(thread);
		else close_socket(client);
		#endif
	}
	return 0;
}

static void openBrowser(const char* url)
{
	#ifdef __WIN32__
	ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
	#else
	std::string command = std::string("xdg-open ") + url + " >/dev/null 2>&1 &";
	if (system(command.c_str()) != 0) {}
	#endif
}

bool viewerStart(int port)
{
	// Already running (the tray app can open it several times): just show it again
	static bool started = false;
	char url[64];
	snprintf(url, sizeof(url), "http://localhost:%d/", port);
	if (started)
	{
		openBrowser(url);
		return true;
	}
	#ifdef __WIN32__
	InitializeCriticalSection(&lock);
	#endif
	memset(&latest, 0, sizeof(latest));
	latest.lx = latest.ly = latest.rx = latest.ry = 128;

	sock_t server = socket(AF_INET, SOCK_STREAM, 0);
	if (server == INVALID_SOCKET) return false;
	int one = 1;
	setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Only reachable from this PC
	if (bind(server, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(server, 8) != 0)
	{
		close_socket(server);
		return false;
	}

	#ifdef __WIN32__
	HANDLE thread = CreateThread(NULL, 0, acceptThread, (LPVOID)(uintptr_t)server, 0, NULL);
	if (!thread) return false;
	CloseHandle(thread);
	#else
	pthread_t thread;
	if (pthread_create(&thread, NULL, acceptThread, (void*)(intptr_t)server) != 0) return false;
	pthread_detach(thread);
	#endif

	started = true;
	printf("3D viewer running at %s\n", url);
	openBrowser(url);
	return true;
}

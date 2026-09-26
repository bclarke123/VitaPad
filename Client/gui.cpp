// Windows tray app: the VitaPad client runs in the background with an icon in the notification area,
// and a small window shows the connection status and the most used settings. Settings are saved to
// windows.xml (like editing it by hand) and apply straight away.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string>

#include "client.h"
#include "tinyxml2.h"

#define WINDOW_CLASS "VitaPadWindow"
#define INSTANCE_MUTEX "VitaPadClientSingleInstance"
#define RUN_KEY "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE "VitaPad"
#define VIGEM_URL "https://github.com/nefarius/ViGEmBus/releases"

#define WM_APP_TRAY (WM_APP + 1)
#define WM_APP_SHOW (WM_APP + 2)
#define TIMER_REFRESH 1
#define REFRESH_MS 250

enum {
	ID_STATUS = 100, ID_DETAILS, ID_ERROR, ID_GET_VIGEM,
	ID_CONTROLLER, ID_STREAM, ID_GYRO, ID_GYRO_VALUE, ID_AUTOSTART,
	ID_MAPPING, ID_VIEWER, ID_HIDE,
	// Tray menu
	ID_MENU_OPEN = 200, ID_MENU_STREAM, ID_MENU_AUTOSTART, ID_MENU_QUIT,
	ID_MENU_CONTROLLER = 300, // + controller index
};

// Controller choices, as written to windows.xml
static const struct { const char* label; unsigned vigem; bool vjoy; } controllers[] = {
	{ "Xbox 360 controller (ViGEm)", 2, false },
	{ "DualShock 4 (ViGEm: gyro, touchpad)", 1, false },
	{ "Keyboard and mouse", 0, false },
	{ "vJoy", 0, true },
};
#define CONTROLLERS_NUM (int)(sizeof(controllers) / sizeof(controllers[0]))

static HINSTANCE instance;
static HWND window;
static HWND controls[ID_HIDE + 1];
static HFONT font, bold_font;
static HICON icon;
static NOTIFYICONDATAA tray;
static UINT taskbar_created;
static int dpi = 96;

// Current settings (as shown in the window)
static int controller = 2;
static bool stream_mode = true;
static float gyro_sensitivity = 1.0f;

static int S(int value) { return MulDiv(value, dpi, 96); }

// ---- Config file ----

static void readConfig()
{
	tinyxml2::XMLDocument doc;
	if (doc.LoadFile(clientConfigFile()) != tinyxml2::XML_NO_ERROR) return;
	unsigned vigem = 0;
	bool vjoy = false;
	tinyxml2::XMLElement* e;
	if ((e = doc.FirstChildElement("VIGEM_MODE"))) e->QueryUnsignedText(&vigem);
	if ((e = doc.FirstChildElement("VJOY_MODE"))) e->QueryBoolText(&vjoy);
	if ((e = doc.FirstChildElement("STREAM_MODE"))) e->QueryBoolText(&stream_mode);
	if ((e = doc.FirstChildElement("VIGEM_GYRO_SENSITIVITY"))) e->QueryFloatText(&gyro_sensitivity);
	controller = 2;
	for (int i = 0; i < CONTROLLERS_NUM; i++)
		if (controllers[i].vigem == vigem && (vigem != 0 || controllers[i].vjoy == vjoy)) { controller = i; break; }
}

// Sets <name>value</name> in the config file, keeping everything else exactly as it was
static void setConfigText(std::string& text, const char* name, const char* value)
{
	std::string open = std::string("<") + name + ">", close = std::string("</") + name + ">";
	size_t start = text.find(open);
	size_t end = start == std::string::npos ? std::string::npos : text.find(close, start);
	if (end != std::string::npos) text.replace(start + open.size(), end - start - open.size(), value);
	else text += open + value + close + "\r\n";
}

static void writeConfig(const char* const* names, const char* const* values, int count)
{
	std::string text;
	FILE* f = fopen(clientConfigFile(), "rb");
	if (f)
	{
		char buffer[4096];
		size_t n;
		while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) text.append(buffer, n);
		fclose(f);
	}
	for (int i = 0; i < count; i++) setConfigText(text, names[i], values[i]);

	// Written next to it, then swapped in, so the client never reads a half written file
	std::string temp = std::string(clientConfigFile()) + ".tmp";
	f = fopen(temp.c_str(), "wb");
	if (!f) return;
	fwrite(text.data(), 1, text.size(), f);
	fclose(f);
	MoveFileExA(temp.c_str(), clientConfigFile(), MOVEFILE_REPLACE_EXISTING);
	clientReloadConfig();
}

static void saveController(int index)
{
	char vigem[8];
	snprintf(vigem, sizeof(vigem), "%u", controllers[index].vigem);
	const char* names[] = { "VIGEM_MODE", "VJOY_MODE" };
	const char* values[] = { vigem, controllers[index].vjoy ? "1" : "0" };
	writeConfig(names, values, 2);
	controller = index;
}

static void saveStreamMode(bool enabled)
{
	const char* names[] = { "STREAM_MODE" };
	const char* values[] = { enabled ? "1" : "0" };
	writeConfig(names, values, 1);
	stream_mode = enabled;
}

static void saveGyroSensitivity(float value)
{
	char text[16];
	snprintf(text, sizeof(text), "%.1f", value);
	const char* names[] = { "VIGEM_GYRO_SENSITIVITY" };
	const char* values[] = { text };
	writeConfig(names, values, 1);
	gyro_sensitivity = value;
}

// ---- Start with Windows ----

static bool autostartEnabled()
{
	HKEY key;
	if (RegOpenKeyExA(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
	bool found = RegQueryValueExA(key, RUN_VALUE, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
	RegCloseKey(key);
	return found;
}

static void setAutostart(bool enabled)
{
	HKEY key;
	if (RegOpenKeyExA(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
	if (enabled)
	{
		char path[MAX_PATH];
		GetModuleFileNameA(NULL, path, MAX_PATH);
		// --tray: start hidden in the notification area
		std::string command = std::string("\"") + path + "\" --tray";
		RegSetValueExA(key, RUN_VALUE, 0, REG_SZ, (const BYTE*)command.c_str(), (DWORD)command.size() + 1);
	}
	else RegDeleteValueA(key, RUN_VALUE);
	RegCloseKey(key);
}

// ---- Status ----

static void describe(const ClientStatus& status, std::string& title, std::string& details)
{
	char text[256];
	switch (status.state)
	{
	case CLIENT_CONNECTED:
		snprintf(text, sizeof(text), "Connected to %s", status.host);
		title = text;
		if (status.rate > 0)
			snprintf(text, sizeof(text), "%s, %d packets/s, longest gap %d ms\r\nVita battery %d%%  |  Sending as: %s",
				strcmp(status.link, "stream") == 0 ? "Streaming (UDP)" : "Polling (TCP)", status.rate, status.max_gap_ms,
				status.battery, status.output);
		else snprintf(text, sizeof(text), "Starting...\r\nSending as: %s", status.output);
		details = text;
		break;
	case CLIENT_CONNECTING:
		snprintf(text, sizeof(text), "Connecting to %s...", status.host);
		title = text;
		details = "";
		break;
	case CLIENT_STOPPED:
		title = "Stopped";
		details = "See the message below.";
		break;
	default:
		title = "Looking for your Vita...";
		details = "Open VitaPad on the Vita. Both devices must be on the same network.";
		break;
	}
}

static void setText(HWND control, const std::string& text)
{
	char current[512];
	GetWindowTextA(control, current, sizeof(current));
	if (text != current) SetWindowTextA(control, text.c_str());
}

static void syncControls();

// The config file can also be edited by hand (Key mapping... opens it): follow it
static void checkConfigFile()
{
	static time_t last = 0;
	struct stat info;
	if (stat(clientConfigFile(), &info) != 0 || info.st_mtime == last) return;
	bool first = last == 0;
	last = info.st_mtime;
	if (first) return;
	readConfig();
	syncControls();
}

static void refresh()
{
	checkConfigFile();
	ClientStatus status;
	clientGetStatus(&status);
	std::string title, details;
	describe(status, title, details);
	setText(controls[ID_STATUS], title);
	setText(controls[ID_DETAILS], details);
	setText(controls[ID_ERROR], status.error);
	ShowWindow(controls[ID_GET_VIGEM], strstr(status.error, "ViGEmBus") ? SW_SHOW : SW_HIDE);

	std::string tip = "VitaPad: " + title;
	if (strcmp(tray.szTip, tip.substr(0, sizeof(tray.szTip) - 1).c_str()) != 0)
	{
		snprintf(tray.szTip, sizeof(tray.szTip), "%s", tip.c_str());
		tray.uFlags = NIF_TIP;
		Shell_NotifyIconA(NIM_MODIFY, &tray);
	}

	// Balloon for problems, once each
	static std::string last_error;
	if (status.error[0] && last_error != status.error)
	{
		tray.uFlags = NIF_INFO;
		snprintf(tray.szInfoTitle, sizeof(tray.szInfoTitle), "VitaPad");
		snprintf(tray.szInfo, sizeof(tray.szInfo), "%s", status.error);
		tray.dwInfoFlags = NIIF_WARNING;
		Shell_NotifyIconA(NIM_MODIFY, &tray);
	}
	last_error = status.error;
}

static void updateGyroLabel()
{
	char text[16];
	snprintf(text, sizeof(text), "%.1fx", gyro_sensitivity);
	SetWindowTextA(controls[ID_GYRO_VALUE], text);
	// Gyro only exists on the DualShock 4
	bool ds4 = controllers[controller].vigem == 1;
	EnableWindow(controls[ID_GYRO], ds4);
	EnableWindow(controls[ID_GYRO_VALUE], ds4);
}

static void syncControls()
{
	SendMessageA(controls[ID_CONTROLLER], CB_SETCURSEL, controller, 0);
	SendMessageA(controls[ID_STREAM], BM_SETCHECK, stream_mode ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessageA(controls[ID_AUTOSTART], BM_SETCHECK, autostartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
	SendMessageA(controls[ID_GYRO], TBM_SETPOS, TRUE, (LPARAM)(gyro_sensitivity * 10 + 0.5f));
	updateGyroLabel();
}

// ---- Window ----

static HWND add(const char* cls, const char* text, DWORD style, int x, int y, int w, int h, int id, HFONT f = NULL)
{
	HWND control = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
		window, (HMENU)(INT_PTR)id, instance, NULL);
	SendMessageA(control, WM_SETFONT, (WPARAM)(f ? f : font), TRUE);
	if (id > 0 && id <= ID_HIDE) controls[id] = control;
	return control;
}

static void createControls()
{
	add("STATIC", "", SS_LEFT, 14, 12, 372, 20, ID_STATUS, bold_font);
	add("STATIC", "", SS_LEFT, 14, 34, 372, 34, ID_DETAILS);
	add("STATIC", "", SS_LEFT, 14, 74, 372, 32, ID_ERROR);
	add("BUTTON", "Get the ViGEmBus driver", BS_PUSHBUTTON, 14, 108, 170, 24, ID_GET_VIGEM);
	ShowWindow(controls[ID_GET_VIGEM], SW_HIDE);
	add("STATIC", "", SS_ETCHEDHORZ, 14, 142, 372, 2, 0);

	add("STATIC", "Controller:", SS_LEFT, 14, 157, 100, 20, 0);
	add("COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 118, 153, 268, 200, ID_CONTROLLER);
	for (int i = 0; i < CONTROLLERS_NUM; i++)
		SendMessageA(controls[ID_CONTROLLER], CB_ADDSTRING, 0, (LPARAM)controllers[i].label);
	add("BUTTON", "Low-latency streaming (UDP)", BS_AUTOCHECKBOX | WS_TABSTOP, 14, 188, 372, 22, ID_STREAM);
	add("STATIC", "Gyro sensitivity:", SS_LEFT, 14, 220, 100, 20, 0);
	add(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 112, 216, 222, 26, ID_GYRO);
	SendMessageA(controls[ID_GYRO], TBM_SETRANGE, TRUE, MAKELPARAM(1, 30)); // 0.1x - 3.0x
	add("STATIC", "", SS_LEFT, 340, 220, 46, 20, ID_GYRO_VALUE);
	add("BUTTON", "Start VitaPad with Windows", BS_AUTOCHECKBOX | WS_TABSTOP, 14, 250, 372, 22, ID_AUTOSTART);

	add("BUTTON", "Key mapping...", BS_PUSHBUTTON | WS_TABSTOP, 14, 284, 116, 28, ID_MAPPING);
	add("BUTTON", "3D viewer", BS_PUSHBUTTON | WS_TABSTOP, 138, 284, 116, 28, ID_VIEWER);
	add("BUTTON", "Hide", BS_PUSHBUTTON | WS_TABSTOP, 294, 284, 92, 28, ID_HIDE);
	syncControls();
}

static void showWindow()
{
	readConfig();
	syncControls();
	refresh();
	ShowWindow(window, SW_SHOW);
	ShowWindow(window, SW_RESTORE);
	SetForegroundWindow(window);
}

static void addTrayIcon()
{
	memset(&tray, 0, sizeof(tray));
	tray.cbSize = sizeof(tray);
	tray.hWnd = window;
	tray.uID = 1;
	tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	tray.uCallbackMessage = WM_APP_TRAY;
	tray.hIcon = icon;
	snprintf(tray.szTip, sizeof(tray.szTip), "VitaPad");
	Shell_NotifyIconA(NIM_ADD, &tray);
}

static void quit()
{
	Shell_NotifyIconA(NIM_DELETE, &tray);
	clientShutdown();
	ExitProcess(0);
}

static void showTrayMenu()
{
	readConfig();
	HMENU menu = CreatePopupMenu();
	AppendMenuA(menu, MF_STRING, ID_MENU_OPEN, "Open VitaPad");
	SetMenuDefaultItem(menu, ID_MENU_OPEN, FALSE);
	AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
	for (int i = 0; i < CONTROLLERS_NUM; i++)
		AppendMenuA(menu, MF_STRING | (i == controller ? MF_CHECKED : 0), ID_MENU_CONTROLLER + i, controllers[i].label);
	AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
	AppendMenuA(menu, MF_STRING | (stream_mode ? MF_CHECKED : 0), ID_MENU_STREAM, "Low-latency streaming (UDP)");
	AppendMenuA(menu, MF_STRING | (autostartEnabled() ? MF_CHECKED : 0), ID_MENU_AUTOSTART, "Start with Windows");
	AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
	AppendMenuA(menu, MF_STRING, ID_MENU_QUIT, "Quit VitaPad");

	POINT point;
	GetCursorPos(&point);
	SetForegroundWindow(window); // Needed for the menu to close when clicking elsewhere
	TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window, NULL);
	PostMessageA(window, WM_NULL, 0, 0);
	DestroyMenu(menu);
}

static void onCommand(int id, int code)
{
	if (id >= ID_MENU_CONTROLLER && id < ID_MENU_CONTROLLER + CONTROLLERS_NUM)
	{
		saveController(id - ID_MENU_CONTROLLER);
		syncControls();
		return;
	}
	switch (id)
	{
	case ID_CONTROLLER:
		if (code == CBN_SELCHANGE)
		{
			int index = (int)SendMessageA(controls[ID_CONTROLLER], CB_GETCURSEL, 0, 0);
			if (index >= 0 && index != controller) saveController(index);
			updateGyroLabel();
		}
		break;
	case ID_STREAM:
		saveStreamMode(SendMessageA(controls[ID_STREAM], BM_GETCHECK, 0, 0) == BST_CHECKED);
		break;
	case ID_MENU_STREAM:
		saveStreamMode(!stream_mode);
		syncControls();
		break;
	case ID_AUTOSTART:
		setAutostart(SendMessageA(controls[ID_AUTOSTART], BM_GETCHECK, 0, 0) == BST_CHECKED);
		break;
	case ID_MENU_AUTOSTART:
		setAutostart(!autostartEnabled());
		syncControls();
		break;
	case ID_MAPPING:
		ShellExecuteA(window, "open", "notepad.exe", clientConfigFile(), NULL, SW_SHOWNORMAL);
		break;
	case ID_VIEWER:
		if (!clientOpenViewer()) MessageBoxA(window, "The 3D viewer couldn't start (is port 5050 in use?).", "VitaPad", MB_ICONWARNING);
		break;
	case ID_GET_VIGEM:
		ShellExecuteA(window, "open", VIGEM_URL, NULL, NULL, SW_SHOWNORMAL);
		break;
	case ID_HIDE:
		ShowWindow(window, SW_HIDE);
		break;
	case ID_MENU_OPEN:
		showWindow();
		break;
	case ID_MENU_QUIT:
		quit();
		break;
	}
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
	case WM_COMMAND:
		onCommand(LOWORD(wparam), HIWORD(wparam));
		return 0;
	case WM_HSCROLL:
		if ((HWND)lparam == controls[ID_GYRO])
		{
			gyro_sensitivity = (float)SendMessageA(controls[ID_GYRO], TBM_GETPOS, 0, 0) / 10.0f;
			updateGyroLabel();
			// Saved once the slider is released
			if (LOWORD(wparam) == TB_ENDTRACK) saveGyroSensitivity(gyro_sensitivity);
		}
		return 0;
	case WM_CTLCOLORSTATIC:
		if ((HWND)lparam == controls[ID_ERROR])
		{
			HDC dc = (HDC)wparam;
			SetTextColor(dc, RGB(192, 0, 0));
			SetBkMode(dc, TRANSPARENT);
			return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
		}
		break;
	case WM_TIMER:
		refresh();
		return 0;
	case WM_APP_TRAY:
		if (lparam == WM_LBUTTONUP || lparam == WM_LBUTTONDBLCLK) showWindow();
		else if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) showTrayMenu();
		return 0;
	case WM_APP_SHOW:
		showWindow();
		return 0;
	case WM_CLOSE:
		// Closing only hides: VitaPad keeps running in the notification area
		ShowWindow(hwnd, SW_HIDE);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		// Explorer restarted: the tray icon has to be added again
		if (msg == taskbar_created && taskbar_created)
		{
			addTrayIcon();
			return 0;
		}
	}
	return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static DWORD WINAPI clientThread(LPVOID)
{
	clientMain(__argc, __argv);
	return 0;
}

int WINAPI WinMain(HINSTANCE hinstance, HINSTANCE, LPSTR, int)
{
	instance = hinstance;

	// Already running: show that one instead
	CreateMutexA(NULL, TRUE, INSTANCE_MUTEX);
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		HWND other = FindWindowA(WINDOW_CLASS, NULL);
		if (other) PostMessageA(other, WM_APP_SHOW, 0, 0);
		return 0;
	}

	// windows.xml and vita_ip.txt live next to the exe (when started with Windows the working
	// directory is somewhere else)
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	char* slash = strrchr(path, '\\');
	if (slash)
	{
		*slash = 0;
		SetCurrentDirectoryA(path);
	}

	bool start_hidden = false;
	for (int i = 1; i < __argc; i++)
		if (strcmp(__argv[i], "--tray") == 0) start_hidden = true;

	INITCOMMONCONTROLSEX common = { sizeof(common), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&common);

	HDC screen = GetDC(NULL);
	dpi = GetDeviceCaps(screen, LOGPIXELSY);
	ReleaseDC(NULL, screen);
	NONCLIENTMETRICSA metrics;
	metrics.cbSize = sizeof(metrics);
	SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	font = CreateFontIndirectA(&metrics.lfMessageFont);
	metrics.lfMessageFont.lfWeight = FW_BOLD;
	bold_font = CreateFontIndirectA(&metrics.lfMessageFont);

	icon = (HICON)LoadImageA(hinstance, "ID", IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
	if (!icon) icon = LoadIcon(NULL, IDI_APPLICATION);
	HICON big_icon = (HICON)LoadImageA(hinstance, "ID", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);

	WNDCLASSEXA wc;
	memset(&wc, 0, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = windowProc;
	wc.hInstance = hinstance;
	wc.hIcon = big_icon ? big_icon : icon;
	wc.hIconSm = icon;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.lpszClassName = WINDOW_CLASS;
	RegisterClassExA(&wc);

	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	RECT rect = { 0, 0, S(400), S(326) };
	AdjustWindowRect(&rect, style, FALSE);
	window = CreateWindowExA(0, WINDOW_CLASS, "VitaPad", style, CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, hinstance, NULL);

	readConfig();
	createControls();
	taskbar_created = RegisterWindowMessageA("TaskbarCreated");
	addTrayIcon();

	HANDLE thread = CreateThread(NULL, 0, clientThread, NULL, 0, NULL);
	if (thread) CloseHandle(thread);

	SetTimer(window, TIMER_REFRESH, REFRESH_MS, NULL);
	refresh();
	if (!start_hidden) showWindow();

	MSG msg;
	while (GetMessageA(&msg, NULL, 0, 0) > 0)
	{
		if (IsDialogMessageA(window, &msg)) continue;
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}
	quit();
	return 0;
}

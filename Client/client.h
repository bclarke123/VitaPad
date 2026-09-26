#pragma once

// The VitaPad client loop and its live state, used by the command-line client (main) and by the
// Windows tray app (gui.cpp), which runs the loop in a background thread

enum {
	CLIENT_STARTING = 0,
	CLIENT_SEARCHING,   // Looking for the Vita on the network
	CLIENT_CONNECTING,
	CLIENT_CONNECTED,
	CLIENT_STOPPED,     // Gave up (see error)
};

typedef struct {
	int state;          // CLIENT_*
	char host[64];      // Vita IP, when known
	const char* link;   // "stream" or "poll"
	int rate;           // Packets per second
	int max_gap_ms;     // Longest wait between two packets over the last second
	int battery;        // Vita battery percentage
	const char* output; // What the Vita's input becomes: "Xbox 360 controller", "DualShock 4", ...
	char error[256];    // Last problem worth showing, empty if none
} ClientStatus;

// Runs the client (never returns unless it gives up)
int clientMain(int argc, char** argv);

void clientGetStatus(ClientStatus* status);

// Reload the config file now (the GUI calls this after saving it)
void clientReloadConfig();

// Opens the 3D viewer in the browser (starting its server the first time)
bool clientOpenViewer();

// Releases the virtual controller before the process exits
void clientShutdown();

// Config file path (relative to the working directory)
const char* clientConfigFile();

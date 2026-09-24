#pragma once

#include "main.h"

// Live 3D view of the Vita in the browser, served on localhost

#define VIEWER_PORT 5050

// Starts the viewer server and opens it in the browser, returns false on failure
bool viewerStart(int port);

// Called with every packet received from the Vita
void viewerUpdate(const PadPacketV2& packet);

// Called when the connection to the Vita goes up or down
void viewerSetConnected(bool connected);

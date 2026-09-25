#pragma once

// Linux: a virtual Xbox 360 pad through uinput (needs write access to /dev/uinput)

#include "xbox.h"

bool uiInit();
bool uiSubmit(const XboxState* state);
void uiDestroy();

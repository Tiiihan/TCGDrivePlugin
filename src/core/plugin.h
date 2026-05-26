#pragma once

#include <windows.h>
#include "fsplugin.h"

// All Total Commander WFX entry-point prototypes live in fsplugin.h
// (now wrapped in extern "C" for C++ TUs). Definitions are in
// plugin.cpp; they delegate to FilesystemBridge and the API/auth
// layers. This header intentionally adds nothing on top of fsplugin.h
// so the C linkage of the entry points is preserved.

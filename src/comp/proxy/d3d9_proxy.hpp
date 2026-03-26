#pragma once

#include <d3d9.h>

// Initialize d3d9_remix.dll proxy
bool InitializeD3D9Proxy();

// Get the loaded d3d9_remix.dll module handle
HMODULE GetD3D9RemixModule();

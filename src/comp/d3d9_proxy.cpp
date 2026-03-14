#include "std_include.hpp"
#include "modules/d3d9ex.hpp"

// d3d9_remix.dll function pointers
typedef IDirect3D9* (WINAPI* PFN_Direct3DCreate9)(UINT SDKVersion);
typedef HRESULT(WINAPI* PFN_Direct3DCreate9Ex)(UINT SDKVersion, IDirect3D9Ex** ppD3D);

static HMODULE g_hD3D9Remix = nullptr;
static PFN_Direct3DCreate9 g_pDirect3DCreate9 = nullptr;
static PFN_Direct3DCreate9Ex g_pDirect3DCreate9Ex = nullptr;

// Initialize the proxy by loading d3d9_remix.dll
bool InitializeD3D9Proxy()
{
	if (g_hD3D9Remix)
		return true;

	g_hD3D9Remix = LoadLibraryW(L"d3d9_remix.dll");
	if (!g_hD3D9Remix)
	{
		shared::common::log("d3d9_proxy", "Failed to load d3d9_remix.dll", shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
		return false;
	}

	g_pDirect3DCreate9 = (PFN_Direct3DCreate9)GetProcAddress(g_hD3D9Remix, "Direct3DCreate9");
	g_pDirect3DCreate9Ex = (PFN_Direct3DCreate9Ex)GetProcAddress(g_hD3D9Remix, "Direct3DCreate9Ex");

	if (!g_pDirect3DCreate9)
	{
		shared::common::log("d3d9_proxy", "Failed to get Direct3DCreate9 from d3d9_remix.dll", shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
		FreeLibrary(g_hD3D9Remix);
		g_hD3D9Remix = nullptr;
		return false;
	}

	shared::common::log("d3d9_proxy", "Successfully loaded d3d9_remix.dll", shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);
	return true;
}

// Exported proxy functions
extern "C" {

	IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion)
	{
		if (!InitializeD3D9Proxy())
			return nullptr;

		// Get the real D3D9 interface from Remix
		IDirect3D9* pRealD3D = g_pDirect3DCreate9(SDKVersion);
		if (!pRealD3D)
			return nullptr;

		// Wrap it with our interception wrapper
		shared::common::log("d3d9_proxy", "Game is invoking 'Direct3DCreate9'. Creating wrapper interface.", shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);
		shared::globals::d3d9_interface = new comp::d3d9ex::_d3d9(pRealD3D);
		return shared::globals::d3d9_interface;
	}

	HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppD3D)
	{
		if (!InitializeD3D9Proxy())
			return E_FAIL;

		if (!g_pDirect3DCreate9Ex)
			return E_NOTIMPL;

		return g_pDirect3DCreate9Ex(SDKVersion, ppD3D);
	}

	// Utility function to get the loaded remix DLL module
	HMODULE GetD3D9RemixModule()
	{
		InitializeD3D9Proxy();
		return g_hD3D9Remix;
	}
}

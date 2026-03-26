#include "std_include.hpp"
#include "window.hpp"

/*
 * Borderless fullscreen for Halo CE.
 *
 * Halo loads d3d9.dll dynamically after its window is already created, so
 * there is no opportunity to intercept CreateWindowExA from DllMain.
 * Instead, comp::main() calls borderless::apply_to_window() once
 * find_game_window() has located the HWND — restyle and reposition the
 * existing window here.
 */

namespace comp::chimera::window
{
	void apply_to_window(HWND hwnd)
	{
		const int w = GetSystemMetrics(SM_CXSCREEN);
		const int h = GetSystemMetrics(SM_CYSCREEN);

		SetWindowLongA(hwnd, GWL_STYLE,   static_cast<LONG>(WS_POPUP | WS_VISIBLE));
		SetWindowLongA(hwnd, GWL_EXSTYLE, 0);

		SetWindowPos(hwnd, HWND_TOP,
		             0, 0, w, h,
		             SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOZORDER);

		shared::common::log("Borderless",
		    std::format("Applied borderless {}x{} to HWND {:p}", w, h, static_cast<void*>(hwnd)),
		    shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);
	}

	void apply_resolution_patches()
	{
		const UINT w = static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN));
		const UINT h = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN));

		// default_resolution_pc_sig (0x5169F1): C7 44 24 20 [W] C7 44 24 24 [H] C7 44 24 28 [Hz]
		// Overwrites the 800-wide DWORD at +4 and the 600-tall DWORD at +12.
		shared::utils::hook::set<DWORD>(reinterpret_cast<void*>(0x005169F5), w);  // +4
		shared::utils::hook::set<DWORD>(reinterpret_cast<void*>(0x005169FD), h);  // +12
		shared::common::log("Game",
			std::format("Resolution: default slot patched to {}x{}", w, h),
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// fallback_resolution_sig (0x51701A): NOP 16 bytes — removes the 800x600 fallback path.
		static const BYTE nop16[16] = {
			0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
			0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90
		};
		shared::utils::hook::set(reinterpret_cast<void*>(0x0051701A), nop16, sizeof(nop16));
		shared::common::log("Game", "Resolution: fallback 800x600 NOP'd",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// load_profile_resolution_sig (0x4956BE): 74 (jz) → EB (jmp)
		// Forces unconditional skip so the saved profile can't restore 640x480.
		shared::utils::hook::set(reinterpret_cast<void*>(0x004956BE), (BYTE)0xEB);
		shared::common::log("Game", "Resolution: profile-load resolution skip patched",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);
	}

} // namespace comp::chimera::window

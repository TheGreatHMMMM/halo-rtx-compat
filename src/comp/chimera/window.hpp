#pragma once

namespace comp::chimera::window
{
	// Retroactively restyle an already-created window to borderless fullscreen.
	// Called from comp::main() once shared::globals::main_window is valid.
	void apply_to_window(HWND hwnd);

	// Patch Halo's internal resolution config so the D3D viewport uses native
	// resolution. Mirrors Chimera's set_up_video_mode() strategy.
	// All addresses are v1.0.10 halo.exe (preferred base 0x400000, no ASLR).
	void apply_resolution_patches();

} // namespace comp::chimera::window

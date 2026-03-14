#include "std_include.hpp"
#include "shared/common/flags.hpp"

namespace comp::game
{
	// --------------
	// game variables

	//DWORD* d3d_dev_sample_addr = nullptr;
	//uintptr_t  CameraRenderMatrices_ptr_addr = 0; // example
	uintptr_t create_mouse_device_addr = 0;
	CameraRenderMatrices* CameraRenderMatrices_ptr = nullptr;

	// --------------
	// game functions

	// SampleTemplate_t SampleTemplate = nullptr;


	// --------------
	// game asm offsets

	//uint32_t retn_addr__func1 = 0u;
	//uint32_t nop_addr__func2 = 0u;
	//uint32_t retn_addr__pre_draw_something = 0u;
	//uint32_t hk_addr__post_draw_something = 0u;


	// --------------

#define PATTERN_OFFSET_SIMPLE(var, pattern, byte_offset, static_addr) \
		if (const auto offset = shared::utils::mem::find_pattern(##pattern, byte_offset, #var, use_pattern, static_addr); offset) { \
			(var) = offset; found_pattern_count++; \
		} total_pattern_count++;

#define PATTERN_OFFSET_DWORD_PTR_CAST_TYPE(var, type, pattern, byte_offset, static_addr) \
		if (const auto offset = shared::utils::mem::find_pattern(##pattern, byte_offset, #var, use_pattern, static_addr); offset) { \
			(var) = (type)*(DWORD*)offset; found_pattern_count++; \
		} total_pattern_count++;

	// init any adresses here
	void init_game_addresses()
	{
		
		const bool use_pattern = !shared::common::flags::has_flag("no_pattern");
		if (use_pattern) {
			shared::common::log("Game", "Getting offsets ...", shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);
		}

		std::uint32_t total_pattern_count = 0u;
		std::uint32_t found_pattern_count = 0u;


#pragma region GAME_VARIABLES

		// Find code that references the global var you are interested in, grab the address of the instruction + pattern
		// Figure out the byte offset that's needed until your global var address starts in the instruction 
		// -> 'mov eax, d3d_dev_sample_addr' == A1 D8 D8 7E 01 where A1 is the mov instruction and the following 4 bytes the addr of the global var -> so offset 1
		
		// Patterns are quite slow on DEBUG builds. The last argument in find_pattern allows you to declare a static offset which will be used
		// when the game gets started with `-no_pattern` in the commandline

		// ----

		// Example verbose
			//if (const auto offset = shared::utils::mem::find_pattern("? ? ? ? ?", 1, "d3d_dev_sample_addr", use_pattern, 0xDEADBEEF); offset) {
			//	d3d_dev_sample_addr = (DWORD*)*(DWORD*)offset; found_pattern_count++; // cast mem at offset
			//} total_pattern_count++;

		// Or via macro
			//PATTERN_OFFSET_DWORD_PTR_CAST_TYPE(d3d_dev_sample_addr, DWORD*, "? ? ? ? ?", 1, 0xDEADBEEF);

		PATTERN_OFFSET_DWORD_PTR_CAST_TYPE(
			CameraRenderMatrices_ptr,
			CameraRenderMatrices*,
			"81 ec a0 02 00 00 53 55 8b ac 24 ac 02 00 00 56 8b 35",
			0x5A,
			0
		);


		// Another example with a structure object
			//PATTERN_OFFSET_DWORD_PTR_CAST_TYPE(vp, some_struct_containing_matrices*, "? ? ? ? ?", 0, 0xDEADBEEF);

		// end GAME_VARIABLES
#pragma endregion

		// ---


#pragma region GAME_FUNCTIONS

		// cast func template
		//PATTERN_OFFSET_DWORD_PTR_CAST_TYPE(SampleTemplate, SampleTemplate_t, "? ? ? ? ?", 0, 0xDEADBEEF);

		// end GAME_FUNCTIONS
#pragma endregion

		// ---


#pragma region GAME_ASM_OFFSETS

		// Assembly offsets are simple offsets that do not require additional casting

		// Example verbose
			//if (const auto offset = shared::utils::mem::find_pattern(" ? ? ? ", 0, "nop_addr__func2", use_pattern, 0xDEADBEEF); offset) {
			//	nop_addr__func2 = offset; found_pattern_count++;
			//} total_pattern_count++;

		// Or via macro
			//PATTERN_OFFSET_SIMPLE(retn_addr__pre_draw_something, "? ? ? ?", 0, 0xDEADBEEF);
			//PATTERN_OFFSET_SIMPLE(hk_addr__post_draw_something, "? ? ? ?", 0, 0xDEADBEEF); 
			PATTERN_OFFSET_SIMPLE(
			create_mouse_device_addr,
			"6a 17 ff 15 ?? ?? ?? ?? 85 c0 74 ?? 66 c7 05 ?? ?? ?? ?? 02 00",
			0,
			0
		);

		// end GAME_ASM_OFFSETS
#pragma endregion


		if (use_pattern)
		{
			if (found_pattern_count == total_pattern_count) {
				shared::common::log("Game", std::format("Found all '{:d}' Patterns.", total_pattern_count), shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);
			}
			else
			{
				shared::common::log("Game", std::format("Only found '{:d}' out of '{:d}' Patterns.", found_pattern_count, total_pattern_count), shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
				shared::common::log("Game", ">> Please create an issue on GitHub and attach this console log and information about your game (version, platform etc.)\n", shared::common::LOG_TYPE::LOG_TYPE_STATUS, true);
			}
		}
	}

#undef PATTERN_OFFSET_SIMPLE

	// -----------------------------------------------------------------------
	// Memory patching helpers
	// -----------------------------------------------------------------------

	static void write_byte(uintptr_t addr, uint8_t value)
	{
		DWORD old_prot;
		VirtualProtect(reinterpret_cast<LPVOID>(addr), 1, PAGE_EXECUTE_READWRITE, &old_prot);
		*reinterpret_cast<uint8_t*>(addr) = value;
		if (old_prot != PAGE_EXECUTE_READWRITE)
		{
			DWORD dummy;
			VirtualProtect(reinterpret_cast<LPVOID>(addr), 1, old_prot, &dummy);
		}
	}


	// -----------------------------------------------------------------------
	// apply_patches
	// -----------------------------------------------------------------------

	void apply_patches()
	{
		// ----------------------------------------------------------------
		// P_DontStealMouse
		// ----------------------------------------------------------------
		// Halo CE passes DISCL_EXCLUSIVE | DISCL_FOREGROUND | DISCL_NOWINKEY
		// (0x0D or similar) to IDirectInputDevice8::SetCooperativeLevel for the
		// mouse device.  This grants Halo exclusive ownership, which:
		//   • Prevents RTX Remix from reading the cursor position.
		//   • Breaks any overlay or debugger that also wants the mouse.
		//
		// Patching byte +0x5B to 0x06 changes the flag to
		// DISCL_NONEXCLUSIVE (0x02) | DISCL_FOREGROUND (0x04) = 0x06.
		// The mouse still works in-game (Halo reads raw axis deltas, not
		// absolute position) but no longer blocks other applications.
		//
		// Insight from Halo CE VR mod (Hooks.cpp :: P_DontStealMouse):
		//   SetByte(o.CreateMouseDevice.Address + 0x5B, 6);
		if (create_mouse_device_addr)
		{
			write_byte(create_mouse_device_addr + 0x5B, 0x06);
			shared::common::log("Game",
				"P_DontStealMouse: patched DirectInput to DISCL_NONEXCLUSIVE | DISCL_FOREGROUND",
				shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);
		}
		else
		{
			shared::common::log("Game",
				"P_DontStealMouse: CreateMouseDevice address not found – skipping patch",
				shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
		}

		// ----------------------------------------------------------------
		// NoCull – disable frustum/portal/PVS culling for RTX raytracing
		// Allows geometry behind the camera to render (reflections/mirrors).
		// ----------------------------------------------------------------

		// DebugNoFrustumClip: set debug_no_frustum_clip = true
		// Skips screen-space frustum clip in FrustumClip_ComputeScreenBounds (0x50CB70)
		shared::utils::hook::set<uint8_t>(0x00710321, 0x01);
		shared::common::log("Game", "NoCull: DebugNoFrustumClip applied",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// AllClustersVisible: NOP the PVS gate 'je' in PortalTraversal_Recursive
		// Allows traversal to enter every portal-connected cluster regardless of PVS data
		shared::utils::hook::nop(0x00554758, 6);
		shared::common::log("Game", "NoCull: AllClustersVisible applied",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// TraversalPortalTestAlwaysVisible: stub PortalPlane_TestWrapper with 'mov eax, 2; ret'
		// Portal test during recursive cluster traversal — returning 2 (fully visible) makes every portal pass
		shared::utils::hook::set(reinterpret_cast<void*>(0x005549C0), (BYTE)0xB8, (BYTE)0x02, (BYTE)0x00, (BYTE)0x00, (BYTE)0x00, (BYTE)0xC3);
		shared::common::log("Game", "NoCull: TraversalPortalTestAlwaysVisible applied",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// PortalTestAlwaysVisible: replace Portal_FrustumTest with 'mov eax, 2; ret'
		// Portals always fully visible, including behind camera
		shared::utils::hook::set(reinterpret_cast<void*>(0x0050D5B0), (BYTE)0xB8, (BYTE)0x02, (BYTE)0x00, (BYTE)0x00, (BYTE)0x00, (BYTE)0xC3);
		shared::common::log("Game", "NoCull: PortalTestAlwaysVisible applied",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// LeafPortalTestAlwaysVisible: replace LeafPortal_FrustumTest with 'xor eax, eax; ret'
		// Returns 0 = surface visible (bypasses frustum plane test)
		shared::utils::hook::set(reinterpret_cast<void*>(0x0050D4C0), (BYTE)0x31, (BYTE)0xC0, (BYTE)0xC3);
		shared::common::log("Game", "NoCull: LeafPortalTestAlwaysVisible applied",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		shared::utils::hook::set(reinterpret_cast<void*>(0x5413D9), (BYTE)0xEB);
		shared::utils::hook::set(reinterpret_cast<void*>(0x515964), (BYTE)0x00, (BYTE)0x00, (BYTE)0x00, (BYTE)0x80);
		shared::utils::hook::nop(0x5159EC, 47);

	}

}

namespace comp::game::Helpers {

CameraRenderMatrices* GetActiveCameraMatrices() {
    return CameraRenderMatrices_ptr;
}

}

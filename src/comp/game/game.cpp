#include "std_include.hpp"
#include "shared/common/flags.hpp"
#include "shared/common/config.hpp"
#include "chimera/extend_limits.hpp"
#include "chimera/window.hpp"

namespace comp::game
{

	uintptr_t create_mouse_device_addr = 0;
	CameraRenderMatrices* CameraRenderMatrices_ptr = nullptr;

	// Address of Halo's global PointLightTable* variable, found via Chimera's
	// light_table_sig pattern.  We store a pointer-to-pointer so the table
	// address is re-read each frame (the engine resets it on map load/unload).
	static PointLightTable** s_light_table_pp = nullptr;


#define PATTERN_OFFSET_SIMPLE(var, pattern, byte_offset, static_addr) \
		if (const auto offset = shared::utils::mem::find_pattern(##pattern, byte_offset, #var, use_pattern, static_addr); offset) { \
			(var) = offset; found_pattern_count++; \
		} total_pattern_count++;

#define PATTERN_OFFSET_DWORD_PTR_CAST_TYPE(var, type, pattern, byte_offset, static_addr) \
		if (const auto offset = shared::utils::mem::find_pattern(##pattern, byte_offset, #var, use_pattern, static_addr); offset) { \
			(var) = (type)*(DWORD*)offset; found_pattern_count++; \
		} total_pattern_count++;

	void init_game_addresses()
	{
		
		const bool use_pattern = !shared::common::flags::has_flag("no_pattern");
		if (use_pattern) {
			shared::common::log("Game", "Getting offsets ...", shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);
		}

		std::uint32_t total_pattern_count = 0u;
		std::uint32_t found_pattern_count = 0u;


#pragma region GAME_VARIABLES

		PATTERN_OFFSET_DWORD_PTR_CAST_TYPE(
			CameraRenderMatrices_ptr,
			CameraRenderMatrices*,
			"81 ec a0 02 00 00 53 55 8b ac 24 ac 02 00 00 56 8b 35",
			0x5A,
			0
		);

		// Chimera light_table_sig: MOV ECX, [point_light_data_table_ptr]
		// Offset +2 reaches the inline 4-byte address embedded in the MOV opcode.
		// One DWORD read from that address gives &(PointLightTable*) in .data.
		PATTERN_OFFSET_DWORD_PTR_CAST_TYPE(
			s_light_table_pp,
			PointLightTable**,
			"8B 0D ?? ?? ?? ?? 8B 51 34 56 8B F0 81 E6 FF FF 00 00 6B F6 7C",
			2,
			0
		);

#pragma endregion // GAME_VARIABLES


#pragma region GAME_FUNCTIONS

#pragma endregion // GAME_FUNCTIONS


#pragma region GAME_ASM_OFFSETS

			PATTERN_OFFSET_SIMPLE(
			create_mouse_device_addr,
			"6a 17 ff 15 ?? ?? ?? ?? 85 c0 74 ?? 66 c7 05 ?? ?? ?? ?? 02 00",
			0,
			0
		);

#pragma endregion // GAME_ASM_OFFSETS


		// Read light config now that patterns are resolved and config is loaded.
		{
			auto& cfg = shared::common::config::get();
			lights_enabled      = cfg.get_int("Lights", "Enabled",           1) != 0;
			light_intensity     = static_cast<float>(cfg.get_int("Lights", "IntensityPercent", 100)) / 100.0f;
			light_range_default = cfg.get_int("Lights", "RangeDefault",      10);
			flashlight_enabled  = cfg.get_int("Lights", "FlashlightEnabled", 1) != 0;
			flashlight_range    = cfg.get_int("Lights", "FlashlightRange",   8);
			shared::common::log("Game", std::format("Lights: enabled={} intensity={:.0f}% rangeDefault={} flashlight={} (NOTE: flashlight fix unresolved)",
				lights_enabled, light_intensity * 100.0f, light_range_default, flashlight_enabled));
		}

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


	void apply_patches()
	{
		// ----------------------------------------------------------------
		// Chimera engine limit extensions
		// ----------------------------------------------------------------
		comp::chimera::extend_limits();


		// ----------------------------------------------------------------
		// P_DontStealMouse pulled from the Halo CE VR mod
		// ----------------------------------------------------------------
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
		// NoCull – disable frustum/portal/PVS culling
		// ----------------------------------------------------------------

		// debug_no_frustum_clip
		shared::utils::hook::set(reinterpret_cast<void*>(0x0050CB75), (BYTE)0xB0, (BYTE)0x01, (BYTE)0x90, (BYTE)0x90, (BYTE)0x90);
		shared::common::log("Game", "NoCull: debug_no_frustum_clip applied",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// AABB frustum test
		shared::utils::hook::set(reinterpret_cast<void*>(0x0050D5B0), (BYTE)0x31, (BYTE)0xC0, (BYTE)0xB0, (BYTE)0x02, (BYTE)0xC3, (BYTE)0x90);
		shared::common::log("Game", "NoCull: AABB frustum test applied",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// Fix: prevent stack overflow in fcn.00458BF0 (light pre-computation).
		// 0x00458E52: TEST AX,AX (85) -> XOR AX,AX (31); forces je at 0x458E54
		// to always skip the 78-float array write.  BSP rasterizer unaffected.
		write_byte(0x00458E52u, 0x31u);
		shared::common::log("Game", "NoCull: AABB light-array overflow guard applied (0x00458E52)",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// Point frustum test
		shared::utils::hook::set(reinterpret_cast<void*>(0x0050D4C0), (BYTE)0x31, (BYTE)0xC0, (BYTE)0xC3);
		shared::common::log("Game", "NoCull: Point frustum test applied",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// Portal visibility test
		shared::utils::hook::set(reinterpret_cast<void*>(0x005549C0), (BYTE)0xB8, (BYTE)0x02, (BYTE)0x00, (BYTE)0x00, (BYTE)0x00, (BYTE)0xC3);
		shared::common::log("Game", "NoCull: Portal visibility test applied",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// PVS gate in portal walk
		shared::utils::hook::set(reinterpret_cast<void*>(0x00554758), (BYTE)0x90, (BYTE)0x90, (BYTE)0x90, (BYTE)0x90, (BYTE)0x90, (BYTE)0x90);	
		shared::common::log("Game", "NoCull: PVS gate in portal walk applied",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// Bounding sphere frustum test
		shared::utils::hook::set(reinterpret_cast<void*>(0x0050D890), (BYTE)0x31, (BYTE)0xC0, (BYTE)0xB0, (BYTE)0x02, (BYTE)0xC3, (BYTE)0x90);
		shared::common::log("Game", "NoCull: Bounding sphere frustum test applied",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

		// DFS backtrack fix
		shared::utils::hook::set(reinterpret_cast<void*>(0x0055483C), (BYTE)0x90, (BYTE)0x90, (BYTE)0x90);
		shared::common::log("Game", "NoCull: DFS backtrack fix applied",
			shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);


		// ----------------------------------------------------------------
		// Resolution
		// ----------------------------------------------------------------
		comp::chimera::window::apply_resolution_patches();

	}


	// ================================================================
	// update_lights — inject runtime point lights each BeginScene
	// ================================================================
	//
	// Reads Halo's PointLightTable (located via Chimera's light_table_sig),
	// maps active entries to D3DLIGHT9 structures, and calls SetLight +
	// LightEnable so RTX Remix can path-trace them.
	//
	// World-space note: Halo CE sets a proper D3DTS_VIEW matrix.  D3DLIGHT9
	// Position is in world space — no camera-offset needed (contrast with FNV).
	void update_lights(IDirect3DDevice9* dev)
	{
		if (!lights_enabled || lights_updated_frame)
			return;
		if (!s_light_table_pp)
			return;

		const PointLightTable* tbl = *s_light_table_pp;
		if (!tbl || !tbl->first_element)
			return;

		lights_updated_frame = true;
		int injected = 0;
		const uint16_t sz = tbl->current_size;

		for (uint16_t idx = 0; idx < sz && injected < MAX_EXTRACTED_LIGHTS; ++idx)
		{
			const PointLightEntry& e = tbl->first_element[idx];
			if (e.id == 0) continue;   // free slot

			D3DLIGHT9 light = {};
			light.Type = D3DLIGHT_POINT;
			light.Diffuse.r = e.red   * light_intensity;
			light.Diffuse.g = e.green * light_intensity;
			light.Diffuse.b = e.blue  * light_intensity;
			light.Diffuse.a = 1.0f;
			light.Position.x = e.x;
			light.Position.y = e.y;
			light.Position.z = e.z;
			light.Range        = static_cast<float>(light_range_default);
			light.Attenuation0 = 0.0f;
			light.Attenuation1 = 0.0f;
			light.Attenuation2 = 1.0f;  // inverse-square falloff

			dev->SetLight(static_cast<DWORD>(injected), &light);
			dev->LightEnable(static_cast<DWORD>(injected), TRUE);
			++injected;
		}

		// Disable slots left over from the previous frame's higher light count.
		for (int j = injected; j < last_enabled_lights; ++j)
			dev->LightEnable(static_cast<DWORD>(j), FALSE);

		last_enabled_lights = injected;
	}

}

namespace comp::game::Helpers {

	CameraRenderMatrices* GetActiveCameraMatrices() {
		return CameraRenderMatrices_ptr;
	}

}

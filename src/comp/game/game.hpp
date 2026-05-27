#pragma once
#include "structs.hpp"

namespace comp::game
{
	// --------------
	// game variables

	//extern DWORD* d3d_dev_sample_addr;
	
	//inline IDirect3DDevice9* get_d3d_device() {
	//	return reinterpret_cast<IDirect3DDevice9*>(*d3d_dev_sample_addr);
	//}

	extern CameraRenderMatrices* CameraRenderMatrices_ptr;


	// --------------
	// game functions

	//typedef	void (__cdecl* SampleTemplate_t)(uint32_t arg1, uint32_t arg2);
	//	extern SampleTemplate_t SampleTemplate;


	// --------------
	// game asm offsets

	//extern uint32_t retn_addr__func1;
	//extern uint32_t nop_addr__func2;
	//extern uint32_t retn_addr__pre_draw_something;
	//extern uint32_t hk_addr__post_draw_something;

	// ---

	// --- Point light injection ---

	// Called once per BeginScene; injects active point lights into the D3D9
	// device so RTX Remix can path-trace them.  Guard: lights_updated_frame
	// prevents double-injection when BeginScene fires multiple times per frame.
	void update_lights(IDirect3DDevice9* dev);

	inline bool  lights_enabled       = true;
	inline float light_intensity      = 1.0f;   // multiplied into Diffuse.rgb
	inline int   light_range_default  = 10;     // world units (1 unit ≈ 10 ft in Halo)
	inline bool  lights_updated_frame = false;  // reset by Present(); prevents re-injection
	inline int   last_enabled_lights  = 0;      // stale-light disable watermark

	// --- Flashlight (INVESTIGATION NEEDED — fix did not work) ---
	// Hypothesis was: game calls SetLight with D3DLIGHT_SPOT for the flashlight and RTX
	// Remix doesn't path-trace spotlights, so we'd mirror as D3DLIGHT_POINT.
	// Outcome: no D3DLIGHT_SPOT SetLight calls were observed during testing with the
	// flashlight active.  The game likely renders the flashlight via a projected texture
	// or shader effect that never touches the D3D9 light pipeline.  The mirror code in
	// d3d9ex.cpp is inert.  Root cause still unknown; needs live tracing to find the
	// actual flashlight rendering path (projected texture stage? custom shader?).
	inline bool flashlight_enabled = true;
	inline int  flashlight_range   = 8;   // retained for future use when root cause is found

	// ---

	extern void init_game_addresses();
	extern void apply_patches();
}

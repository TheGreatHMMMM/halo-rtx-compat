#include "std_include.hpp"
#include "renderer.hpp"

#include "imgui.hpp"
#include "skinning.hpp"
#include "game/game.hpp"
#include "shared/common/ffp_state.hpp"

namespace
{
	// Saves and restores alpha blend + Z-write states around decal draws.
	// Decal geometry (POSITION + D3DCOLOR UV encoding, no TEXCOORD) uses the vertex
	// shader to handle alpha in the original pipeline; in FFP mode the game does not
	// set ALPHABLENDENABLE, so we must force it for correct RTX Remix transparency.
	struct decal_blend_guard
	{
		IDirect3DDevice9* dev = nullptr;
		DWORD ab = FALSE, src = D3DBLEND_ONE, dst = D3DBLEND_ZERO, zw = TRUE;

		void push(IDirect3DDevice9* d)
		{
			dev = d;
			d->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
			d->GetRenderState(D3DRS_SRCBLEND,         &src);
			d->GetRenderState(D3DRS_DESTBLEND,        &dst);
			d->GetRenderState(D3DRS_ZWRITEENABLE,     &zw);
			d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
			d->SetRenderState(D3DRS_SRCBLEND,         D3DBLEND_SRCALPHA);
			d->SetRenderState(D3DRS_DESTBLEND,        D3DBLEND_INVSRCALPHA);
			d->SetRenderState(D3DRS_ZWRITEENABLE,     FALSE);
		}

		void pop()
		{
			if (!dev) return;
			dev->SetRenderState(D3DRS_ALPHABLENDENABLE, ab);
			dev->SetRenderState(D3DRS_SRCBLEND,         src);
			dev->SetRenderState(D3DRS_DESTBLEND,        dst);
			dev->SetRenderState(D3DRS_ZWRITEENABLE,     zw);
		}
	};
}

namespace comp
{
	int g_is_rendering_something = 0;
	bool g_rendered_first_primitive = false;
	bool g_applied_hud_hack = false; // was hud "injection" applied this frame

	namespace tex_addons
	{
		bool initialized = false;
		LPDIRECT3DTEXTURE9 berry = nullptr;

		void init_texture_addons(bool release)
		{
			if (release)
			{
				if (tex_addons::berry) tex_addons::berry->Release();
				return;
			}

			shared::common::log("Renderer", "Loading CompMod Textures ...", shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);

			auto load_texture = [](IDirect3DDevice9* dev, const char* path, LPDIRECT3DTEXTURE9* tex)
				{
					HRESULT hr;
					hr = D3DXCreateTextureFromFileA(dev, path, tex);
					if (FAILED(hr)) shared::common::log("Renderer", std::string("Failed to load ") + path, shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
				};

			const auto dev = shared::globals::d3d_device;
			load_texture(dev, "rtx_comp\\textures\\berry.png", &tex_addons::berry);
			tex_addons::initialized = true;
		}
	}


	// ----

	drawcall_mod_context& setup_context(IDirect3DDevice9* dev)
	{
		auto& ctx = renderer::dc_ctx;
		ctx.info.device_ptr = dev;

		// any additional info about the current drawcall here

		return ctx;
	}


	// ----

		HRESULT renderer::on_draw_primitive(IDirect3DDevice9* dev, const D3DPRIMITIVETYPE& PrimitiveType, const UINT& StartVertex, const UINT& PrimitiveCount)
	{
		if (!g_rendered_first_primitive) {
			g_rendered_first_primitive = true;
		}

		if (!is_initialized() || shared::globals::imgui_is_rendering) {
			return dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		}

		static auto im = imgui::get();
		im->m_stats._drawcall_prim_incl_ignored.track_single();

		auto& ctx = setup_context(dev);
		auto& ffp = shared::common::ffp_state::get();
		ffp.increment_draw_count();

		auto hr = S_OK;

		/*
		 * FFP draw routing for non-indexed draws.
		 *
		 * GAME-SPECIFIC: Adjust conditions if your game's non-indexed draws
		 * include world geometry that should be converted to FFP.
		 */
		if (ffp.is_enabled() && ffp.view_proj_valid() &&
			ffp.last_decl() && !ffp.cur_decl_has_pos_t() && !ffp.cur_decl_is_skinned())
		{
			// World-space non-indexed draw: engage FFP
			ffp.engage(dev);
			ffp.setup_albedo_texture(dev);

			const bool decl_patched = ffp.try_patch_decl_for_color_uv(dev);
			//if (decl_patched) {
			//	UINT nv;
			//	switch (PrimitiveType) {
			//		case D3DPT_POINTLIST:    nv = PrimitiveCount; break;
			//		case D3DPT_LINELIST:     nv = PrimitiveCount * 2; break;
			//		case D3DPT_LINESTRIP:    nv = PrimitiveCount + 1; break;
			//		case D3DPT_TRIANGLELIST: nv = PrimitiveCount * 3; break;
			//		default:                 nv = PrimitiveCount + 2; break; // strip/fan
			//	}
			//	ffp.prepare_uv_stream(dev, StartVertex, nv);
			//}
			decal_blend_guard blend_guard;
			if (decl_patched) blend_guard.push(dev);
			hr = dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
			if (decl_patched) { blend_guard.pop(); ffp.restore_patched_decl(dev); }
			im->m_stats._drawcall_prim.track_single();
		}
		else
		{
			// Passthrough: POSITIONT / no decl / pre-viewProj / skinned / FFP disabled
			ffp.disengage(dev);
			hr = dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
			im->m_stats._drawcall_prim.track_single();
			im->m_stats._drawcall_using_vs.track_single();
		}

		ctx.restore_all(dev);
		ctx.reset_context();

		return hr;
	}


	// ----

	HRESULT renderer::on_draw_indexed_prim(IDirect3DDevice9* dev, const D3DPRIMITIVETYPE& PrimitiveType, const INT& BaseVertexIndex, const UINT& MinVertexIndex, const UINT& NumVertices, const UINT& startIndex, const UINT& primCount)
	{
		if (!is_initialized() || shared::globals::imgui_is_rendering) {
			return dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
		}

		auto& ctx = setup_context(dev);
		const auto im = imgui::get();
		auto& ffp = shared::common::ffp_state::get();
		ffp.increment_draw_count();

		im->m_stats._drawcall_indexed_prim_incl_ignored.track_single();

		if (ctx.modifiers.do_not_render)
		{
			ctx.restore_all(dev);
			ctx.reset_context();
			return S_OK;
		}

		auto hr = S_OK;

		/*
		 * FFP draw routing for indexed draws — the main conversion path.
		 *
		 * Decision tree (port of d3d9_device.c WD_DrawIndexedPrimitive):
		 *   viewProjValid?
		 *   +-- NO  -> passthrough with shaders
		 *   +-- YES
		 *       +-- curDeclIsSkinned?
		 *       |   +-- YES + skinning module -> skinning::draw_skinned_dip()
		 *       |   +-- YES + no skinning     -> passthrough with shaders
		 *       +-- !curDeclHasNormal?
		 *       |   +-- passthrough (HUD/UI)
		 *       |   GAME-SPECIFIC: remove this filter if world geometry lacks NORMAL
		 *       +-- else (rigid 3D mesh)
		 *           +-- FFP engage + draw + restore
		 */
		if (!ffp.is_enabled() || !ffp.view_proj_valid())
		{
			// Transforms not ready or FFP disabled: passthrough with shaders
			hr = dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			im->m_stats._drawcall_indexed_prim.track_single();
			im->m_stats._drawcall_indexed_prim_using_vs.track_single();
		}
		else if (ffp.cur_decl_is_skinned())
		{
			// Skinned mesh: route through skinning module when available (matching Skin_DrawDIP
			// in proxy-minimal). Falls back to FFP engage (not shader passthrough) if module
			// is absent — Halo CE BSP vertices carry BLENDWEIGHT+BLENDINDICES, making them
			// appear "skinned". Without this, BSP bypasses FFP and RTX Remix captures it
			// via vertex shader capture instead of fixed-function.
			if (skinning::is_available())
			{
				hr = skinning::get()->draw_skinned_dip(dev,
					PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
				im->m_stats._drawcall_indexed_prim.track_single();
			}
			else
			{
				// No skinning module: treat as rigid FFP (D3DVBF_1WEIGHTS applies
				// 100% of WORLDMATRIX(0) via the BLENDWEIGHT=1.0 vertex element).
				ffp.engage(dev);
				ffp.setup_albedo_texture(dev);

				const bool decl_patched = ffp.try_patch_decl_for_color_uv(dev);
				//if (decl_patched) {
				//	const INT fv = static_cast<INT>(MinVertexIndex) + BaseVertexIndex;
				//	if (fv >= 0) ffp.prepare_uv_stream(dev, static_cast<UINT>(fv), NumVertices);
				//}
				decal_blend_guard blend_guard;
				if (decl_patched) blend_guard.push(dev);
				hr = dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
				if (decl_patched) { blend_guard.pop(); ffp.restore_patched_decl(dev); }
				im->m_stats._drawcall_indexed_prim.track_single();
			}
		}
		else if (ffp.cur_decl_has_pos_t())
		{
			// Pre-transformed (screen-space) geometry — HUD, menus. Never convert to FFP.
			ffp.disengage(dev);
			hr = dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			im->m_stats._drawcall_indexed_prim.track_single();
			im->m_stats._drawcall_indexed_prim_using_vs.track_single();
		}
		else
		{
			// Rigid 3D mesh with NORMAL: engage FFP conversion
			ffp.engage(dev);
			if (ffp.skinning_setup() && skinning::is_available())
				skinning::get()->disable_skinning(dev);
			ffp.setup_albedo_texture(dev);

			const bool decl_patched = ffp.try_patch_decl_for_color_uv(dev);
			//if (decl_patched) {
			//	const INT fv = static_cast<INT>(MinVertexIndex) + BaseVertexIndex;
			//	if (fv >= 0) ffp.prepare_uv_stream(dev, static_cast<UINT>(fv), NumVertices);
			//}
			decal_blend_guard blend_guard;
			if (decl_patched) blend_guard.push(dev);
			hr = dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			if (decl_patched) { blend_guard.pop(); ffp.restore_patched_decl(dev); }
			im->m_stats._drawcall_indexed_prim.track_single();
		}

		ctx.restore_all(dev);
		ctx.reset_context();

		return hr;
	}

	// ---

	void renderer::manually_trigger_remix_injection(IDirect3DDevice9* dev)
	{
		if (!m_triggered_remix_injection)
		{
			auto& ctx = dc_ctx;

			dev->SetRenderState(D3DRS_FOGENABLE, FALSE);

			ctx.save_vs(dev);
			dev->SetVertexShader(nullptr);
			ctx.save_ps(dev);
			dev->SetPixelShader(nullptr);

			ctx.save_rs(dev, D3DRS_ZWRITEENABLE);
			dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);

			struct CUSTOMVERTEX
			{
				float x, y, z, rhw;
				D3DCOLOR color;
			};

			const auto color = D3DCOLOR_COLORVALUE(0, 0, 0, 0);
			const auto w = -0.49f;
			const auto h = -0.495f;

			CUSTOMVERTEX vertices[] =
			{
				{ -0.5f, -0.5f, 0.0f, 1.0f, color },
				{     w, -0.5f, 0.0f, 1.0f, color },
				{ -0.5f,     h, 0.0f, 1.0f, color },
				{     w,     h, 0.0f, 1.0f, color }
			};

			dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(CUSTOMVERTEX));

			ctx.restore_vs(dev);
			ctx.restore_ps(dev);
			ctx.restore_render_state(dev, D3DRS_ZWRITEENABLE);
			m_triggered_remix_injection = true;
		}
	}


	// ---
	// GAME-SPECIFIC: Assembly stubs for per-object hooks.
	// Use these when you need to detect render boundaries (e.g., "all skinned draws
	// happen between these two function calls") to set g_is_rendering_something.

	/*__declspec (naked) void pre_render_something_stub()
	{
		__asm
		{
			mov     ebx, ecx;
			cmp     eax, 0xFFFFFFFF;
			mov		g_is_rendering_something, 1;
			jmp		game::retn_addr__pre_draw_something;
		}
	}*/

	/*__declspec (naked) void post_render__something_stub()
	{
		__asm
		{
			mov		g_is_rendering_something, 0;
			retn    0x10;
		}
	}*/

	// ---

	renderer::renderer()
	{
		p_this = this;

		// Initialize FFP state tracker with real (unwrapped) device
		shared::common::ffp_state::get().init(shared::globals::d3d_real_device);

		// Feed the live Halo CE camera matrix struct so apply_transforms can read
		// view/proj directly from game memory each frame (matches proxy-minimal approach).
		shared::common::ffp_state::get().set_camera_base(comp::game::CameraRenderMatrices_ptr);

		// GAME-SPECIFIC: Create hooks as required.
		// See documentation for per-object hook examples.

		m_initialized = true;
		shared::common::log("Renderer", "Module initialized.", shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);
	}

	renderer::~renderer()
	{
		tex_addons::init_texture_addons(true);
	}
}

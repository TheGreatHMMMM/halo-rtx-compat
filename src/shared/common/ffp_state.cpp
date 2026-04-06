// SPDX-License-Identifier: MIT
// Copyright (c) 2026 github.com/xoxor4d
// Copyright (c) 2026 Kim2091
// Original author: xoxor4d (https://github.com/xoxor4d/remix-comp-base)
// Incorporated via Ekozmaster's MIT-licensed project and Kim2091's fork
// See THIRD-PARTY-NOTICES.md for the full license texts

#include "std_include.hpp"
#include "ffp_state.hpp"
#include "../../comp/game/game.hpp"
#include "shared/common/flags.hpp"

namespace shared::common
{
	namespace
	{
		constexpr UINT kWorldRegStart = 29;
		constexpr UINT kWorldRegEnd = 32;
	}

	ffp_state& ffp_state::get()
	{
		static ffp_state instance;
		return instance;
	}

	void ffp_state::init(IDirect3DDevice9* /*real_device*/)
	{
		cfg_ = &config::get().ffp;
		enabled_ = cfg_->enabled;
		create_tick_ = GetTickCount();
		camera_base_ = comp::game::CameraRenderMatrices_ptr;

		log("FFP", std::string("State tracker initialized, FFP=") + (enabled_ ? "ON" : "OFF"));
	}

	// ---- State mutators ----

	void ffp_state::on_set_vs_const_f(UINT start_reg, const float* data, UINT count)
	{
		if (!data || start_reg + count > 256) return;

		std::memcpy(&vs_const_[start_reg * 4], data, count * 4 * sizeof(float));

		// Dirty tracking keyed to game-specific register layout
		UINT end_reg = start_reg + count;
		if (start_reg < static_cast<UINT>(cfg_->vs_reg_proj_end) &&
			end_reg > static_cast<UINT>(cfg_->vs_reg_view_start))
		{
			view_proj_dirty_ = true;
		}
		if (start_reg == kWorldRegStart && count == kWorldRegEnd - kWorldRegStart)
		{
			world_dirty_ = true;
		}

		// Mark View+Proj valid once both ranges have been written
		auto view_start = static_cast<UINT>(cfg_->vs_reg_view_start);
		auto proj_start = static_cast<UINT>(cfg_->vs_reg_proj_start);
		auto proj_end = static_cast<UINT>(cfg_->vs_reg_proj_end);

		if (start_reg <= proj_start && start_reg + count >= proj_end)
		{
			view_proj_valid_ = true;
		}
		else if (start_reg == view_start && count >= 4 && vs_const_write_log_[proj_start])
		{
			view_proj_valid_ = true;
		}
		else if (start_reg == proj_start && count >= 4 && vs_const_write_log_[view_start])
		{
			view_proj_valid_ = true;
		}

		for (UINT i = 0; i < count; i++)
		{
			if (start_reg + i < 256)
				vs_const_write_log_[start_reg + i] = 1;
		}

		// Bone palette detection (for skinning module)
		if (start_reg >= static_cast<UINT>(cfg_->vs_reg_bone_threshold) &&
			count >= static_cast<UINT>(cfg_->vs_bone_min_regs) &&
			(count % static_cast<UINT>(cfg_->vs_regs_per_bone)) == 0)
		{
			bone_start_reg_ = static_cast<int>(start_reg);
			num_bones_ = static_cast<int>(count) / cfg_->vs_regs_per_bone;
		}
	}

	void ffp_state::on_set_ps_const_f(UINT start_reg, const float* data, UINT count)
	{
		if (!data || start_reg + count > 32) return;

		std::memcpy(&ps_const_[start_reg * 4], data, count * 4 * sizeof(float));
		ps_const_dirty_ = true;
	}

	void ffp_state::on_set_vertex_shader(IDirect3DVertexShader9* shader)
	{
		if (shader) shader->AddRef();
		if (last_vs_) last_vs_->Release();
		last_vs_ = shader;
		ffp_active_ = false;


		ShaderInfo::ShaderClasify info;
		if (shader && !shader_info.is_shader_info_cached(shader, info))
		{
			bool dump_shader = false;// shared::common::flags::has_flag("dump_shaders");;
			std::string decomp;
			uint32_t hash = shader_info.get_shader_decomp(shader, &decomp, dump_shader);

			if (decomp.contains("dcl_blendindices"))
			{
				info.type = ShaderInfo::SHADER_SKINNING;
			}
			else
			{
				info.type = ShaderInfo::SHADER_UNKNOWN;
			}

			shader_info.add_to_cache(hash, info);
		}

		vs_type = ShaderInfo::EShaderType(info.type);
	}

	bool ffp_state::on_set_pixel_shader(IDirect3DPixelShader9* shader)
	{
		if (shader) shader->AddRef();
		if (last_ps_) last_ps_->Release();
		last_ps_ = shader;


		ShaderInfo::ShaderClasify info;
		if (shader && !shader_info.is_shader_info_cached(shader, info))
		{
			bool dump_shader = false;// shared::common::flags::has_flag("dump_shaders");;
			std::string decomp;
			uint32_t hash = shader_info.get_shader_decomp(shader, &decomp, dump_shader);

			if (decomp.contains("float4 c_primary_change_color") ||
				(decomp.contains("mad r4, r2.w, c0, 1-r2.w") && decomp.contains("texld r2, t2"))
			)
			{
				info.type = ShaderInfo::SHADER_SKINNING_COLOR_CHG;
			}
			else
			{
				info.type = ShaderInfo::SHADER_UNKNOWN;
			}

			shader_info.add_to_cache(hash, info);
		}

		ps_type = ShaderInfo::EShaderType(info.type);

		// Swallow the call while in FFP mode (don't forward to real device)
		return ffp_active_;
	}

	void ffp_state::on_set_texture(UINT stage, IDirect3DBaseTexture9* texture)
	{
		if (stage < 8)
		{
			cur_texture_[stage] = texture;
			if (stage > 0 && texture != nullptr)
				textures_dirty_ = true;
		}
	}

	void ffp_state::on_set_stream_source(UINT stream, IDirect3DVertexBuffer9* vb, UINT offset, UINT stride)
	{
		if (stream < 4)
		{
			stream_vb_[stream] = vb;
			stream_offset_[stream] = offset;
			stream_stride_[stream] = stride;
		}
	}

	void ffp_state::on_set_vertex_declaration(IDirect3DVertexDeclaration9* decl)
	{
		last_decl_ = decl;
		cur_decl_is_skinned_ = false;
		cur_decl_has_texcoord_ = false;
		cur_decl_has_normal_ = false;
		cur_decl_has_color_ = false;
		cur_decl_has_pos_t_ = false;
		cur_decl_texcoord_type_ = -1;
		cur_decl_texcoord_off_ = 0;
		cur_decl_num_weights_ = 0;
		cur_decl_blend_weight_off_ = 0;
		cur_decl_blend_weight_type_ = 0;
		cur_decl_blend_indices_off_ = 0;
		cur_decl_blend_indices_type_ = 0;
		cur_decl_pos_off_ = 0;
		cur_decl_normal_off_ = 0;
		cur_decl_normal_type_ = -1;
		cur_decl_color_off_ = 0;
		cur_decl_color_stream_ = 0;
		cur_decl_color_type_ = 0;

		if (!decl) return;

		UINT num_elems = 0;
		if (FAILED(decl->GetDeclaration(nullptr, &num_elems))) return;
		if (num_elems == 0 || num_elems > 32) return;

		D3DVERTEXELEMENT9 elems[32];
		if (FAILED(decl->GetDeclaration(elems, &num_elems))) return;

		bool has_blend_weight = false;
		bool has_blend_indices = false;
		BYTE blend_weight_type = 0;

		for (UINT e = 0; e < num_elems; e++)
		{
			const auto& el = elems[e];
			if (el.Stream == 0xFF) break;

			switch (el.Usage)
			{
			case D3DDECLUSAGE_POSITIONT:
				cur_decl_has_pos_t_ = true;
				break;

			case D3DDECLUSAGE_BLENDWEIGHT:
				has_blend_weight = true;
				blend_weight_type = el.Type;
				cur_decl_blend_weight_off_ = el.Offset;
				cur_decl_blend_weight_type_ = el.Type;
				break;

			case D3DDECLUSAGE_BLENDINDICES:
				has_blend_indices = true;
				cur_decl_blend_indices_off_ = el.Offset;
				cur_decl_blend_indices_type_ = el.Type;
				break;

			case D3DDECLUSAGE_POSITION:
				if (el.Stream == 0)
					cur_decl_pos_off_ = el.Offset;
				break;

			case D3DDECLUSAGE_NORMAL:
				if (el.Stream == 0)
				{
					cur_decl_has_normal_ = true;
					cur_decl_normal_off_ = el.Offset;
					cur_decl_normal_type_ = el.Type;
				}
				break;

			case D3DDECLUSAGE_TEXCOORD:
				if (el.UsageIndex == 0 && el.Stream == 0)
				{
					cur_decl_has_texcoord_ = true;
					cur_decl_texcoord_type_ = el.Type;
					cur_decl_texcoord_off_ = el.Offset;
				}
				break;

			case D3DDECLUSAGE_COLOR:
				if (el.UsageIndex == 0)
				{
					cur_decl_has_color_    = true;
					cur_decl_color_off_    = el.Offset;
					cur_decl_color_stream_ = el.Stream;
					cur_decl_color_type_   = el.Type;
				}
				break;
			}
		}

		if (has_blend_weight && has_blend_indices)
		{
			cur_decl_is_skinned_ = true;

			switch (blend_weight_type)
			{
			case D3DDECLTYPE_FLOAT1:  cur_decl_num_weights_ = 1; break;
			case D3DDECLTYPE_FLOAT2:  cur_decl_num_weights_ = 2; break;
			case D3DDECLTYPE_FLOAT3:  cur_decl_num_weights_ = 3; break;
			case D3DDECLTYPE_FLOAT4:  cur_decl_num_weights_ = 3; break;
			case D3DDECLTYPE_UBYTE4N: cur_decl_num_weights_ = 3; break;
			default:                  cur_decl_num_weights_ = 3; break;
			}
		}
	}

	void ffp_state::on_present()
	{
		frame_count_++;
		ffp_setup_ = false;
		draw_call_count_ = 0;
		scene_count_ = 0;
		disengage(shared::globals::d3d_device);
		std::memset(vs_const_write_log_, 0, sizeof(vs_const_write_log_));
	}

	void ffp_state::on_begin_scene()
	{
		ffp_setup_ = false;
		scene_count_++;
	}

	void ffp_state::on_reset()
	{
		if (last_vs_) { last_vs_->Release(); last_vs_ = nullptr; }
		if (last_ps_) { last_ps_->Release(); last_ps_ = nullptr; }

		view_proj_valid_ = false;
		ffp_setup_ = false;
		world_dirty_ = false;
		view_proj_dirty_ = false;
		ps_const_dirty_ = false;
		skinning_setup_ = false;
		ffp_active_ = false;
		bone_start_reg_ = 0;
		num_bones_ = 0;
		textures_dirty_ = true;

		for (auto& [orig, patched] : patched_decl_cache_)
			if (patched) patched->Release();
		patched_decl_cache_.clear();

		if (uv_stream_vb_) { uv_stream_vb_->Release(); uv_stream_vb_ = nullptr; }
		uv_stream_vb_capacity_ = 0;

		log("FFP", "State reset");
	}

	// ---- State consumers ----

	void ffp_state::engage(IDirect3DDevice9* dev)
	{
		if (!enabled_ || !dev) return;

		if (!ffp_active_)
		{
			dev->SetVertexShader(nullptr);
			dev->SetPixelShader(nullptr);
			ffp_active_ = true;
		}

		apply_transforms(dev);
		setup_texture_stages(dev);

		if (!ffp_setup_)
		{
			setup_lighting(dev);
			ffp_setup_ = true;
		}
	}

	void ffp_state::disengage(IDirect3DDevice9* dev)
	{
		if (!ffp_active_ || !dev) return;

		// Always restore game textures when leaving FFP mode
		for (DWORD ts = 0; ts < 8; ts++)
			dev->SetTexture(ts, cur_texture_[ts]);
		textures_dirty_ = true;

		if (skinning_setup_)
		{
			dev->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
			dev->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
			skinning_setup_ = false;
		}

		dev->SetVertexShader(last_vs_);
		dev->SetPixelShader(last_ps_);
		ffp_active_ = false;
	}

	void ffp_state::setup_albedo_texture(IDirect3DDevice9* dev)
	{
		if (!dev) return;

		int as = cfg_->albedo_stage;
		auto* albedo = (as >= 0 && as < 8) ? cur_texture_[as] : cur_texture_[0];

		dev->SetTexture(0, albedo);

		// Only null stages 1-7 if the game has written non-null textures since last null
		if (textures_dirty_)
		{
			for (DWORD ts = 1; ts < 8; ts++)
				dev->SetTexture(ts, nullptr);
			textures_dirty_ = false;
		}
	}

	void ffp_state::restore_textures(IDirect3DDevice9* dev)
	{
		if (!dev) return;

		for (DWORD ts = 0; ts < 8; ts++)
			dev->SetTexture(ts, cur_texture_[ts]);
		textures_dirty_ = true;
	}

	bool ffp_state::try_patch_decl_for_color_uv(IDirect3DDevice9* dev)
	{
		if (!cur_decl_has_color_ || cur_decl_has_texcoord_ || !last_decl_ || !dev)
			return false;

		auto it = patched_decl_cache_.find(last_decl_);
		if (it != patched_decl_cache_.end())
		{
			dev->SetVertexDeclaration(it->second);
			return true;
		}

		UINT num_elems = 0;
		if (FAILED(last_decl_->GetDeclaration(nullptr, &num_elems))) return false;
		if (num_elems == 0 || num_elems > 32) return false;

		// 32 original elements + 1 new TEXCOORD0 + 1 D3DDECL_END
		D3DVERTEXELEMENT9 src_elems[34];
		if (FAILED(last_decl_->GetDeclaration(src_elems, &num_elems))) return false;

		// Build the patched declaration:
		//   - Copy all original elements EXCEPT COLOR0. The COLOR bytes encode UV, not actual
		//     diffuse color. Keeping the COLOR element would make FFP read those bytes as vertex
		//     diffuse, multiplying (darkening) the texture and corrupting the alpha channel.
		//     With it removed, FFP falls back to the material diffuse ({1,1,1,1} from
		//     setup_lighting), which is correct: texture color and alpha pass through unmodified.
		//   - Append a FLOAT2 TEXCOORD0 on stream 1 (filled per-draw by prepare_uv_stream with
		//     R/255 → U, G/255 → V, matching the original shader's mov oT0.xy, v4).
		D3DVERTEXELEMENT9 elems[34];
		UINT out = 0;
		for (UINT i = 0; i < num_elems - 1; ++i)  // -1 to skip D3DDECL_END at tail
		{
			if (src_elems[i].Usage == D3DDECLUSAGE_COLOR && src_elems[i].UsageIndex == 0)
				continue;
			elems[out++] = src_elems[i];
		}

		const D3DVERTEXELEMENT9 tc_elem = {
			1,                      // stream 1 — separate UV stream, see prepare_uv_stream()
			0,                      // offset 0
			D3DDECLTYPE_FLOAT2,
			D3DDECLMETHOD_DEFAULT,
			D3DDECLUSAGE_TEXCOORD,
			0
		};
		elems[out++] = tc_elem;
		elems[out]   = D3DDECL_END();

		IDirect3DVertexDeclaration9* patched = nullptr;
		if (FAILED(dev->CreateVertexDeclaration(elems, &patched))) return false;

		patched_decl_cache_[last_decl_] = patched;
		dev->SetVertexDeclaration(patched);
		return true;
	}

	void ffp_state::prepare_uv_stream(IDirect3DDevice9* dev, UINT first_vertex, UINT num_vertices)
	{
		if (!dev || num_vertices == 0 || !cur_decl_has_color_) return;

		const BYTE cs = cur_decl_color_stream_;
		if (cs >= 4) return;

		auto* src_vb    = stream_vb_[cs];
		const UINT stride = stream_stride_[cs];
		const UINT src_off = stream_offset_[cs];
		if (!src_vb || stride == 0) return;

		// Grow the UV VB if it can't hold all vertices we'll write
		const UINT needed = first_vertex + num_vertices;
		if (!uv_stream_vb_ || uv_stream_vb_capacity_ < needed) {
			if (uv_stream_vb_) { uv_stream_vb_->Release(); uv_stream_vb_ = nullptr; }
			const UINT cap = needed < 65536u ? 65536u : needed + 1024u;
			if (FAILED(dev->CreateVertexBuffer(cap * sizeof(float) * 2,
					D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
					&uv_stream_vb_, nullptr)))
				return;
			uv_stream_vb_capacity_ = cap;
		}

		// Lock source VB (full) to read D3DCOLOR bytes
		void* src_raw = nullptr;
		if (FAILED(src_vb->Lock(0, 0, &src_raw, 0))) return;

		// Lock UV VB with DISCARD — gives fresh backing memory, no GPU stall
		float* uv_raw = nullptr;
		if (FAILED(uv_stream_vb_->Lock(0, 0, reinterpret_cast<void**>(&uv_raw), D3DLOCK_DISCARD))) {
			src_vb->Unlock();
			return;
		}

		// D3DCOLOR in memory is BGRA: byte[0]=B, byte[1]=G, byte[2]=R, byte[3]=A.
		// The decal shader (mov oT0.xy, v4) reads hardware-expanded (R/255, G/255) as UV.
		const auto* base = static_cast<const BYTE*>(src_raw) + src_off + cur_decl_color_off_;
		float* uv_dst = uv_raw + first_vertex * 2;
		for (UINT i = 0; i < num_vertices; ++i) {
			const auto* c = base + (first_vertex + i) * stride;
			uv_dst[i * 2 + 0] = c[2] / 255.0f;  // R → U
			uv_dst[i * 2 + 1] = c[1] / 255.0f;  // G → V
		}

		uv_stream_vb_->Unlock();
		src_vb->Unlock();

		dev->SetStreamSource(1, uv_stream_vb_, 0, sizeof(float) * 2);
	}

	void ffp_state::restore_patched_decl(IDirect3DDevice9* dev)
	{
		if (last_decl_ && dev) {
			dev->SetVertexDeclaration(last_decl_);
			dev->SetStreamSource(1, nullptr, 0, 0);
		}
	}

	// ---- Internal helpers ----

	void ffp_state::apply_transforms(IDirect3DDevice9* dev)
	{
		const auto* camera = camera_base_ ? camera_base_ : comp::game::CameraRenderMatrices_ptr;
		if (!camera) return;
		{
			D3DXMATRIX view_matrix = {};
			view_matrix._11 = camera->viewMatrix.rotation[0];
			view_matrix._12 = camera->viewMatrix.rotation[1];
			view_matrix._13 = camera->viewMatrix.rotation[2];
			view_matrix._14 = 0.0;
			view_matrix._21 = camera->viewMatrix.rotation[3];
			view_matrix._22 = camera->viewMatrix.rotation[4];
			view_matrix._23 = camera->viewMatrix.rotation[5];
			view_matrix._24 = 0.0;
			view_matrix._31 = camera->viewMatrix.rotation[6];
			view_matrix._32 = camera->viewMatrix.rotation[7];
			view_matrix._33 = camera->viewMatrix.rotation[8];
			view_matrix._34 = 0.0;
			view_matrix._41 = camera->viewMatrix.translation.x;
			view_matrix._43 = camera->viewMatrix.translation.z;
			view_matrix._42 = camera->viewMatrix.translation.y;
			view_matrix._44 = 1.0;
			dev->SetTransform(D3DTS_VIEW, &view_matrix);

		}

		{
			D3DXMATRIX proj_matrix = camera->projectionMatrix;
			dev->SetTransform(D3DTS_PROJECTION, &proj_matrix);
		}

		{
			dev->SetTransform(D3DTS_WORLD, &shared::globals::IDENTITY);
		}

		if (world_dirty_)
		{
			const float* src = &vs_const_[kWorldRegStart * 4];
			float world_matrix[16];

			world_matrix[0]  = src[0];  world_matrix[1]  = src[4];  world_matrix[2]  = src[8];   world_matrix[3]  = 0.0f;
			world_matrix[4]  = src[1];  world_matrix[5]  = src[5];  world_matrix[6]  = src[9];   world_matrix[7]  = 0.0f;
			world_matrix[8]  = src[2];  world_matrix[9]  = src[6];  world_matrix[10] = src[10];  world_matrix[11] = 0.0f;
			world_matrix[12] = src[3];  world_matrix[13] = src[7];  world_matrix[14] = src[11];  world_matrix[15] = 1.0f;

			dev->SetTransform(D3DTS_WORLDMATRIX(0), reinterpret_cast<const D3DMATRIX*>(world_matrix));
			world_dirty_ = false;
		}

	}

	void ffp_state::setup_lighting(IDirect3DDevice9* dev)
	{
		dev->SetRenderState(D3DRS_LIGHTING, FALSE);

		D3DMATERIAL9 mat = {};
		mat.Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
		mat.Ambient = { 1.0f, 1.0f, 1.0f, 1.0f };
		dev->SetMaterial(&mat);
	}

	void ffp_state::setup_texture_stages(IDirect3DDevice9* dev)
	{
		// Stage 0: modulate texture color with vertex/material diffuse
		dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_CURRENT);
		dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
		dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

		// Disable stages 1-7: the game binds shadow maps, LUTs, normal maps etc.
		// on higher stages for its pixel shaders. In FFP mode those become active
		// and Remix may consume the wrong textures.
		for (DWORD s = 1; s <= 7; s++)
		{
			dev->SetTextureStageState(s, D3DTSS_COLOROP, D3DTOP_DISABLE);
			dev->SetTextureStageState(s, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		}
	}

	// ---- Utility ----

	void ffp_state::mat4_transpose(float* dst, const float* src)
	{
		dst[0]  = src[0];  dst[1]  = src[4];  dst[2]  = src[8];  dst[3]  = src[12];
		dst[4]  = src[1];  dst[5]  = src[5];  dst[6]  = src[9];  dst[7]  = src[13];
		dst[8]  = src[2];  dst[9]  = src[6];  dst[10] = src[10]; dst[11] = src[14];
		dst[12] = src[3];  dst[13] = src[7];  dst[14] = src[11]; dst[15] = src[15];
	}

	bool ffp_state::mat4_is_interesting(const float* m)
	{
		bool all_zero = true;
		for (int i = 0; i < 16; i++)
		{
			if (m[i] != 0.0f) { all_zero = false; break; }
		}
		if (all_zero) return false;

		// Check for identity
		if (m[0] == 1.0f && m[1] == 0.0f && m[2] == 0.0f  && m[3] == 0.0f &&
			m[4] == 0.0f && m[5] == 1.0f && m[6] == 0.0f  && m[7] == 0.0f &&
			m[8] == 0.0f && m[9] == 0.0f && m[10] == 1.0f && m[11] == 0.0f &&
			m[12] == 0.0f && m[13] == 0.0f && m[14] == 0.0f && m[15] == 1.0f)
			return false;

		return true;
	}
}
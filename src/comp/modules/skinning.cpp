// SPDX-License-Identifier: MIT
// Original author: xoxor4d (https://github.com/xoxor4d/remix-comp-base)
// Incorporated via Ekozmaster's MIT-licensed project and Kim2091's fork
// See THIRD-PARTY-NOTICES.md for the full license texts

#include "std_include.hpp"
#include "skinning.hpp"
#include "imgui.hpp"

#include "shared/common/config.hpp"
#include "shared/common/ffp_state.hpp"

namespace comp
{
	skinning::skinning()
	{
		p_this = this;
		initialized_ = true;
		shared::common::log("Skinning", "Module initialized.", shared::common::LOG_TYPE::LOG_TYPE_WARN);
	}

	skinning::~skinning()
	{
		if (streaming_vb_) { streaming_vb_->Release(); streaming_vb_ = nullptr; }
		if (skin_exp_decl_) { skin_exp_decl_->Release(); skin_exp_decl_ = nullptr; }
	}

	HRESULT skinning::draw_skinned_dip(IDirect3DDevice9* dev,
		D3DPRIMITIVETYPE pt, INT base_vtx, UINT min_vtx, UINT num_verts,
		UINT start_idx, UINT prim_count)
	{
		using shared::common::ShaderInfo;

		auto& ffp = shared::common::ffp_state::get();
		auto* im = imgui::get();
		if (im) im->m_stats._skin_dip_calls.track_single();

		if (!skin_exp_decl_)
			create_expanded_decl(dev);
		if (!streaming_vb_)
			ensure_streaming_vb(dev);

		if (!skin_exp_decl_ || !streaming_vb_)
		{
			ffp.disengage(dev);
			return dev->DrawIndexedPrimitive(pt, base_vtx, min_vtx, num_verts, start_idx, prim_count);
		}

		auto* src_vb = ffp.stream_vb(0);
		UINT stream_off = ffp.stream_offset(0);
		UINT stride = ffp.stream_stride(0);
		INT combined_base = base_vtx + static_cast<INT>(min_vtx);

		UINT vb_offset = expand_vertices(dev, src_vb, stream_off, combined_base, num_verts, stride);

		if (vb_offset == UINT_MAX)
		{
			if (im) im->m_stats._skin_expvb_fail.track_single();
			ffp.disengage(dev);
			return dev->DrawIndexedPrimitive(pt, base_vtx, min_vtx, num_verts, start_idx, prim_count);
		}

		if (im) im->m_stats._skin_expvb_success.track_single();

		ffp.engage(dev);
		ffp.setup_albedo_texture(dev);

		//ffp.get_shader_info().get_shader_decomp(ffp.last_ps(), NULL, true);
		bool is_color_change_shader = (ffp.get_ps_shader_type() == ShaderInfo::SHADER_SKINNING_COLOR_CHG);
		if (is_color_change_shader)
		{
			const float* change_color = ffp.ps_const_data(); //we care about reg0
			dev->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_COLORVALUE(change_color[0], change_color[1], change_color[2], change_color[3]));
			dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		}

		upload_bones(dev);

		IDirect3DVertexDeclaration9* orig_decl = nullptr;
		dev->GetVertexDeclaration(&orig_decl);
		dev->SetVertexDeclaration(skin_exp_decl_);
		dev->SetStreamSource(0, streaming_vb_, vb_offset * SKIN_VTX_SIZE, SKIN_VTX_SIZE);

		auto hr = dev->DrawIndexedPrimitive(pt, -(INT)min_vtx, min_vtx, num_verts, start_idx, prim_count);

		dev->SetVertexDeclaration(orig_decl);
		if (orig_decl) orig_decl->Release();
		dev->SetStreamSource(0, src_vb, ffp.stream_offset(0), stride);
		//disable gpu skinning
		dev->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
		dev->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);

		return hr;
	}

	void skinning::on_reset()
	{
		if (streaming_vb_) { streaming_vb_->Release(); streaming_vb_ = nullptr; }
		if (skin_exp_decl_) { skin_exp_decl_->Release(); skin_exp_decl_ = nullptr; }
		streaming_cursor_ = 0;
		last_discard_frame_ = UINT_MAX;
	}

	void skinning::create_expanded_decl(IDirect3DDevice9* dev)
	{
		// Pre-skinned vertex layout (32 bytes): positions/normals blended on CPU, already in world space
		//   FLOAT3 pos, FLOAT3 normal, FLOAT2 uv
		D3DVERTEXELEMENT9 elems[] = {
			{ 0,  0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
			{ 0, 12, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
			{ 0, 16, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0 },
			{ 0, 20, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
			{ 0, 32, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
			D3DDECL_END()
		};

		HRESULT hr = dev->CreateVertexDeclaration(elems, &skin_exp_decl_);
		if (FAILED(hr))
		{
			shared::common::log("Skinning", "Failed to create expanded vertex declaration!",
				shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
			skin_exp_decl_ = nullptr;
		}
	}

	void skinning::ensure_streaming_vb(IDirect3DDevice9* dev)
	{
		if (streaming_vb_) return;
		HRESULT hr = dev->CreateVertexBuffer(
			STREAMING_CAPACITY * SKIN_VTX_SIZE,
			D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
			&streaming_vb_, nullptr);
		if (FAILED(hr))
		{
			shared::common::log("Skinning", "Failed to create streaming VB!",
				shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
			streaming_vb_ = nullptr;
		}
		streaming_cursor_ = 0;
		last_discard_frame_ = UINT_MAX;
	}

	UINT skinning::expand_vertices(IDirect3DDevice9* dev,
		IDirect3DVertexBuffer9* src_vb, UINT stream_offset, INT base_vtx, UINT num_verts, UINT stride)
	{
		if (!src_vb || stride == 0 || num_verts == 0 || num_verts > STREAMING_CAPACITY)
			return UINT_MAX;

		auto& ffp = shared::common::ffp_state::get();
		auto& cfg = shared::common::config::get().ffp;

		// Determine lock mode: DISCARD on new frame or overflow, NOOVERWRITE otherwise
		UINT cur_frame = ffp.frame_count();
		bool need_discard = (cur_frame != last_discard_frame_);

		if (streaming_cursor_ + num_verts > STREAMING_CAPACITY)
			need_discard = true;

		if (need_discard)
		{
			streaming_cursor_ = 0;
			last_discard_frame_ = cur_frame;
		}

		DWORD lock_flags = need_discard ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE;
		UINT my_offset = streaming_cursor_;
		streaming_cursor_ += num_verts;

		// Lock source VB
		unsigned char* src_data = nullptr;
		UINT read_off = stream_offset + static_cast<UINT>(base_vtx) * stride;
		if (FAILED(src_vb->Lock(read_off, num_verts * stride,
			reinterpret_cast<void**>(&src_data), D3DLOCK_READONLY)))
			return UINT_MAX;

		// Lock streaming VB (DISCARD orphans old buffer; NOOVERWRITE avoids GPU stall)
		unsigned char* dst_data = nullptr;
		UINT write_off = my_offset * SKIN_VTX_SIZE;
		if (FAILED(streaming_vb_->Lock(write_off, num_verts * SKIN_VTX_SIZE,
			reinterpret_cast<void**>(&dst_data), lock_flags)))
		{
			src_vb->Unlock();
			return UINT_MAX;
		}

		// Cache all per-draw layout info outside the vertex loop
		const int pos_off   = ffp.cur_decl_pos_off();
		const int bw_type   = ffp.cur_decl_blend_weight_type();
		const int nw        = ffp.cur_decl_num_weights();
		const int bw_off    = ffp.cur_decl_blend_weight_off();
		const int bi_off    = ffp.cur_decl_blend_indices_off();
		const int bi_type   = ffp.cur_decl_blend_indices_type();
		const bool has_norm = ffp.cur_decl_has_normal();
		const int norm_off  = ffp.cur_decl_normal_off();
		const int norm_type = ffp.cur_decl_normal_type();
		const bool has_tc   = ffp.cur_decl_has_texcoord();
		const int tc_off    = ffp.cur_decl_texcoord_off();
		const int tc_type   = ffp.cur_decl_texcoord_type();

		const float* vs_const    = ffp.vs_const_data();
		const int bone_start     = ffp.bone_start_reg();
		const int rpb            = cfg.vs_regs_per_bone;
		const bool bones_valid_base = (ffp.num_bones() > 0 &&
			bone_start >= cfg.vs_reg_bone_threshold);

		const bool bw_is_float = (bw_type >= D3DDECLTYPE_FLOAT1 && bw_type <= D3DDECLTYPE_FLOAT4);
		const bool bw_is_ubyte = (bw_type == D3DDECLTYPE_UBYTE4N);
		const bool tc_is_half  = (tc_type == D3DDECLTYPE_FLOAT16_2);
		const bool tc_is_float = (tc_type >= D3DDECLTYPE_FLOAT1 && tc_type <= D3DDECLTYPE_FLOAT4);

		bool first_vtx_valid = false;

		for (UINT v = 0; v < num_verts; v++)
		{
			const unsigned char* sv = &src_data[v * stride];
			float* out = reinterpret_cast<float*>(&dst_data[v * SKIN_VTX_SIZE]);

			// Position
			auto* pos = reinterpret_cast<const float*>(&sv[pos_off]);
			const float px = pos[0], py = pos[1], pz = pos[2];

			// Blend weights
			float w0 = 1.0f, w1 = 0.0f;
			if (bw_is_float)
			{
				auto* bw = reinterpret_cast<const float*>(&sv[bw_off]);
				w0 = bw[0];
				w1 = (nw >= 2) ? bw[1] : (1.0f - w0);
			}
			else if (bw_is_ubyte)
			{
				w0 = sv[bw_off] / 255.0f;
				w1 = (nw >= 2) ? (sv[bw_off + 1] / 255.0f) : (1.0f - w0);
			}

			// Blend indices
			int idx0 = 0, idx1 = 0;
			{
				const auto* si = &sv[bi_off];
				if (bi_type == D3DDECLTYPE_SHORT2)
				{
					idx0 = static_cast<signed short>(si[0] | (static_cast<unsigned short>(si[1]) << 8));
					idx1 = static_cast<signed short>(si[2] | (static_cast<unsigned short>(si[3]) << 8));
				}
				else if (bi_type == D3DDECLTYPE_D3DCOLOR)
				{
					idx0 = si[2]; idx1 = si[1];
				}
				else
				{
					idx0 = si[0]; idx1 = si[1];
				}
			}

			// Normal
			float nx = 0.0f, ny = 0.0f, nz = 1.0f;
			if (has_norm)
			{
				float normal[3];
				decode_normal(&sv[norm_off], norm_type, normal);
				nx = normal[0]; ny = normal[1]; nz = normal[2];
			}

			// Bone blending
			const int reg0 = bone_start + idx0 * rpb;
			const int reg1 = bone_start + idx1 * rpb;
			const bool valid = bones_valid_base && reg0 + rpb <= 256 && reg1 + rpb <= 256;

			{
				out[0] = px; out[1] = py; out[2] = pz;
				BYTE* outidx = (BYTE*)&out[3];
				outidx[0] = idx0; outidx[1] = idx1; outidx[2] = outidx[3] = 0;
				out[4] = w0;
				out[5] = nx; out[6] = ny; out[7] = nz;
			}

			if (v == 0) first_vtx_valid = valid;

			// Texcoord
			if (has_tc)
			{
				if (tc_is_half)
				{
					auto* h = reinterpret_cast<const unsigned short*>(&sv[tc_off]);
					out[8] = half_to_float(h[0]);
					out[9] = half_to_float(h[1]);
				}
				else if (tc_is_float)
				{
					auto* tc = reinterpret_cast<const float*>(&sv[tc_off]);
					out[8] = tc[0];
					out[9] = (tc_type >= D3DDECLTYPE_FLOAT2) ? tc[1] : 0.0f;
				}
				else
				{
					out[8] = out[9] = 0.0f;
				}
			}
			else
			{
				out[8] = out[9] = 0.0f;
			}
		}

		streaming_vb_->Unlock();
		src_vb->Unlock();

		auto* im = imgui::get();
		if (im)
		{
			if (first_vtx_valid) im->m_stats._skin_vtx_valid_draws.track_single();
			else im->m_stats._skin_vtx_fallback_draws.track_single();
		}

		return my_offset;
	}

	void skinning::upload_bones(IDirect3DDevice9* dev)
	{
		auto& ffp = shared::common::ffp_state::get();
		auto& cfg = shared::common::config::get().ffp;

		int bone_start = ffp.bone_start_reg();
		int num_bones = ffp.num_bones();
		if (bone_start < cfg.vs_reg_bone_threshold || num_bones <= 0) return;

		const float* vs_const = ffp.vs_const_data();
		int regs_per_bone = cfg.vs_regs_per_bone;
		int max_bones = (num_bones > 48) ? 48 : num_bones;

		// Set render state BEFORE bone loop (matching proxy-minimal)
		dev->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE);
		// Halo CE gbxmodel: at most 2 nodes per vertex -> always D3DVBF_1WEIGHTS
		dev->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_1WEIGHTS);
		ffp.set_skinning_setup(true);

		for (int i = 0; i < max_bones; i++)
		{
			const float* src = &vs_const[(bone_start + i * regs_per_bone) * 4];
			float bone44[16];

			// Inline 4x3 -> 4x4 transpose (matching proxy-minimal)
			// Only reads 12 floats (3 registers) per bone
			bone44[0]  = src[0];  bone44[1]  = src[4];  bone44[2]  = src[8];   bone44[3]  = 0.0f;
			bone44[4]  = src[1];  bone44[5]  = src[5];  bone44[6]  = src[9];   bone44[7]  = 0.0f;
			bone44[8]  = src[2];  bone44[9]  = src[6];  bone44[10] = src[10];  bone44[11] = 0.0f;
			bone44[12] = src[3];  bone44[13] = src[7];  bone44[14] = src[11];  bone44[15] = 1.0f;

			dev->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_WORLDMATRIX(i)),
				reinterpret_cast<const D3DMATRIX*>(bone44));
		}

		dev->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_1WEIGHTS);
		dev->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, TRUE);
	}

	void skinning::disable_skinning(IDirect3DDevice9* dev)
	{
		auto& ffp = shared::common::ffp_state::get();
		if (!ffp.skinning_setup()) return;
		dev->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
		dev->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
		ffp.set_skinning_setup(false);
	}

	float skinning::half_to_float(unsigned short h)
	{
		unsigned int sign = (h >> 15) & 0x1;
		unsigned int exp = (h >> 10) & 0x1F;
		unsigned int mant = h & 0x3FF;

		if (exp == 0)
		{
			if (mant == 0) {
				unsigned int f = sign << 31;
				float result;
				std::memcpy(&result, &f, sizeof(float));
				return result;
			}
			// Denormalized
			while (!(mant & 0x400)) { mant <<= 1; exp--; }
			exp++; mant &= ~0x400u;
		}
		else if (exp == 31)
		{
			unsigned int f = (sign << 31) | 0x7F800000u | (mant << 13);
			float result;
			std::memcpy(&result, &f, sizeof(float));
			return result;
		}

		exp += (127 - 15);
		unsigned int f = (sign << 31) | (exp << 23) | (mant << 13);
		float result;
		std::memcpy(&result, &f, sizeof(float));
		return result;
	}

	void skinning::decode_normal(const unsigned char* src, int type, float* out)
	{
		switch (type)
		{
		case D3DDECLTYPE_FLOAT3:
		{
			auto* fp = reinterpret_cast<const float*>(src);
			out[0] = fp[0]; out[1] = fp[1]; out[2] = fp[2];
			break;
		}
		case D3DDECLTYPE_FLOAT16_2:
		{
			auto* h = reinterpret_cast<const unsigned short*>(src);
			out[0] = half_to_float(h[0]);
			out[1] = half_to_float(h[1]);
			out[2] = 0.0f;
			break;
		}
		case D3DDECLTYPE_SHORT4N: // 10 — normalized signed 16-bit (matching proxy-minimal)
		{
			auto* s = reinterpret_cast<const signed short*>(src);
			out[0] = s[0] / 32767.0f;
			out[1] = s[1] / 32767.0f;
			out[2] = s[2] / 32767.0f;
			break;
		}
		case D3DDECLTYPE_DEC3N:
		{
			unsigned int packed = *reinterpret_cast<const unsigned int*>(src);
			int x = static_cast<int>((packed >>  0) & 0x3FF); if (x & 0x200) x |= ~0x3FF;
			int y = static_cast<int>((packed >> 10) & 0x3FF); if (y & 0x200) y |= ~0x3FF;
			int z = static_cast<int>((packed >> 20) & 0x3FF); if (z & 0x200) z |= ~0x3FF;
			out[0] = x / 511.0f;
			out[1] = y / 511.0f;
			out[2] = z / 511.0f;
			break;
		}
		case D3DDECLTYPE_UBYTE4N:
		{
			out[0] = (src[0] / 127.5f) - 1.0f;
			out[1] = (src[1] / 127.5f) - 1.0f;
			out[2] = (src[2] / 127.5f) - 1.0f;
			break;
		}
		default: // FLOAT3/FLOAT4 and anything else — read raw floats
		{
			auto* fp = reinterpret_cast<const float*>(src);
			out[0] = fp[0]; out[1] = fp[1]; out[2] = fp[2];
			break;
		}
		}
	}
}

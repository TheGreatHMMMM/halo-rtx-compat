#pragma once
#include <string>
#include <vector>
#include <direct.h> //for mkdir
#include <concepts>
#include <d3d9.h>

namespace shared::common
{
	template<typename T>
	concept ShaderObject = std::is_same<T, IDirect3DVertexShader9*>::value ||
						   std::is_same<T, IDirect3DPixelShader9*>::value;

	// shader cache and whitelist manager
	class ShaderInfo
	{
	public:
		ShaderInfo() = default;


		// get or compute shader hash
		template<ShaderObject S>
		uint32_t get_shader_hash(S shader)
		{
			if (!shader) {
				return 0;
			}

			// check cache
			if (const auto it = shader_hash_cache_.find(shader); it != shader_hash_cache_.end()) {
				return it->second;
			}

			// get bytecode
			UINT bytecode_size = 0;
			shader->GetFunction(nullptr, &bytecode_size);

			std::vector<BYTE> bytecode(bytecode_size);
			shader->GetFunction(bytecode.data(), &bytecode_size);

			// compute and cache hash
			const uint32_t hash = shared::utils::data_hash32(bytecode.data(), bytecode_size);
			shader_hash_cache_[shader] = hash;

#if DEBUG && 0
			// log shaders
			static std::set<uint32_t> seen_hashes;
			if (seen_hashes.insert(hash).second) {
				shared::common::log("shader", std::format("New shader hash : {:08X}", hash), shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, true);
			}
#endif

			return hash;
		}


		template<typename S>
		void write_shader_to_file_dispatch(S _, uint32_t hash, const void* data, size_t sz)
		{
			write_shader_to_file("zz", hash, data, sz);
		}

		template<>
		void write_shader_to_file_dispatch<IDirect3DVertexShader9*>(IDirect3DVertexShader9* _, uint32_t hash, const void* data, size_t sz)
		{
			write_shader_to_file("vs", hash, data, sz);
		}
		template<>
		void write_shader_to_file_dispatch<IDirect3DPixelShader9*>(IDirect3DPixelShader9* _, uint32_t hash, const void* data, size_t sz)
		{
			write_shader_to_file("ps", hash, data, sz);
		}

		void write_shader_to_file(const char *prefix, uint32_t hash, const void *data, size_t sz)
		{
			char fname[64];
			snprintf(fname, sizeof(fname), "shaders\\%s_%x.txt", prefix, hash);
			FILE* f;
			if ((f = fopen(fname, "r"))) {
				fclose(f);
			}
			else
			{
				f = fopen(fname, "wb");
				if (!f)
				{
					_CRT_UNUSED(_mkdir("shaders"));
					f = fopen(fname, "wb");
				}

				if (f)
				{
					int retry = 3;
					while (1 != fwrite(data, sz, 1, f))
					{
						retry--;
						if (retry <= 0) break;
					}
					fclose(f);
				}
				else
					shared::common::log("shader", std::format("Cannot open file for dumps {:s}", fname), shared::common::LOG_TYPE::LOG_TYPE_WARN, true);
			}
		}

		template<ShaderObject S>
		uint32_t get_shader_decomp(S shader, std::string* decomp, bool data_dump)
		{
			if (!shader) {
				return 0;
			}

			uint32_t hash = 0;

			// check cache
			if (const auto it = shader_hash_cache_.find(shader); it != shader_hash_cache_.end()) {
				hash = it->second;
			}
			
			// get bytecode
			UINT bytecode_size = 0;
			shader->GetFunction(nullptr, &bytecode_size);

			std::vector<BYTE> bytecode(bytecode_size);
			shader->GetFunction(bytecode.data(), &bytecode_size);

			if(hash == 0)
			{
				// compute and cache hash
				hash = shared::utils::data_hash32(bytecode.data(), bytecode_size);
				shader_hash_cache_[shader] = hash;
			}

			if (decomp || data_dump)
			{
				HRESULT res;
				LPD3DXBUFFER decompbuf;

				if (SUCCEEDED((res = D3DXDisassembleShader((DWORD*)bytecode.data(), FALSE, NULL, &decompbuf))))
				{
					if (data_dump)
					{
						write_shader_to_file_dispatch(shader, hash, decompbuf->GetBufferPointer(), decompbuf->GetBufferSize());
					}
					if (decomp)
					{
						decomp->assign((const char*)decompbuf->GetBufferPointer(), decompbuf->GetBufferSize());
					}
					decompbuf->Release();
				}
				else
				{
					shared::common::log("shader", std::format("Shader {:08X} failed to decompile\n", hash), shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
				}
			}

			return hash;
		}

		enum EShaderType
		{
			SHADER_NEW,
			SHADER_UNKNOWN,
			SHADER_GEO,
			SHADER_MODEL,
			SHADER_SKINNING,
			SHADER_SKINNING_COLOR_CHG,
			SHADER_IGNORE,
			SHADER_LIGHT,
			SHADER_UI,
			SHADER_UI_FF,
			SHADER_SKY
		};

		enum EShaderCategory
		{
			SHCAT_NEW,
			SHCAT_GEOMETRY,
			SHCAT_DEFERRED,
			SHCAT_LIGHT,
			SHCAT_UI
		};

		struct ShaderClasify
		{
			uint8_t type;
			uint8_t category;
			uint8_t albedoStage;
			ShaderClasify() : type(SHADER_NEW), category(SHCAT_NEW), albedoStage(0) {}
		};

		const char* get_shader_type_str(EShaderType type)
		{
			switch (type)
			{
			case SHADER_NEW: return "NEW";
			case SHADER_UNKNOWN: return "UNKNOWN";
			case SHADER_GEO: return "GEO";
			case SHADER_MODEL: return "MODEL";
			case SHADER_SKINNING: return "SKINNING";
			case SHADER_SKINNING_COLOR_CHG: return "SKIN_CLRCHG";
			case SHADER_IGNORE: return "IGNORE";
			case SHADER_LIGHT: return "LIGHT";
			case SHADER_UI: return "UI";
			case SHADER_UI_FF: return "UI_FF";
			case SHADER_SKY: return "SKY";
			}
			return "__FAIL__";
		}

		const char* get_shader_category_str(EShaderCategory cat)
		{
			switch (cat)
			{
			case SHCAT_NEW: return "NEW";
			case SHCAT_GEOMETRY: return "GEO";
			case SHCAT_DEFERRED: return "DEFERRED";
			case SHCAT_LIGHT: return "LIGHT";
			case SHCAT_UI: return "UI";
			}
			return "__FAIL__";
		}

		// check if shader is cached
		template<ShaderObject S>
		bool is_shader_info_cached(S shader, ShaderClasify& info)
		{
			const uint32_t hash = get_shader_hash(shader);
			if (hash)
			{
				if (const auto it = shader_storage_.find(hash); it != shader_storage_.end())
				{
					info = it->second;
					return true;
				}

			}
			return false;
		}

		// clear cache
		void clear_cache() {
			shader_hash_cache_.clear();
			shader_storage_.clear();
		}

		// add hash to whitelist
		void add_to_cache(uint32_t hash, ShaderClasify& info) {
			shader_storage_[hash] = info;
			shared::common::log("shader", std::format("Shader {:08X} marked as [type:{:12s}]", hash, get_shader_type_str(EShaderType(info.type))),
				shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, true);
		}

	private:
		std::unordered_map<void*, uint32_t> shader_hash_cache_;
		std::unordered_map<uint32_t, ShaderClasify> shader_storage_;
	};

	// global instance
	inline ShaderInfo g_shader_cache;
}

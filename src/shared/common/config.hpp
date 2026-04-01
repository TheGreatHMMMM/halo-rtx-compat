// SPDX-License-Identifier: MIT
// Copyright (c) 2026 github.com/xoxor4d
// Copyright (c) 2026 Kim2091
// Original author: xoxor4d (https://github.com/xoxor4d/remix-comp-base)
// Incorporated via Ekozmaster's MIT-licensed project and Kim2091's fork
// See THIRD-PARTY-NOTICES.md for the full license texts

#pragma once

namespace shared::common
{
	class config
	{
	public:
		static config& get();

		void load(const std::string& ini_path);
		bool is_loaded() const { return loaded_; }

		int get_int(const char* section, const char* key, int default_val) const;

		struct ffp_settings
		{
			bool enabled = true;
			int albedo_stage = 0;

			int vs_reg_view_start = 0;
			int vs_reg_view_end = 4;
			int vs_reg_proj_start = 4;
			int vs_reg_proj_end = 8;
			int vs_reg_world_start = 29;
			int vs_reg_world_end = 32;

			int vs_reg_bone_threshold = 29;
			int vs_regs_per_bone = 3;
			int vs_bone_min_regs = 3;
		} ffp;

	private:
		std::string ini_path_;
		bool loaded_ = false;

		void parse_all();
	};
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 github.com/xoxor4d
// Copyright (c) 2026 Kim2091
// Original author: xoxor4d (https://github.com/xoxor4d/remix-comp-base)
// Incorporated via Ekozmaster's MIT-licensed project and Kim2091's fork
// See THIRD-PARTY-NOTICES.md for the full license texts

#include "std_include.hpp"
#include "config.hpp"

namespace shared::common
{
	config& config::get()
	{
		static config instance;
		return instance;
	}

	void config::load(const std::string& path)
	{
		ini_path_ = path;
		loaded_ = true;
		parse_all();
	}

	int config::get_int(const char* section, const char* key, int default_val) const
	{
		if (!loaded_) return default_val;
		return GetPrivateProfileIntA(section, key, default_val, ini_path_.c_str());
	}

	void config::parse_all()
	{
		// [FFP]
		ffp.enabled = get_int("FFP", "Enabled", 1) != 0;
		ffp.albedo_stage = get_int("FFP", "AlbedoStage", 0);
		if (ffp.albedo_stage < 0 || ffp.albedo_stage > 7)
			ffp.albedo_stage = 0;

		// [FFP.Registers]
		ffp.vs_reg_view_start = get_int("FFP.Registers", "ViewStart", 0);
		ffp.vs_reg_view_end = get_int("FFP.Registers", "ViewEnd", 4);
		ffp.vs_reg_proj_start = get_int("FFP.Registers", "ProjStart", 4);
		ffp.vs_reg_proj_end = get_int("FFP.Registers", "ProjEnd", 8);
		ffp.vs_reg_world_start = get_int("FFP.Registers", "WorldStart", 29);
		ffp.vs_reg_world_end = get_int("FFP.Registers", "WorldEnd", 32);
		ffp.vs_reg_bone_threshold = get_int("FFP.Registers", "BoneThreshold", 29);
		ffp.vs_regs_per_bone = get_int("FFP.Registers", "RegsPerBone", 3);
		ffp.vs_bone_min_regs = get_int("FFP.Registers", "BoneMinRegs", 3);

		log("Config", std::string("Loaded from: ") + ini_path_);
	}
}

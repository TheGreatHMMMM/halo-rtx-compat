#include "std_include.hpp"

#include "modules/imgui.hpp"
#include "modules/renderer.hpp"
#include "modules/skinning.hpp"
#include "modules/borderless.hpp"
#include "shared/common/remix_api.hpp"
#include "game/game.hpp"
#include "chimera/interpolate.hpp"

// see comment in main()
#include "shared/common/dinput_hook_v1.hpp"
//#include "shared/common/dinput_hook_v2.hpp"

namespace comp
{
	void on_begin_scene_cb()
	{
		if (!tex_addons::initialized) {
			tex_addons::init_texture_addons();
		}
                game::update_lights(shared::globals::d3d_device);
        }


        void main()
        {
		// Apply borderless fullscreen now that shared::globals::main_window is valid.
		borderless::apply_to_window(shared::globals::main_window);

		// #Step 2: init remix api if you want to use it or comment it otherwise
		// Requires "exposeRemixApi = True" in the "bridge.conf" that is located in the .trex folder
		//shared::common::remix_api::initialize(nullptr, nullptr, nullptr, false);

		// init modules which do not need to be initialized, before the game inits, here
		shared::common::loader::module_loader::register_module(std::make_unique<imgui>());
		shared::common::loader::module_loader::register_module(std::make_unique<renderer>());
		shared::common::loader::module_loader::register_module(std::make_unique<skinning>());

		// #Step 3: hook dinput if your game uses direct input (for ImGui) - ONLY USE ONE
		shared::common::loader::module_loader::register_module(std::make_unique<shared::common::dinput_v1>()); // v1: might cause issues with the Alt+X menu
		//shared::common::loader::module_loader::register_module(std::make_unique<shared::common::dinput_v2>()); // v2: better but might need further tweaks

		chimera::interpolate::set_up_interpolation();

		MH_EnableHook(MH_ALL_HOOKS);
	}
}

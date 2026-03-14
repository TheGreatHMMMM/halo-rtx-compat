#include "std_include.hpp"

#include "modules/imgui.hpp"
#include "modules/renderer.hpp"
#include "shared/common/remix_api.hpp"
#include "game/game.hpp"

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

		if (!comp::game::CameraRenderMatrices_ptr) return;

			D3DXMATRIX view_matrix; // = comp::game::CameraRenderMatrices_ptr->viewMatrix;
			view_matrix._11 = comp::game::CameraRenderMatrices_ptr->viewMatrix.rotation[0];
			view_matrix._12 = comp::game::CameraRenderMatrices_ptr->viewMatrix.rotation[1];
			view_matrix._13 = comp::game::CameraRenderMatrices_ptr->viewMatrix.rotation[2];
			view_matrix._14 = 0.0;
			view_matrix._21 = comp::game::CameraRenderMatrices_ptr->viewMatrix.rotation[3];
			view_matrix._22 = comp::game::CameraRenderMatrices_ptr->viewMatrix.rotation[4];
			view_matrix._23 = comp::game::CameraRenderMatrices_ptr->viewMatrix.rotation[5];
			view_matrix._24 = 0.0;
			view_matrix._31 = comp::game::CameraRenderMatrices_ptr->viewMatrix.rotation[6];
			view_matrix._32 = comp::game::CameraRenderMatrices_ptr->viewMatrix.rotation[7];
			view_matrix._33 = comp::game::CameraRenderMatrices_ptr->viewMatrix.rotation[8];
			view_matrix._34 = 0.0;
			view_matrix._41 = comp::game::CameraRenderMatrices_ptr->viewMatrix.translation.x;
			view_matrix._43 = comp::game::CameraRenderMatrices_ptr->viewMatrix.translation.z;
			view_matrix._42 = comp::game::CameraRenderMatrices_ptr->viewMatrix.translation.y;
			view_matrix._44 = 1.0;

			D3DXMATRIX proj_matrix  = comp::game::CameraRenderMatrices_ptr->projectionMatrix;

			shared::globals::d3d_device->SetTransform(D3DTS_WORLD, &shared::globals::IDENTITY);
			shared::globals::d3d_device->SetTransform(D3DTS_VIEW, &view_matrix);
			shared::globals::d3d_device->SetTransform(D3DTS_PROJECTION, &proj_matrix);
	}


	void main()
	{
		// #Step 2: init remix api if you want to use it or comment it otherwise
		// Requires "exposeRemixApi = True" in the "bridge.conf" that is located in the .trex folder
		//shared::common::remix_api::initialize(nullptr, nullptr, nullptr, false);

		// init modules which do not need to be initialized, before the game inits, here
		shared::common::loader::module_loader::register_module(std::make_unique<imgui>());
		shared::common::loader::module_loader::register_module(std::make_unique<renderer>());

		// #Step 3: hook dinput if your game uses direct input (for ImGui) - ONLY USE ONE
		shared::common::loader::module_loader::register_module(std::make_unique<shared::common::dinput_v1>()); // v1: might cause issues with the Alt+X menu
		//shared::common::loader::module_loader::register_module(std::make_unique<shared::common::dinput_v2>()); // v2: better but might need further tweaks

		MH_EnableHook(MH_ALL_HOOKS);
	}
}

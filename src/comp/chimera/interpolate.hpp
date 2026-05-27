// SPDX-License-Identifier: GPL-3.0-or-later
// Animation interpolation system ported from Chimera (https://github.com/SnowyMouse/chimera).
// Original code copyright Snowy Mouse / Kavawuvi et al., licensed under GPLv3.
// This port adapts the system to the halo-rtx-compat mod's hook/pattern infrastructure.

#pragma once

namespace comp::chimera::interpolate
{
	/**
	 * Install the animation interpolation system. Hooks ticks, frames, the
	 * camera and the first person renderer so that animated state is
	 * smoothed between Halo's fixed 30Hz ticks.
	 *
	 * Must be called after the EXE is fully mapped (the game window exists).
	 */
	void set_up_interpolation() noexcept;

	/**
	 * Reverse all hooks installed by set_up_interpolation(). Provided for
	 * symmetry with Chimera; not currently used by the mod.
	 */
	void disable_interpolation() noexcept;

	/**
	 * Returns true once set_up_interpolation() has succeeded.
	 */
	bool is_enabled() noexcept;
}

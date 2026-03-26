#pragma once

namespace comp::chimera
{
	// Extended visible-cluster buffer (512 entries × 0x1A0 bytes).
	// Exposed so that sort hooks can reference the new buffer base.
	extern void* g_vis_cluster_buf;

	void extend_limits();

} // namespace comp::chimera

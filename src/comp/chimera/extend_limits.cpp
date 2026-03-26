/*
 * chimera/extend_limits.cpp
 *
 * Halo CE engine limit extensions, ported from chimera-style patches.
 *
 * Raises the following engine limits so that RTX Remix has access to the
 * full scene during ray tracing:
 *
 *   BSP polygon (surface) limit : 16 384  ->  32 766
 *   Visible cluster limit       :    128  ->     512   (fixes portal-vis crash on large levels)
 *   Visible object limit        :    256  ->   1 024
 *   Draw distance               :  1 024  ->   2 250   world units
 *
 * Root crash cause: the visible-cluster array at 0x7C3390 is statically
 * sized for exactly 128 entries (128 * 0x1A0 = 0xD000 bytes).  Entry[128]
 * lands at 0x7D0390, which is the vis_count word itself.  On large open
 * levels the camera-cluster PVS covers > 128 clusters, so without the
 * 128-entry cap the 129th write corrupts vis_count and crashes.
 * Fix: VirtualAlloc a 512-entry buffer and redirect all 13 code refs to it.
 *
 * Address map:
 *   BSP poly array (base 0x850398)      - 13 instruction-embedded pointer refs
 *   BSP surface budget (0x4000)         -  4 immediate word comparisons
 *   Vis-cluster array (base 0x7C3390)   - 13 instruction-embedded pointer refs
 *   Vis-object array (base 0x6B8DC4)    -  4 instruction-embedded pointer refs
 *   Vis-object count (0x100)            -  3 immediate literals
 *   Draw distance pointer (0x004C91E5)  -  1 instruction-embedded pointer ref
 */

#include "std_include.hpp"

namespace comp::chimera
{

// Exposed so sort hooks can reference the extended buffer when sorting
// visible clusters by BFS hop distance.
void* g_vis_cluster_buf = nullptr;

void extend_limits()
{
	// ------------------------------------------------------------------
	// 13 instruction-embedded addresses referencing the BSP poly array
	// (static array base 0x850398, 16 384 entries x 4 bytes)
	// ------------------------------------------------------------------
	static constexpr uintptr_t bsp_poly_refs[] = {
		0x0050C19E, 0x0050C2FC, 0x0050C33E, 0x0050C377, 0x0050C3A9,
		0x0050C3D3, 0x0050C40E, 0x0050C51F, 0x0050C540,
		0x00552800, 0x0055293C, 0x00552A26, 0x00552B06
	};

	// 4 "cmp word ptr [...], 0x4000" BSP-surface-budget checks
	static constexpr uintptr_t bsp_limit_sites[] = {
		0x00553947, 0x005539B7, 0x00553A1A, 0x00553B37
	};

	// ------------------------------------------------------------------
	// 13 instruction-embedded addresses referencing the visible-cluster
	// array (static base 0x7C3390, 128 entries x 0x1A0 bytes)
	// ------------------------------------------------------------------
	static constexpr uintptr_t vis_clus_refs[] = {
		0x004F0EAC, 0x0050C121, 0x0050C160, 0x0050C1EE, 0x0050C22E,
		0x0050C472, 0x005538BA, 0x00553969, 0x00553AAA,
		0x0055444D, 0x0055458B, 0x00554677, 0x005546A8
	};

	// ------------------------------------------------------------------
	// 4 instruction-embedded addresses referencing the visible-object
	// array (static base 0x6B8DC4, 256 entries x 4 bytes)
	// ------------------------------------------------------------------
	static constexpr uintptr_t vis_obj_refs[] = {
		0x0050E9A6, 0x0050EAEB, 0x0050EB31, 0x0050EB86
	};

	// ---- BSP polygon buffer (32 766 entries x 4 bytes) ---------------
	void* bsp_buf = VirtualAlloc(nullptr, 0x7FFE * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!bsp_buf)
	{
		shared::common::log("Chimera", "extend_limits: VirtualAlloc BSP poly buffer FAILED",
			shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
		return;
	}
	const auto bsp_ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bsp_buf));
	for (auto ref : bsp_poly_refs)
		shared::utils::hook::set<uint32_t>(reinterpret_cast<void*>(ref), bsp_ptr);

	// Raise surface budget: 0x4000 (16 384) -> 0x7FFE (32 766)
	for (auto ref : bsp_limit_sites)
		shared::utils::hook::set<uint16_t>(reinterpret_cast<void*>(ref), uint16_t(0x7FFE));

	shared::common::log("Chimera", "extend_limits: BSP polygon buffer extended to 32766 entries",
		shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

	// ---- Visible cluster buffer (512 entries x 0x1A0 bytes) ----------
	// The 128-entry static array at 0x7C3390 collides with vis_count at
	// 0x7D0390 on large levels -- the 129th cluster write corrupts the
	// count and crashes the renderer.  Replace all refs with a 512-entry
	// VirtualAlloc'd buffer well clear of any engine data.
	void* vis_clus_buf = VirtualAlloc(nullptr, 512 * 0x1A0, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!vis_clus_buf)
	{
		shared::common::log("Chimera", "extend_limits: VirtualAlloc vis-cluster buffer FAILED",
			shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
		return;
	}
	g_vis_cluster_buf = vis_clus_buf;
	const auto vis_clus_ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vis_clus_buf));
	for (auto ref : vis_clus_refs)
		shared::utils::hook::set<uint32_t>(reinterpret_cast<void*>(ref), vis_clus_ptr);

	shared::common::log("Chimera", "extend_limits: visible cluster buffer extended to 512 entries",
		shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

	// ---- Visible object buffer (1 024 entries x 4 bytes) -------------
	void* vis_buf = VirtualAlloc(nullptr, 1024 * 4, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!vis_buf)
	{
		shared::common::log("Chimera", "extend_limits: VirtualAlloc vis-object buffer FAILED",
			shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
		return;
	}
	const auto vis_ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vis_buf));
	for (auto ref : vis_obj_refs)
		shared::utils::hook::set<uint32_t>(reinterpret_cast<void*>(ref), vis_ptr);

	// Raise visible object count: 256 (0x100) -> 1 024 (0x400)
	shared::utils::hook::set<uint32_t>(reinterpret_cast<void*>(0x0050EAE6u), uint32_t(0x400)); // push 0x100    -> push 0x400
	shared::utils::hook::set<uint32_t>(reinterpret_cast<void*>(0x0050EB1Au), uint32_t(0x400)); // mov ecx,0x100 -> mov ecx,0x400
	shared::utils::hook::set<uint16_t>(reinterpret_cast<void*>(0x0050EB56u), uint16_t(0x400)); // cmp word,0x100 -> cmp word,0x400

	shared::common::log("Chimera", "extend_limits: visible object buffer extended to 1024 entries",
		shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

	// ---- Draw distance 1 024 -> 2 250 world units --------------------
	// Replace the instruction-embedded pointer at 0x004C91E5 with the
	// address of our own float so the engine reads our value each frame.
	static float draw_distance = 2250.0f;
	const auto draw_ptr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&draw_distance));
	shared::utils::hook::set<uint32_t>(reinterpret_cast<void*>(0x004C91E5u), draw_ptr);

	shared::common::log("Chimera", "extend_limits: draw distance extended to 2250 world units",
		shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);

	shared::common::log("Chimera",
		"extend_limits: all limits raised -- BSP polys 32766, vis clusters 512, vis objects 1024, draw dist 2250",
		shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);
}

} // namespace comp::chimera

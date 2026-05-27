#pragma once
#include <cstdint>
#include <d3d9.h>
#include "../../../deps/halocevr/maths/Vectors.h"
#include "../../../deps/halocevr/maths/Maths.h"

namespace comp::game
{
	// place any game structures here

	struct Viewport
	{
		float left;
		float right;
		float top;
		float bottom;
	};
	static_assert(sizeof(Viewport) == 0x10);

	struct sRect
	{
		short top;
		short left;
		short bottom;
		short right;
	};
	static_assert(sizeof(sRect) == 0x8);

	struct CameraFrustum
	{
		Vector3 position;
		Vector3 facingDirection;
		Vector3 upDirection;
		// Maybe meant to control first vs third person?
		bool drawPlayer;
		std::uint8_t unk0[3];
		float fov;
		sRect WindowViewport;
		sRect InnerViewport;
		// Not sure if these are the znear/far values, but they have values in the correct range
		float zNear;
		float zFar;
		Vector4 unk1;
	};
	static_assert(sizeof(CameraFrustum) == 0x54);

	struct Renderer
	{
		short playerId;
		bool unk1;
		// Possibly just padding
		std::uint8_t unk2;
		// For reasons I've yet to decipher there are 2 nearly identical structs that both need setting
		// Maybe one does culling and one does rendering? Setting one of them doesn't seem to work
		CameraFrustum frustum;
		CameraFrustum frustum2;
	};
	static_assert(sizeof(Renderer) == 0xac);

	struct CameraRenderMatrices
	{
		Viewport viewport;
		Transform viewMatrix;
		Transform matrix;
		Vector4 quaternions[6];
		float zNear;
		float zFar;
		Vector3 frustumCorners[4];
		Vector3 cameraPosition;
		Vector3 frustumCentre;
		float floats[6];
		uint32_t unk0;
		D3DMATRIX projectionMatrix;
		float unk1;
		float unk2;
	};

	namespace Helpers
	{
		sRect* GetWindowRect();
		sRect* GetCurrentRect();
		CameraRenderMatrices* GetActiveCameraMatrices();
	}

	// ================================================================
	// Dynamic point light table (object_lights.c in the Xbox decomp)
	// ================================================================

	// Runtime point-light datum entry.  Layout verified against:
	//   - Chimera halo_data/light.hpp (0x7C-byte Light struct)
	//   - Xbox decomp object_lights.c (field offsets confirmed via pattern)
	struct PointLightEntry {
		uint16_t id;              // +0x00  generation counter (0 = dead/free slot)
		uint16_t flags;           // +0x02  bit 0x2 = active; bit 0x4 = connected_to_map
		uint32_t tag_datum;       // +0x04  'ligh' tag handle
		uint32_t unknown1;        // +0x08
		uint32_t some_counter;    // +0x0C  increments each game tick while light is alive
		uint32_t unknown2;        // +0x10  cluster partition node anchor
		float    red;             // +0x14  computed diffuse R (after tag color × modifiers)
		float    green;           // +0x18  computed diffuse G
		float    blue;            // +0x1C  computed diffuse B
		uint32_t unknown3;        // +0x20
		uint32_t unknown4;        // +0x24
		uint32_t unknown5;        // +0x28
		uint32_t parent_obj_id;   // +0x2C  datum handle of owning object
		float    x;               // +0x30  world-space position X
		float    y;               // +0x34  world-space position Y
		float    z;               // +0x38  world-space position Z
		float    forward[3];      // +0x3C  unit forward direction
		float    up[3];           // +0x48  unit up direction
		uint8_t  _pad[0x28];      // +0x54  remainder to reach 0x7C
	};
	static_assert(sizeof(PointLightEntry) == 0x7C);

	// Halo GenericTable<PointLightEntry> header (Chimera table.hpp, 0x38 bytes).
	struct PointLightTable {
		char             name[0x20];        // +0x00
		uint16_t         max_elements;      // +0x20
		uint16_t         element_size;      // +0x22
		uint8_t          _pad[0x0A];        // +0x24  (PAD(8) + PAD(2) from Chimera)
		uint16_t         current_size;      // +0x2E
		uint16_t         count;             // +0x30
		uint16_t         next_id;           // +0x32
		PointLightEntry* first_element;     // +0x34
	};
	static_assert(sizeof(PointLightTable) == 0x38);

	constexpr int MAX_EXTRACTED_LIGHTS = 128;
}

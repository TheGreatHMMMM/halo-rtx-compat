// SPDX-License-Identifier: GPL-3.0-or-later
// Animation interpolation system ported from Chimera (https://github.com/SnowyMouse/chimera).
// Original code copyright Snowy Mouse / Kavawuvi et al., licensed under GPLv3.
//
// This file is a faithful port of the following Chimera source files, consolidated
// into a single translation unit and adapted to use this mod's MinHook + pattern
// scanner infrastructure rather than Chimera's signature/event subsystems:
//
//   src/chimera/math_trig/math_trig.{hpp,cpp}
//   src/chimera/halo_data/{type,object,antenna,flag,light,particle,camera,pause,player}.{hpp,cpp}
//   src/chimera/signature/hook.{hpp,cpp}
//   src/chimera/event/{event,tick,frame,camera,revert}.{hpp,cpp}
//   src/chimera/fix/interpolate/{interpolate,antenna,camera,flag,fp,light,object,particle}.{hpp,cpp}
//
// Only the retail Halo CE 1.10 PC engine is supported.

#include "std_include.hpp"
#include "interpolate.hpp"

#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <optional>
#include <algorithm>
#include <unordered_map>

namespace comp::chimera::interpolate
{
	// ====================================================================
	// Internal Chimera-style infrastructure (anonymous namespace).
	// ====================================================================
	namespace {

	//--------------------------------------------------------------------
	// Math types & helpers (from math_trig.hpp / math_trig.cpp).
	// POD-only so they line up bit-for-bit with Halo's in-memory layout.
	//--------------------------------------------------------------------

	struct Point3D { float x, y, z; };
	struct Point2D { float x, y; };

	struct RotationMatrix;
	struct Quaternion {
		float x = 0.0F;
		float y = 0.0F;
		float z = 0.0F;
		float w = 1.0F;
	};
	struct RotationMatrix {
		Point3D v[3];
	};

	// Convert a Halo 3x3 RotationMatrix to a Quaternion (direct port of
	// Chimera's Quaternion(const RotationMatrix&) constructor).
	static Quaternion quat_from_matrix(const RotationMatrix& m) noexcept
	{
		Quaternion q;
		float tr = m.v[0].x + m.v[1].y + m.v[2].z;
		if (tr > 0) {
			float S = std::sqrt(tr + 1.0f) * 2.0f;
			q.w = 0.25f * S;
			q.x = (m.v[2].y - m.v[1].z) / S;
			q.y = (m.v[0].z - m.v[2].x) / S;
			q.z = (m.v[1].x - m.v[0].y) / S;
		} else if ((m.v[0].x > m.v[1].y) && (m.v[0].x > m.v[2].z)) {
			float S = std::sqrt(1.0f + m.v[0].x - m.v[1].y - m.v[2].z) * 2.0f;
			q.w = (m.v[2].y - m.v[1].z) / S;
			q.x = 0.25f * S;
			q.y = (m.v[0].y + m.v[1].x) / S;
			q.z = (m.v[0].z + m.v[2].x) / S;
		} else if (m.v[1].y > m.v[2].z) {
			float S = std::sqrt(1.0f + m.v[1].y - m.v[0].x - m.v[2].z) * 2.0f;
			q.w = (m.v[0].z - m.v[2].x) / S;
			q.x = (m.v[0].y + m.v[1].x) / S;
			q.y = 0.25f * S;
			q.z = (m.v[1].z + m.v[2].y) / S;
		} else {
			float S = std::sqrt(1.0f + m.v[2].z - m.v[0].x - m.v[1].y) * 2.0f;
			q.w = (m.v[1].x - m.v[0].y) / S;
			q.x = (m.v[0].z + m.v[2].x) / S;
			q.y = (m.v[1].z + m.v[2].y) / S;
			q.z = 0.25f * S;
		}
		return q;
	}

	// Convert a Quaternion back to a 3x3 RotationMatrix (direct port of
	// Chimera's RotationMatrix(const Quaternion&) constructor).
	static RotationMatrix matrix_from_quat(const Quaternion& q) noexcept
	{
		RotationMatrix m{};
		float sqw = q.w * q.w;
		float sqx = q.x * q.x;
		float sqy = q.y * q.y;
		float sqz = q.z * q.z;

		float invs = 1.0f / (sqx + sqy + sqz + sqw);
		m.v[0].x = ( sqx - sqy - sqz + sqw) * invs;
		m.v[1].y = (-sqx + sqy - sqz + sqw) * invs;
		m.v[2].z = (-sqx - sqy + sqz + sqw) * invs;

		float tmp1 = q.x * q.y;
		float tmp2 = q.z * q.w;
		m.v[1].x = 2.0f * (tmp1 + tmp2) * invs;
		m.v[0].y = 2.0f * (tmp1 - tmp2) * invs;

		tmp1 = q.x * q.z;
		tmp2 = q.y * q.w;
		m.v[2].x = 2.0f * (tmp1 - tmp2) * invs;
		m.v[0].z = 2.0f * (tmp1 + tmp2) * invs;

		tmp1 = q.y * q.z;
		tmp2 = q.x * q.w;
		m.v[2].y = 2.0f * (tmp1 + tmp2) * invs;
		m.v[1].z = 2.0f * (tmp1 - tmp2) * invs;
		return m;
	}

	static inline void interpolate_point(const Point3D& before, const Point3D& after, Point3D& output, float scale) noexcept
	{
		output.x = before.x + (after.x - before.x) * scale;
		output.y = before.y + (after.y - before.y) * scale;
		output.z = before.z + (after.z - before.z) * scale;
	}

	// SLERP. Special thanks to MosesOfEgypt (per Chimera source).
	static void interpolate_quat(const Quaternion& in_before, const Quaternion& in_after, Quaternion& out, float scale) noexcept
	{
		auto& w1 = in_before.w; auto& x1 = in_before.x; auto& y1 = in_before.y; auto& z1 = in_before.z;
		auto w0 = in_after.w; auto x0 = in_after.x; auto y0 = in_after.y; auto z0 = in_after.z;

		float cos_half_theta = w0 * w1 + x0 * x1 + y0 * y1 + z0 * z1;
		if (cos_half_theta < 0) {
			w0 *= -1; x0 *= -1; y0 *= -1; z0 *= -1;
			cos_half_theta *= -1;
		}
		if (cos_half_theta < 0.01f) return;

		float half_theta = (std::fabs(cos_half_theta) >= 1.0f) ? 0.0f : std::acos(cos_half_theta);

		float sin_half_theta = 0;
		float m = (1 - cos_half_theta * cos_half_theta);
		if (m > 0) sin_half_theta = m;

		float r0 = 1 - scale;
		float r1 = scale;
		if (sin_half_theta > 0.00001f) {
			r0 = std::sin((1 - scale) * half_theta) / sin_half_theta;
			r1 = std::sin(scale * half_theta) / sin_half_theta;
		}

		out.w = w0 * r1 + w1 * r0;
		out.x = x0 * r1 + x1 * r0;
		out.y = y0 * r1 + y1 * r0;
		out.z = z0 * r1 + z1 * r0;
	}

	static inline float distance_squared(float x1, float y1, float z1, float x2, float y2, float z2) noexcept
	{
		float x = x1 - x2, y = y1 - y2, z = z1 - z2;
		return x * x + y * y + z * z;
	}
	static inline float distance_squared(const Point3D& a, const Point3D& b) noexcept
	{
		return distance_squared(a.x, a.y, a.z, b.x, b.y, b.z);
	}
	static inline float magnitude_squared(const Point3D& a) noexcept
	{
		return a.x * a.x + a.y * a.y + a.z * a.z;
	}

	static double counter_time_elapsed(const LARGE_INTEGER& before, const LARGE_INTEGER& after) noexcept
	{
		static LARGE_INTEGER performance_frequency = {};
		if (performance_frequency.QuadPart == 0) QueryPerformanceFrequency(&performance_frequency);
		return static_cast<double>(after.QuadPart - before.QuadPart) / performance_frequency.QuadPart;
	}

	//--------------------------------------------------------------------
	// HaloID / ObjectID / TagID (from halo_data/type.hpp).
	//--------------------------------------------------------------------

	using TickCount = std::uint32_t;
	union HaloID {
		std::uint32_t whole_id;
		struct {
			std::uint16_t index;
			std::uint16_t id;
		} index;

		static HaloID null_id() noexcept { return { 0xFFFFFFFFu }; }
		bool is_null() const noexcept { return whole_id == 0xFFFFFFFFu; }
		bool operator==(const HaloID& other) const noexcept { return whole_id == other.whole_id; }
		bool operator!=(const HaloID& other) const noexcept { return whole_id != other.whole_id; }
	};
	static_assert(sizeof(HaloID) == 4);
	using TagID = HaloID;
	using ObjectID = HaloID;
	using PlayerID = HaloID;

	//--------------------------------------------------------------------
	// Hook class + write_jmp_call + overwrite (from signature/hook.{hpp,cpp}).
	// This is a direct port -- it implements x86 instruction decoding for
	// the limited subset of opcodes Chimera uses, so we can splice 5-byte
	// E9 JMPs into the middle of arbitrary Halo functions.
	//--------------------------------------------------------------------

	template<typename T> inline void overwrite(void* pointer, const T* data, std::size_t length) noexcept
	{
		DWORD new_protection = PAGE_EXECUTE_READWRITE, old_protection;
		VirtualProtect(pointer, length * sizeof(T), new_protection, &old_protection);
		std::copy(data, data + length, reinterpret_cast<T*>(pointer));
		if (new_protection != old_protection) {
			VirtualProtect(pointer, length * sizeof(T), old_protection, &new_protection);
		}
	}
	template<typename T> inline void overwrite(void* pointer, const T& data) noexcept
	{
		overwrite(pointer, &data, 1);
	}

	class Hook {
	public:
		std::vector<std::byte> original_bytes;
		std::byte* address = nullptr;
		std::unique_ptr<std::byte[]> hook;

		void rollback() noexcept
		{
			if (original_bytes.empty()) return;
			overwrite(address, original_bytes.data(), original_bytes.size());
			original_bytes.clear();
		}
	};

	// Decode one or more x86 instructions starting at `at_start`, accumulating
	// raw bytes into `bytes` and per-instruction offsets into `offsets`, until
	// at least `minimum_size` bytes have been captured. Direct port of
	// Chimera's get_instructions(); terminates on unknown opcodes.
	static void get_instructions(const std::byte* at_start, std::vector<std::byte>& bytes, std::vector<std::uintptr_t>& offsets, std::size_t minimum_size)
	{
		offsets.clear();
		const auto* at = at_start;

		auto push_n = [&](std::size_t n) {
			offsets.push_back(at - at_start);
			bytes.insert(bytes.end(), at, at + n);
			at += n;
		};

		while (bytes.size() < minimum_size) {
			switch (*reinterpret_cast<const std::uint8_t*>(at)) {
			case 0x05: push_n(5); break;
			case 0x0F: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				auto op2 = *reinterpret_cast<const std::uint8_t*>(at + 2);
				if (op1 == 0x84) { push_n(6); break; }
				if (op1 == 0xBF || op1 == 0xB6 || op1 == 0xB7) {
					if (op2 == 0x6E || op2 == 0x4E || op2 == 0x4B || op2 == 0x43) { push_n(4); }
					else if (op2 == 0x15) { push_n(7); }
					else if (op2 == 0x54 || op2 == 0x44) { push_n(5); }
					else { push_n(3); }
					break;
				}
				std::terminate();
			}
			case 0x25: push_n(5); break;
			case 0x2B: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (op1 == 0x0D) { push_n(6); break; }
				std::terminate();
			}
			case 0x33: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (op1 == 0xDB || op1 == 0xF6 || op1 == 0xC9) { push_n(2); break; }
				std::terminate();
			}
			case 0x3B: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (op1 == 0xCD) { push_n(2); break; }
				std::terminate();
			}
			case 0x3D: push_n(5); break;
			case 0x50: case 0x54: case 0x58: case 0x5C: case 0x60:
			case 0x51: case 0x55: case 0x59: case 0x5D: case 0x61:
			case 0x52: case 0x56: case 0x5A: case 0x5E:
			case 0x53: case 0x57: case 0x5B: case 0x5F:
				push_n(1); break;
			case 0x66: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				auto op2 = *reinterpret_cast<const std::uint8_t*>(at + 2);
				if (op1 == 0x89) {
					if (op2 == 0x45 || op2 == 0x4A) push_n(4);
					else push_n(3);
					break;
				}
				if (op1 == 0xC7) {
					if (op2 == 0x45) push_n(6);
					else if (op2 == 0x44) push_n(7);
					else if (op2 == 0x05) push_n(9);
					break;
				}
				if (op1 == 0x29) { if (op2 == 0x8B) push_n(7); break; }
				if (op1 == 0xA3) { push_n(6); break; }
				if (op1 == 0x3B || op1 == 0x3D || op1 == 0x8B) {
					if (op2 == 0xCE) push_n(3);
					else push_n(4);
					break;
				}
				std::terminate();
			}
			case 0x68: push_n(5); break;
			case 0x69: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (op1 == 0xFF) { push_n(6); break; }
				std::terminate();
			}
			case 0x6A: push_n(2); break;
			case 0x74: push_n(2); break;
			case 0x7D: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (op1 < 0x80) { push_n(2); break; }
				std::terminate();
			}
			case 0x81: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (op1 == 0x3D) { push_n(10); break; }
				if (op1 >= 0xC0 || op1 == 0x0D) { push_n(6); break; }
				std::terminate();
			}
			case 0x83: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (op1 >= 0xC0) { push_n(3); } else { std::terminate(); }
				break;
			}
			case 0x84:
			case 0x85: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (op1 >= 0xC0) { push_n(2); break; }
				std::terminate();
			}
			case 0x89: {
				auto a = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (a == 0x06) { push_n(2); break; }
				if (a == 0x7D) { push_n(3); break; }
				if (a == 0x15 || a == 0x3D) { push_n(6); break; }
				if (a == 0x6C || a == 0x4C || a == 0x44 || a == 0x54) { push_n(4); break; }
				auto b = *reinterpret_cast<const std::uint8_t*>(at + 2);
				if (a == 0xC && b == 0x85) { push_n(7); break; }
				if ((a == 0x94 || a == 0x84) && b == 0x24) { push_n(7); break; }
				std::terminate();
			}
			case 0x8A: {
				auto a = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (a == 0x1C || a == 0x46 || a == 0x48 || a == 0x14) { push_n(3); break; }
				if (a == 0x54) { push_n(4); break; }
				std::terminate();
			}
			case 0x8B: {
				auto a = *reinterpret_cast<const std::uint8_t*>(at + 1);
				auto b = *reinterpret_cast<const std::uint8_t*>(at + 2);
				if ((a == 0x6C || a == 0x4C || a == 0x44 || a == 0x54) && b == 0x24) { push_n(4); break; }
				if (a == 0xE5 || a == 0xF8 || a == 0xC3 || a == 0xC2 || a == 0xEC || a == 0x12 || a == 0xF0) { push_n(2); break; }
				if (a == 0x50 || a == 0x40) { push_n(3); break; }
				if (a == 0x93 || a == 0x0D || a == 0x2D || a == 0x1D || a == 0x83 || a == 0x89 || a == 0x92 || a == 0x15) { push_n(6); break; }
				std::terminate();
			}
			case 0x8D: {
				auto a = *reinterpret_cast<const std::uint8_t*>(at + 1);
				auto b = *reinterpret_cast<const std::uint8_t*>(at + 2);
				if (a == 0x44 && (b == 0x0C || b == 0x24)) { push_n(4); break; }
				if (a == 0x7E) { push_n(3); break; }
				std::terminate();
			}
			case 0xC1: push_n(3); break;
			case 0xD3: {
				auto a = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (a == 0xE3) { push_n(2); break; }
				std::terminate();
			}
			case 0x90: push_n(1); break;
			case 0xA0: case 0xA1: case 0xA2: case 0xA3: push_n(5); break;
			case 0xB8: case 0xBA: case 0xBB: case 0xBE: case 0xBF: push_n(5); break;
			case 0xC7: {
				auto a = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (a == 0x05) { push_n(6); break; }
				if (a == 0x44) { push_n(8); break; }
				if (a == 0x45) { push_n(7); break; }
				std::terminate();
			}
			case 0xD8: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (op1 == 0x4F) { push_n(3); break; }
				if (op1 == 0x4C) { push_n(4); break; }
				if (op1 == 0x0D) { push_n(6); break; }
				std::terminate();
			}
			case 0xD9: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (op1 == 0x47 || op1 == 0x55 || op1 == 0x42 || op1 == 0x45 || op1 == 0x46) { push_n(3); break; }
				if (op1 == 0xC0) { push_n(2); break; }
				if (op1 == 0x1D || op1 == 0x05) { push_n(6); break; }
				if (op1 == 0x1C || op1 == 0x9C) { push_n(7); break; }
				std::terminate();
			}
			case 0xE8: push_n(5); break;
			case 0xF7: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (op1 == 0x05) { push_n(10); break; }
				std::terminate();
			}
			case 0xFF: {
				auto op1 = *reinterpret_cast<const std::uint8_t*>(at + 1);
				if (op1 == 0x51 || op1 == 0x52 || op1 == 0x56 || op1 == 0x57) { push_n(3); }
				else if (op1 == 0xD3) { push_n(2); }
				else if (op1 == 0x54) { push_n(4); }
				else if (op1 == 0x15 || op1 == 0x92 || op1 == 0x91) { push_n(6); }
				else { std::terminate(); }
				break;
			}
			default:
				shared::common::log("Interpolate", std::format("Unknown opcode 0x{:02X} at 0x{:X}",
					static_cast<unsigned>(*reinterpret_cast<const std::uint8_t*>(at)),
					reinterpret_cast<std::uintptr_t>(at)),
					shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
				std::terminate();
			}
		}
	}

	static void write_jmp_call(void* jmp_at, Hook& hook, const void* call_before, const void* call_after, bool pushad_pushfd = true)
	{
		hook.rollback();
		hook.address = reinterpret_cast<std::byte*>(jmp_at);

		std::vector<std::uintptr_t> offsets;
		std::vector<std::byte> bytes;
		std::byte* jmp_at_byte = reinterpret_cast<std::byte*>(jmp_at);
		get_instructions(jmp_at_byte, bytes, offsets, 5);

		std::size_t added_pushad_bytes = pushad_pushfd ? 4 : 0;
		std::size_t size = bytes.size()
			+ (call_before ? 5 + added_pushad_bytes : 0)
			+ (call_after ? 5 + added_pushad_bytes : 0)
			+ 5;

		hook.original_bytes.insert(hook.original_bytes.end(), jmp_at_byte, jmp_at_byte + bytes.size());

		hook.hook = std::make_unique<std::byte[]>(size);
		auto* hook_data = hook.hook.get();
		DWORD old_protection;
		VirtualProtect(hook_data, size, PAGE_EXECUTE_READWRITE, &old_protection);

		DWORD new_protection = PAGE_EXECUTE_READWRITE;
		VirtualProtect(jmp_at_byte, bytes.size(), new_protection, &old_protection);
		*reinterpret_cast<std::uint8_t*>(jmp_at_byte) = 0xE9;
		*reinterpret_cast<std::uintptr_t*>(jmp_at_byte + 1) = hook_data - (jmp_at_byte + 5);
		std::memset(jmp_at_byte + 5, 0x90, bytes.size() - 5);
		if (old_protection != new_protection) {
			VirtualProtect(jmp_at, bytes.size(), old_protection, &new_protection);
		}

		auto add_call = [&pushad_pushfd](const void* where, std::byte* data) {
			std::size_t call_offset = pushad_pushfd ? 2 : 0;
			if (pushad_pushfd) {
				*reinterpret_cast<std::uint8_t*>(data + 0) = 0x9C; // pushfd
				*reinterpret_cast<std::uint8_t*>(data + 1) = 0x60; // pushad
				*reinterpret_cast<std::uint8_t*>(data + 7) = 0x61; // popad
				*reinterpret_cast<std::uint8_t*>(data + 8) = 0x9D; // popfd
			}
			*reinterpret_cast<std::uint8_t*>(data + call_offset) = 0xE8;
			*reinterpret_cast<std::uintptr_t*>(data + call_offset + 1) =
				reinterpret_cast<const std::byte*>(where) - (data + call_offset + 5);
		};

		if (call_before) {
			add_call(call_before, hook_data);
			hook_data += 5 + added_pushad_bytes;
		}

		std::copy(bytes.data(), bytes.data() + bytes.size(), hook_data);

		// Fix up any relative E8 call instructions in the copied block.
		for (const std::uintptr_t& offset : offsets) {
			if (*reinterpret_cast<std::uint8_t*>(hook_data + offset) == 0xE8) {
				auto& op = *reinterpret_cast<std::uintptr_t*>(hook_data + offset + 1);
				const auto* actual_address = (jmp_at_byte + offset + 5) + op;
				op = reinterpret_cast<std::uintptr_t>(actual_address) -
					reinterpret_cast<std::uintptr_t>(hook_data + offset + 5);
			}
		}
		hook_data += bytes.size();

		if (call_after) {
			add_call(call_after, hook_data);
			hook_data += 5 + added_pushad_bytes;
		}

		// Jump back into Halo's code just past the original (now NOPped) bytes.
		*reinterpret_cast<std::uint8_t*>(hook_data) = 0xE9;
		*reinterpret_cast<std::uintptr_t*>(hook_data + 1) =
			(jmp_at_byte + bytes.size()) - (hook_data + 5);
	}

	//--------------------------------------------------------------------
	// Signature wrapper: maps Chimera's signature names to string patterns
	// understood by shared::utils::mem::find_pattern, and caches results.
	//--------------------------------------------------------------------

	struct SigDef { const char* name; const char* pattern; };

	static constexpr SigDef SIGNATURES[] = {
		// Tick/frame/camera/revert events
		{ "on_tick_sig",                       "E8 ?? ?? ?? ?? A1 ?? ?? ?? ?? 8B 50 14 8B 48 0C" },
		{ "on_frame_sig",                      "E8 ?? ?? ?? ?? 83 C4 08 89 3D" },
		{ "on_camera_sig",                     "E8 ?? ?? ?? ?? 8B 45 EC 8B 4D F0 40 81 C6" },
		{ "revert_sig",                        "57 8B 3D ?? ?? ?? ?? 83 CA FF E8 ?? ?? ?? ?? 83 F8 FF 0F 84 ?? ?? ?? ??" },
		// Tick / timing
		{ "tick_rate_sig",                     "D8 0D ?? ?? ?? ?? 83 EC 08 D9 5C 24 08 D9 44 24 14 D8 41 1C" },
		{ "game_speed_sig",                    "A1 ?? ?? ?? ?? 66 89 58 10 66 8B 0D ?? ?? ?? ?? 66 83 F9 01 74 1E 66 83 F9 02" },
		{ "tick_progress_sig",                 "A1 ?? ?? ?? ?? 8A 48 02 84 C9" },
		{ "tick_counter_sig",                  "A1 ?? ?? ?? ?? 8B 50 14 8B 48 0C 83 C4 04 42 41 4E 4F" },
		// Pause
		{ "game_paused_sig",                   "8B 15 ?? ?? ?? ?? 8A 42 02 84 C0 75 22 8B 0D" },
		// Tables
		{ "object_table_sig",                  "8B 0D ?? ?? ?? ?? 8B 51 34 25 FF FF 00 00 8D" },
		{ "antenna_table_sig",                 "8B 15 ?? ?? ?? ?? 8B C7 25 FF FF 00 00 C1 E0 05 55 8B 6C 08 14 89 6C 24 28" },
		{ "flag_table_sig",                    "8B 3D ?? ?? ?? ?? 83 C4 0C 8D 4E 01 83 CB FF 66 85 C9 7C 31" },
		{ "light_table_sig",                   "8B 0D ?? ?? ?? ?? 8B 51 34 56 8B F0 81 E6 FF FF 00 00 6B F6 7C" },
		{ "particle_table_sig",                "8B 2D ?? ?? ?? ?? 83 CA FF 8B FD E8 ?? ?? ?? ?? 8B F8 83 FF FF 0F 84 10 06 00 00" },
		{ "player_table_sig",                  "A1 ?? ?? ?? ?? 89 44 24 48 35" },
		// Camera
		{ "camera_type_sig",                   "81 C1 ?? ?? ?? ?? 8B 41 08 3D ?? ?? ?? ?? 75 1D D9 05" },
		{ "camera_coord_sig",                  "D9 05 ?? ?? ?? ?? 83 EC 18 DD 5C 24 10" },
		{ "camera_interpolation_sig",          "8D 54 24 ?? 52 E8 ?? ?? ?? ?? 83 C4 10 84 C0 74 ??" },
		// Visible-object / followed object
		{ "visible_object_count_sig",          "66 39 35 ?? ?? ?? ?? 57 8B F8 7E 1B 0F BF C6" },
		{ "visible_object_ptr_sig",            "8B 0C 85 ?? ?? ?? ?? 89 0F E8 0F 00 00 00 46" },
		{ "followed_object_sig",               "83 C8 FF 66 A3 ?? ?? ?? ?? A3 ?? ?? ?? ??" },
		// First-person
		{ "fp_cam_tick_rate_sig",              "D8 0D ?? ?? ?? ?? 83 C4 0C D8 15 ?? ?? ?? ?? DF E0 F6 C4 41 75 0A DD D8 D9 05" },
		{ "fp_interp_sig",                     "E8 ?? ?? ?? ?? 83 C4 10 5F 5E 5B 83 C4 18 C3" },
		{ "first_person_node_base_address_sig","8B 0D ?? ?? ?? ?? 53 8A 5C 24 08" },
	};

	struct CachedSig {
		std::byte* data = nullptr;
		bool resolved = false;
	};
	static std::unordered_map<std::string, CachedSig> g_sig_cache;

	// Look up a Chimera-named signature, scanning + caching on first call.
	// Returns nullptr if not found (caller is responsible for guarding).
	static std::byte* get_signature(const char* name) noexcept
	{
		auto it = g_sig_cache.find(name);
		if (it != g_sig_cache.end()) {
			return it->second.data;
		}

		const SigDef* def = nullptr;
		for (const auto& s : SIGNATURES) {
			if (std::strcmp(s.name, name) == 0) { def = &s; break; }
		}

		CachedSig cs;
		if (def) {
			const DWORD addr = shared::utils::mem::find_pattern(def->pattern, 0u, def->name, true, 0u);
			cs.data = reinterpret_cast<std::byte*>(addr);
			cs.resolved = (addr != 0);
		}
		g_sig_cache.emplace(name, cs);
		return cs.data;
	}

	//--------------------------------------------------------------------
	// Event system (from event/event.hpp + per-event files).
	//--------------------------------------------------------------------

	enum EventPriority {
		EVENT_PRIORITY_BEFORE,
		EVENT_PRIORITY_DEFAULT,
		EVENT_PRIORITY_AFTER,
		EVENT_PRIORITY_FINAL
	};

	template<typename T> struct Event {
		T function = nullptr;
		EventPriority priority = EVENT_PRIORITY_DEFAULT;
	};

	using EventFunction = void(*)();

	template<typename T, typename... Args>
	static inline void call_in_order(std::vector<Event<T>> events, Args&&... args)
	{
		auto call_events = [&](EventPriority priority) {
			for (const auto& e : events) {
				if (e.priority == priority) e.function(std::forward<Args>(args)...);
			}
		};
		call_events(EVENT_PRIORITY_BEFORE);
		call_events(EVENT_PRIORITY_DEFAULT);
		call_events(EVENT_PRIORITY_AFTER);
		call_events(EVENT_PRIORITY_FINAL);
	}

	// Per-event lists.
	static std::vector<Event<EventFunction>> g_pretick_events;
	static std::vector<Event<EventFunction>> g_tick_events;
	static std::vector<Event<EventFunction>> g_preframe_events;
	static std::vector<Event<EventFunction>> g_frame_events;
	static std::vector<Event<EventFunction>> g_precamera_events;
	static std::vector<Event<EventFunction>> g_camera_events;
	static std::vector<Event<EventFunction>> g_revert_events;

	static LARGE_INTEGER g_current_tick_time = {};

	static void on_pretick_dispatch() { call_in_order(g_pretick_events); }
	static void on_tick_dispatch()    { QueryPerformanceCounter(&g_current_tick_time); call_in_order(g_tick_events); }
	static void on_preframe_dispatch(){ call_in_order(g_preframe_events); }
	static void on_frame_dispatch()   { call_in_order(g_frame_events); }
	static void on_precamera_dispatch(){ call_in_order(g_precamera_events); }
	static void on_camera_dispatch()  { call_in_order(g_camera_events); }
	static void on_revert_dispatch()  { call_in_order(g_revert_events); }

	static Hook g_tick_hook;
	static Hook g_frame_hook;
	static Hook g_camera_hook;
	static Hook g_revert_hook;

	static bool g_tick_hooked = false;
	static bool g_frame_hooked = false;
	static bool g_camera_hooked = false;
	static bool g_revert_hooked = false;

	static void ensure_tick_hook()
	{
		if (g_tick_hooked) return;
		auto* sig = get_signature("on_tick_sig"); if (!sig) return;
		write_jmp_call(sig, g_tick_hook, reinterpret_cast<const void*>(on_pretick_dispatch), reinterpret_cast<const void*>(on_tick_dispatch));
		g_tick_hooked = true;
	}
	static void ensure_frame_hook()
	{
		if (g_frame_hooked) return;
		auto* sig = get_signature("on_frame_sig"); if (!sig) return;
		write_jmp_call(sig, g_frame_hook, reinterpret_cast<const void*>(on_preframe_dispatch), reinterpret_cast<const void*>(on_frame_dispatch));
		g_frame_hooked = true;
	}
	static void ensure_camera_hook()
	{
		if (g_camera_hooked) return;
		auto* sig = get_signature("on_camera_sig"); if (!sig) return;
		write_jmp_call(sig, g_camera_hook, reinterpret_cast<const void*>(on_precamera_dispatch), reinterpret_cast<const void*>(on_camera_dispatch));
		g_camera_hooked = true;
	}
	static void ensure_revert_hook()
	{
		if (g_revert_hooked) return;
		auto* sig = get_signature("revert_sig"); if (!sig) return;
		// Same +0xA offset as Chimera.
		write_jmp_call(sig + 0xA, g_revert_hook, reinterpret_cast<const void*>(on_revert_dispatch), nullptr);
		g_revert_hooked = true;
	}

	template<typename Vec>
	static void add_event_to(Vec& vec, EventFunction fn, EventPriority prio)
	{
		for (std::size_t i = 0; i < vec.size(); i++) if (vec[i].function == fn) { vec.erase(vec.begin() + i); break; }
		vec.emplace_back(Event<EventFunction>{ fn, prio });
	}
	template<typename Vec>
	static void remove_event_from(Vec& vec, EventFunction fn)
	{
		for (std::size_t i = 0; i < vec.size(); i++) if (vec[i].function == fn) { vec.erase(vec.begin() + i); return; }
	}

	static void add_pretick_event(EventFunction fn, EventPriority prio = EVENT_PRIORITY_DEFAULT)   { ensure_tick_hook();   add_event_to(g_pretick_events, fn, prio); }
	static void add_tick_event(EventFunction fn, EventPriority prio = EVENT_PRIORITY_DEFAULT)      { ensure_tick_hook();   add_event_to(g_tick_events, fn, prio); }
	static void add_preframe_event(EventFunction fn, EventPriority prio = EVENT_PRIORITY_DEFAULT)  { ensure_frame_hook();  add_event_to(g_preframe_events, fn, prio); }
	static void add_frame_event(EventFunction fn, EventPriority prio = EVENT_PRIORITY_DEFAULT)     { ensure_frame_hook();  add_event_to(g_frame_events, fn, prio); }
	static void add_precamera_event(EventFunction fn, EventPriority prio = EVENT_PRIORITY_DEFAULT) { ensure_camera_hook(); add_event_to(g_precamera_events, fn, prio); }
	static void add_camera_event(EventFunction fn, EventPriority prio = EVENT_PRIORITY_DEFAULT)    { ensure_camera_hook(); add_event_to(g_camera_events, fn, prio); }
	static void add_revert_event(EventFunction fn, EventPriority prio = EVENT_PRIORITY_DEFAULT)    { ensure_revert_hook(); add_event_to(g_revert_events, fn, prio); }

	static void remove_tick_event(EventFunction fn)      { remove_event_from(g_tick_events, fn); }
	static void remove_pretick_event(EventFunction fn)   { remove_event_from(g_pretick_events, fn); }
	static void remove_preframe_event(EventFunction fn)  { remove_event_from(g_preframe_events, fn); }
	static void remove_frame_event(EventFunction fn)     { remove_event_from(g_frame_events, fn); }
	static void remove_precamera_event(EventFunction fn) { remove_event_from(g_precamera_events, fn); }
	static void remove_camera_event(EventFunction fn)    { remove_event_from(g_camera_events, fn); }
	static void remove_revert_event(EventFunction fn)    { remove_event_from(g_revert_events, fn); }

	//--------------------------------------------------------------------
	// Tick timing accessors (from event/tick.cpp).
	//--------------------------------------------------------------------

	static float tick_rate() noexcept
	{
		static float* tick_ptr = nullptr;
		if (!tick_ptr) {
			auto* s = get_signature("tick_rate_sig"); if (!s) return 30.0f;
			tick_ptr = *reinterpret_cast<float**>(s + 2);
		}
		return *tick_ptr;
	}

	static float effective_tick_rate() noexcept
	{
		static const float* game_speed_ptr = nullptr;
		if (!game_speed_ptr) {
			auto* s = get_signature("game_speed_sig"); if (!s) return tick_rate();
			game_speed_ptr = reinterpret_cast<float*>(**reinterpret_cast<std::byte***>(s + 1) + 0x18);
		}
		return *game_speed_ptr * tick_rate();
	}

	static float get_tick_progress() noexcept
	{
		static std::optional<float*> tick_progress;
		if (!tick_progress.has_value()) {
			auto* s = get_signature("tick_progress_sig"); if (!s) return 0.0f;
			tick_progress = reinterpret_cast<float*>(**reinterpret_cast<std::byte***>(s + 1) + 304);
		}
		float v = effective_tick_rate() * **tick_progress;
		if (v > 1) v = 1;
		return v;
	}

	//--------------------------------------------------------------------
	// Pause (halo_data/pause.cpp).
	//--------------------------------------------------------------------

	static bool game_paused() noexcept
	{
		static std::optional<std::byte**> paused_addr;
		if (!paused_addr.has_value()) {
			auto* s = get_signature("game_paused_sig"); if (!s) return false;
			paused_addr = *reinterpret_cast<std::byte***>(s + 2);
		}
		return *reinterpret_cast<bool*>(*paused_addr.value() + 2);
	}

	//--------------------------------------------------------------------
	// Generic table (from halo_data/table.hpp).
	//--------------------------------------------------------------------

	template<typename T>
	struct GenericTable {
		char     name[0x20];
		std::uint16_t max_elements;
		std::uint16_t element_size;
		std::uint32_t unknown0;
		std::uint32_t unknown1;
		std::uint16_t current_size;
		std::uint16_t count;
		std::uint16_t next_id;
		std::uint16_t first_element_index;
		T*       first_element;

		// Match Chimera: bound by current_size (slots Halo has actually
		// initialised, including gaps). Reading past it is undefined.
		T* get_element(std::size_t index) noexcept {
			if (index >= current_size) return nullptr;
			return &first_element[index];
		}
	};
	static_assert(sizeof(GenericTable<int>) == 0x38);

	//--------------------------------------------------------------------
	// Object table + base dynamic object + nodes (halo_data/object.{hpp,cpp}).
	// Only the fields/offsets actually touched by the interpolation port.
	//--------------------------------------------------------------------

	#define INTERP_PAD(n) char _pad_##__LINE__[n]

	enum ObjectType : std::uint16_t {
		OBJECT_TYPE_BIPED = 0,
		OBJECT_TYPE_VEHICLE,
		OBJECT_TYPE_WEAPON,
		OBJECT_TYPE_EQUIPMENT,
		OBJECT_TYPE_GARBAGE,
		OBJECT_TYPE_PROJECTILE,
		OBJECT_TYPE_SCENERY,
		OBJECT_TYPE_DEVICE_MACHINE,
		OBJECT_TYPE_DEVICE_CONTROL,
		OBJECT_TYPE_DEVICE_LIGHT_FIXTURE,
		OBJECT_TYPE_PLACEHOLDER,
		OBJECT_TYPE_SOUND_SCENERY
	};

	struct ModelNode {
		float scale;
		RotationMatrix rotation;
		Point3D position;
	};
	static_assert(sizeof(ModelNode) == 0x34);

	static constexpr std::size_t MAX_NODES = 64;

	// Object field accessors. The Halo CE 1.10 BaseDynamicObject is huge and
	// the per-type layout varies, so we treat the object as an opaque byte
	// blob and address fields by offset.

	static constexpr std::size_t OFFSET_OBJECT_TAG_ID             = 0x00; // TagID
	static constexpr std::size_t OFFSET_OBJECT_FLAGS_NO_COLLISION = 0x10; // u32, bit 0 = no_collision
	static constexpr std::size_t OFFSET_OBJECT_POSITION           = 0x5C; // Point3D
	static constexpr std::size_t OFFSET_OBJECT_VELOCITY           = 0x68; // Point3D
	static constexpr std::size_t OFFSET_OBJECT_CENTER             = 0xA0; // Point3D
	static constexpr std::size_t OFFSET_OBJECT_TYPE               = 0xB4; // ObjectType (u16)
	static constexpr std::size_t OFFSET_OBJECT_HEALTH             = 0xE0; // float
	static constexpr std::size_t OFFSET_OBJECT_PARENT             = 0x11C;// ObjectID (BaseDynamicObject + 0x118 is `weapon`)

	// Per-type node array offsets, from Chimera's BaseDynamicObject::nodes().
	static constexpr std::size_t NODE_OFFSETS[] = {
		0x550, // BIPED
		0x5C0, // VEHICLE
		0x340, // WEAPON
		0x294, // EQUIPMENT
		0x244, // GARBAGE
		0x2B0, // PROJECTILE
		0x1F8, // SCENERY
		0x228, // DEVICE_MACHINE
		0x21C, // DEVICE_CONTROL
	};

	static inline std::byte* obj_bytes(void* o) noexcept { return reinterpret_cast<std::byte*>(o); }

	static inline TagID&     obj_tag_id(void* o)   noexcept { return *reinterpret_cast<TagID*>(obj_bytes(o) + OFFSET_OBJECT_TAG_ID); }
	static inline ObjectType obj_type(void* o)     noexcept { return *reinterpret_cast<ObjectType*>(obj_bytes(o) + OFFSET_OBJECT_TYPE); }
	static inline bool       obj_no_collision(void* o) noexcept {
		auto flags = *reinterpret_cast<std::uint32_t*>(obj_bytes(o) + OFFSET_OBJECT_FLAGS_NO_COLLISION);
		return (flags & 0x1u) != 0;
	}
	static inline ObjectID&  obj_parent(void* o)   noexcept { return *reinterpret_cast<ObjectID*>(obj_bytes(o) + OFFSET_OBJECT_PARENT); }
	static inline Point3D&   obj_position(void* o) noexcept { return *reinterpret_cast<Point3D*>(obj_bytes(o) + OFFSET_OBJECT_POSITION); }
	static inline Point3D&   obj_velocity(void* o) noexcept { return *reinterpret_cast<Point3D*>(obj_bytes(o) + OFFSET_OBJECT_VELOCITY); }
	static inline float      obj_health(void* o)   noexcept { return *reinterpret_cast<float*>(obj_bytes(o) + OFFSET_OBJECT_HEALTH); }
	static inline Point3D&   obj_center(void* o)   noexcept { return *reinterpret_cast<Point3D*>(obj_bytes(o) + OFFSET_OBJECT_CENTER); }

	// nodes() returns a pointer to the per-type node array embedded inside
	// the object (not a separately-allocated pointer). Returns nullptr for
	// object types without a known node offset.
	static inline ModelNode* obj_nodes(void* o) noexcept {
		auto t = obj_type(o);
		if (static_cast<std::size_t>(t) >= sizeof(NODE_OFFSETS) / sizeof(NODE_OFFSETS[0])) return nullptr;
		return reinterpret_cast<ModelNode*>(obj_bytes(o) + NODE_OFFSETS[t]);
	}

	// Chimera's ObjectTableIndexHeader: {u16 id; 6 bytes pad; T* object;}
	struct ObjectTableEntry {
		std::uint16_t id;
		char          pad[6];
		void*         object;
	};
	static_assert(sizeof(ObjectTableEntry) == 0xC);

	struct ObjectTable : GenericTable<ObjectTableEntry> {
		void* get_dynamic_object(std::uint32_t index) noexcept {
			auto* e = get_element(index);
			return (e && e->object) ? e->object : nullptr;
		}
		void* get_dynamic_object(const ObjectID& id) noexcept {
			auto* e = get_element(id.index.index);
			return (e && e->id == id.index.id && e->object) ? e->object : nullptr;
		}
		static ObjectTable& get_object_table() noexcept {
			static auto& table = ***reinterpret_cast<ObjectTable***>(get_signature("object_table_sig") + 2);
			return table;
		}
	};

	//--------------------------------------------------------------------
	// Tag data accessors (from halo_data/tag.hpp).
	// The retail PC Halo 1.10 tag-data header lives at a fixed VA.
	//--------------------------------------------------------------------

	struct Tag {
		std::uint32_t primary_class;
		std::uint32_t secondary_class;
		std::uint32_t tertiary_class;
		TagID         id;
		char*         path;
		std::byte*    data;
		std::uint32_t indexed;
		std::uint32_t externally_loaded;
	};
	static_assert(sizeof(Tag) == 0x20);

	struct TagDataHeader {
		Tag*          tag_array;
		TagID         scenario_tag;
		std::uint32_t checksum;
		std::uint32_t tag_count;
		std::uint32_t model_part_count;
		std::uint32_t model_data_file_offset;
		std::uint32_t model_part_count_again;
		std::uint32_t vertex_size;
		std::uint32_t model_data_size;
		std::uint32_t tags_literal;
	};
	static_assert(sizeof(TagDataHeader) == 0x28);

	static inline TagDataHeader& get_tag_data_header() noexcept {
		return *reinterpret_cast<TagDataHeader*>(0x40440000);
	}

	static inline Tag* get_tag(TagID tag_id) noexcept {
		if (tag_id.is_null()) return nullptr;
		auto& hdr = get_tag_data_header();
		if (tag_id.index.index >= hdr.tag_count) return nullptr;
		return hdr.tag_array + tag_id.index.index;
	}
	//--------------------------------------------------------------------
	// Antenna / Flag / Light / Particle / Camera / Player.
	//--------------------------------------------------------------------

	struct AntennaVertex { Point3D position; Point3D velocity; float scale; std::uint32_t counter; };
	struct Antenna {
		std::uint32_t unknown_0;
		std::uint32_t unknown_1;
		std::uint32_t tag_id;
		std::uint32_t parent_object_id;
		Point3D       position;
		AntennaVertex vertices[0x15];
	};
	struct AntennaTable : GenericTable<Antenna> {
		static AntennaTable& get_antenna_table() noexcept {
			static auto* table = **reinterpret_cast<AntennaTable***>(get_signature("antenna_table_sig") + 2);
			return *table;
		}
	};

	struct FlagPart { Point3D position; Point3D velocity; };
	static_assert(sizeof(FlagPart) == 0x18);
	struct Flag {
		std::uint32_t some_id;
		std::uint32_t unknown0;
		ObjectID parent_object_id;
		std::uint32_t some_id1;
		Point3D  position;
		FlagPart parts[241];
		char     padding[8];
	};
	static_assert(sizeof(Flag) == 0x16BC);
	struct FlagTable : GenericTable<Flag> {
		static FlagTable& get_flag_table() noexcept {
			static auto* table = **reinterpret_cast<FlagTable***>(get_signature("flag_table_sig") + 2);
			return *table;
		}
	};

	struct Light {
		std::uint32_t unknown0;
		std::uint32_t some_id;
		std::uint32_t unknown1;
		std::uint32_t some_counter;
		std::uint32_t unknown2;
		float red, green, blue;
		std::uint32_t unknown3, unknown4, unknown5, parent_object_id;
		Point3D position;
		Point3D orientation[2];
		char    pad_tail[0x28];
	};
	static_assert(sizeof(Light) == 0x7C);
	struct LightTable : GenericTable<Light> {
		static LightTable& get_light_table() noexcept {
			static auto* table = **reinterpret_cast<LightTable***>(get_signature("light_table_sig") + 2);
			return *table;
		}
	};

	struct Particle {
		std::uint32_t unknown0;
		std::uint32_t tag_id;
		std::uint32_t unknown1;
		std::uint32_t unknown2;
		std::uint32_t frames_alive;
		float a0, a, b;
		float c;
		std::uint32_t unknown3, unknown4, some_id;
		Point3D position;
		float ux0;
		float uy0, uz0, ux1, uy1;
		float uz1, radius_x, radius_y, radius_z;
		std::uint32_t unknown5;
		float red, green, blue;
	};
	static_assert(sizeof(Particle) == 0x70);
	struct ParticleTable : GenericTable<Particle> {
		static ParticleTable& get_particle_table() noexcept {
			static auto* table = **reinterpret_cast<ParticleTable***>(get_signature("particle_table_sig") + 2);
			return *table;
		}
	};

	enum CameraType : std::uint16_t {
		CAMERA_FIRST_PERSON = 0,
		CAMERA_VEHICLE,
		CAMERA_CINEMATIC,
		CAMERA_DEBUG
	};

	struct CameraData {
		Point3D       position;
		std::uint32_t unknown[5];
		Point3D       orientation[2];
		float         fov;
	};
	static_assert(sizeof(CameraData) == 0x3C);

	static CameraType camera_type() noexcept
	{
		static auto* cta = reinterpret_cast<CameraType*>(*reinterpret_cast<std::byte**>(get_signature("camera_type_sig") + 0x2) + 0x56);
		return *cta;
	}
	static CameraData& camera_data() noexcept
	{
		static std::optional<CameraData*> camera_coord_addr;
		if (!camera_coord_addr.has_value()) {
			camera_coord_addr = reinterpret_cast<CameraData*>(*reinterpret_cast<std::byte**>(get_signature("camera_coord_sig") + 2) - 0x8);
		}
		return **camera_coord_addr;
	}

	// Player: only the few fields actually read by camera.cpp.
	#pragma pack(push, 1)
	struct PlayerStub {
		char     pad0[0x34];      // 0x00 .. 0x33
		ObjectID object_id;       // 0x34
	};
	#pragma pack(pop)

	struct PlayerTable : GenericTable<PlayerStub> {
		PlayerStub* get_client_player() noexcept {
			// We don't resolve player_id_sig in this build (the lookup table
			// has no entry for it), so we can't pick out the "real" client
			// player slot. The camera path only needs an ObjectID for
			// skip-detection, and on single-player the client is always at
			// slot 0, so use that as a safe stand-in.
			if (current_size == 0) return nullptr;
			return &first_element[0];
		}
		static PlayerTable& get_player_table() noexcept {
			static PlayerTable* table = nullptr;
			if (!table) {
				table = *reinterpret_cast<PlayerTable**>(*reinterpret_cast<std::byte**>(get_signature("player_table_sig") + 1));
			}
			return *table;
		}
	};

	//--------------------------------------------------------------------
	// Spectate flag (Chimera's camera.cpp references this).
	//--------------------------------------------------------------------
	static bool spectate_enabled = false;

	// Tick progress fraction shared by every interpolator. Updated on each
	// preframe and cleared on each tick.
	static float interpolation_tick_progress = 0.0f;
	static bool  interpolation_enabled = false;

	//====================================================================
	// Per-system interpolators (direct ports of fix/interpolate/*.cpp).
	//====================================================================

	//--- antenna --------------------------------------------------------
	namespace antenna_sys {
		constexpr std::size_t MAX_ANTENNA = 0xC;
		static Antenna buffers[2][MAX_ANTENNA] = {};
		static Antenna* current_tick = buffers[0];
		static Antenna* previous_tick = buffers[1];
		static bool tick_passed = false;

		static void before() noexcept
		{
			auto& table = AntennaTable::get_antenna_table();
			if (tick_passed) {
				std::swap(current_tick, previous_tick);
				tick_passed = false;
				std::copy(table.first_element, table.first_element + MAX_ANTENNA, current_tick);
			}
			for (std::size_t i = 0; i < table.current_size && i < MAX_ANTENNA; i++) {
				auto& cur = current_tick[i];
				auto& prev = previous_tick[i];
				auto& mem = table.first_element[i];
				interpolate_point(prev.position, cur.position, mem.position, interpolation_tick_progress);
				for (std::size_t v = 0; v < sizeof(cur.vertices) / sizeof(cur.vertices[0]); v++) {
					interpolate_point(prev.vertices[v].position, cur.vertices[v].position, mem.vertices[v].position, interpolation_tick_progress);
				}
			}
		}
		static void after() noexcept
		{
			auto& table = AntennaTable::get_antenna_table();
			for (std::size_t i = 0; i < table.current_size && i < MAX_ANTENNA; i++) {
				auto& cur = current_tick[i];
				auto& mem = table.first_element[i];
				mem.position = cur.position;
				for (std::size_t v = 0; v < sizeof(cur.vertices) / sizeof(cur.vertices[0]); v++) {
					mem.vertices[v].position = cur.vertices[v].position;
				}
			}
		}
		static void on_tick() noexcept { tick_passed = true; }
	}

	//--- flag -----------------------------------------------------------
	namespace flag_sys {
		constexpr std::size_t MAX_FLAG = 0x2;
		struct InterpolatedFlag { int counter = 0; Flag flag_data; };
		static InterpolatedFlag buffers[2][MAX_FLAG];
		static InterpolatedFlag* current_tick = buffers[0];
		static InterpolatedFlag* previous_tick = buffers[1];
		static bool tick_passed = false;

		static void before() noexcept
		{
			auto& flags = FlagTable::get_flag_table();
			if (tick_passed) {
				std::swap(current_tick, previous_tick);
				tick_passed = false;
				auto& object_table = ObjectTable::get_object_table();
				for (std::size_t i = 0; i < MAX_FLAG; i++) {
					auto& cur = current_tick[i];
					auto& prev = previous_tick[i];
					auto* flag = flags.get_element(i);
					if (flag) {
						cur.flag_data = *flag;
						auto* obj = object_table.get_dynamic_object(flag->parent_object_id);
						if (!obj) { cur.counter = 0; prev.counter = 0; continue; }
						bool moved = false;
						for (std::size_t f = 0; f < sizeof(flag->parts) / sizeof(flag->parts[0]); f++) {
							if (distance_squared(prev.flag_data.parts[f].position, cur.flag_data.parts[f].position) > 0.00001f) {
								moved = true; break;
							}
						}
						if (moved) { cur.counter++; prev.counter++; }
						else       { cur.counter = 0; prev.counter = 0; }
					} else {
						cur.counter = 0; prev.counter = 0;
					}
				}
			}

			for (std::size_t i = 0; i < flags.current_size && i < MAX_FLAG; i++) {
				auto& cur = current_tick[i];
				auto& prev = previous_tick[i];
				if (cur.counter < 3) continue;
				auto* flag = flags.get_element(i);
				if (flag) {
					for (std::size_t f = 0; f < sizeof(flag->parts) / sizeof(flag->parts[0]); f++) {
						interpolate_point(prev.flag_data.parts[f].position, cur.flag_data.parts[f].position, flag->parts[f].position, interpolation_tick_progress);
					}
				}
			}
		}
		static void clear() noexcept { std::memset(buffers, 0, sizeof(buffers)); }
		static void on_tick() noexcept { tick_passed = true; }
	}

	//--- light ----------------------------------------------------------
	namespace light_sys {
		constexpr std::size_t MAX_LIGHT = 0x380;
		struct InterpolatedLight {
			bool interpolate = false;
			Point3D position;
			Point3D orientation[2];
			std::uint32_t some_counter = 0;
		};
		static InterpolatedLight buffers[2][MAX_LIGHT];
		static InterpolatedLight* current_tick = buffers[0];
		static InterpolatedLight* previous_tick = buffers[1];
		static bool tick_passed = false;

		static void before() noexcept
		{
			auto& table = LightTable::get_light_table();
			if (tick_passed) {
				std::swap(current_tick, previous_tick);
				tick_passed = false;
				for (std::size_t i = 0; i < MAX_LIGHT; i++) {
					current_tick[i].interpolate = false;
					auto* light = table.get_element(i);
					if (!light) continue;
					auto& cur = current_tick[i];
					cur.some_counter = light->some_counter;
					if (cur.some_counter > previous_tick[i].some_counter) {
						cur.interpolate = true;
						cur.position = light->position;
						cur.orientation[0] = light->orientation[0];
						cur.orientation[1] = light->orientation[1];
					}
				}
			}
			for (std::size_t i = 0; i < table.current_size && i < MAX_LIGHT; i++) {
				auto& cur = current_tick[i];
				auto& prev = previous_tick[i];
				auto& mem = table.first_element[i];
				if (cur.interpolate && prev.interpolate) {
					interpolate_point(prev.orientation[0], cur.orientation[0], mem.orientation[0], interpolation_tick_progress);
					interpolate_point(prev.orientation[1], cur.orientation[1], mem.orientation[1], interpolation_tick_progress);
					interpolate_point(prev.position,        cur.position,        mem.position,        interpolation_tick_progress);
				}
			}
		}
		static void clear() noexcept { std::memset(buffers, 0, sizeof(buffers)); }
		static void on_tick() noexcept { tick_passed = true; }
	}

	//--- particle -------------------------------------------------------
	namespace particle_sys {
		constexpr std::size_t PARTICLE_BUFFER_SIZE = 1024;
		struct InterpolatedParticle { bool interpolate; Point3D position; };
		static InterpolatedParticle buffers[2][PARTICLE_BUFFER_SIZE] = {};
		static InterpolatedParticle* current_tick = buffers[0];
		static InterpolatedParticle* previous_tick = buffers[1];
		static bool tick_passed = false;

		static void run() noexcept
		{
			auto& table = ParticleTable::get_particle_table();
			if (tick_passed) {
				std::swap(current_tick, previous_tick);
				for (std::size_t i = 0; i < PARTICLE_BUFFER_SIZE; i++) {
					auto* p = table.get_element(i);
					auto& cur = current_tick[i];
					cur.interpolate = false;
					if (!p) continue;
					cur.position = p->position;
					cur.interpolate = (p->unknown0 & 0xFFFF) != 0;
				}
				tick_passed = false;
			}
			for (std::size_t i = 0; i < table.current_size && i < PARTICLE_BUFFER_SIZE; i++) {
				auto* p = table.first_element + i;
				auto& cur = current_tick[i];
				auto& prev = previous_tick[i];
				if (cur.interpolate && prev.interpolate) {
					interpolate_point(prev.position, cur.position, p->position, interpolation_tick_progress);
				}
			}
		}
		static void after() noexcept
		{
			auto& table = ParticleTable::get_particle_table();
			for (std::size_t i = 0; i < table.current_size && i < PARTICLE_BUFFER_SIZE; i++) {
				auto* p = table.get_element(i);
				if (!p) continue;
				auto& cur = current_tick[i];
				auto& prev = previous_tick[i];
				if (cur.interpolate && prev.interpolate) {
					p->position = cur.position;
				}
			}
		}
		static void clear() noexcept
		{
			for (std::size_t i = 0; i < PARTICLE_BUFFER_SIZE; i++) {
				current_tick[i].interpolate = false;
				previous_tick[i].interpolate = false;
			}
		}
		static void on_tick() noexcept { tick_passed = true; }
	}

	//--- first person ---------------------------------------------------
	namespace fp_sys {
		struct FirstPersonNode { Quaternion orientation; Point3D position; float scale; };
		static_assert(sizeof(FirstPersonNode) == 0x20);

		static std::byte* first_person_nodes() noexcept {
			static std::optional<std::byte*> cached;
			if (!cached.has_value()) {
				auto* s = get_signature("first_person_node_base_address_sig");
				if (!s) { cached = nullptr; return nullptr; }
				cached = **reinterpret_cast<std::byte***>(s + 2);
			}
			return cached.value();
		}

		constexpr std::size_t NODES_PER_BUFFER = 128;
		static FirstPersonNode buffers[2][NODES_PER_BUFFER] = {};
		static FirstPersonNode* current_tick = buffers[0];
		static FirstPersonNode* previous_tick = buffers[1];

		static bool skip = false;
		static bool revert = false;
		static bool tick_passed = false;

		static void before() noexcept
		{
			if (game_paused()) return;
			std::byte* base = first_person_nodes();
			if (!base) return;
			FirstPersonNode* fpn = reinterpret_cast<FirstPersonNode*>(base + 0x8C);

			if (tick_passed) {
				static std::uint32_t last_weapon = ~0u;
				std::uint32_t current_weapon = *reinterpret_cast<std::uint32_t*>(base + 8);

				// Faithful to Chimera's intent: skip when the weapon swaps.
				skip = (last_weapon != current_weapon);

				if (revert) { skip = true; revert = false; }

				std::swap(current_tick, previous_tick);
				last_weapon = current_weapon;
				tick_passed = false;
				std::copy(fpn, fpn + NODES_PER_BUFFER, current_tick);
			}

			if (!skip) {
				for (std::size_t i = 0; i < NODES_PER_BUFFER; i++) {
					interpolate_quat(previous_tick[i].orientation, current_tick[i].orientation, fpn[i].orientation, interpolation_tick_progress);
					interpolate_point(previous_tick[i].position, current_tick[i].position, fpn[i].position, interpolation_tick_progress);
					fpn[i].scale = previous_tick[i].scale + (current_tick[i].scale - previous_tick[i].scale) * interpolation_tick_progress;
				}
			}
		}

		static void after() noexcept
		{
			if (skip || game_paused()) return;
			std::byte* base = first_person_nodes();
			if (!base) return;
			FirstPersonNode* fpn = reinterpret_cast<FirstPersonNode*>(base + 0x8C);
			std::copy(current_tick, current_tick + NODES_PER_BUFFER, fpn);
		}
		static void clear() noexcept
		{
			skip = true; revert = true;
			std::memset(buffers, 0, sizeof(buffers));
		}
		static void on_tick() noexcept { tick_passed = true; }
	}

	//--- camera ---------------------------------------------------------
	namespace camera_sys {
		struct InterpolatedCamera { CameraType type; ObjectID followed_object; CameraData data; };
		static InterpolatedCamera buffers[2];
		static InterpolatedCamera* current_tick = buffers + 0;
		static InterpolatedCamera* previous_tick = buffers + 1;
		static bool tick_passed = false;
		static bool skip = false;
		static bool rollback = false;

		static void before() noexcept
		{
			if (game_paused()) return;
			auto type = camera_type();

			if (tick_passed) {
				std::swap(current_tick, previous_tick);
				static auto** followed_object = reinterpret_cast<ObjectID**>(get_signature("followed_object_sig") + 10);
				current_tick->data = camera_data();
				current_tick->type = type;
				current_tick->followed_object = **followed_object;
				tick_passed = false;

				skip = (type == CAMERA_CINEMATIC && current_tick->followed_object.is_null()) ||
					(current_tick->followed_object != previous_tick->followed_object || current_tick->type != previous_tick->type);

				if (!skip && type == CAMERA_FIRST_PERSON) {
					skip = distance_squared(previous_tick->data.position, current_tick->data.position) > 5.0f * 5.0f;
				}
			}

			if (skip) return;

			auto& data = camera_data();
			bool vehicle_first_person = false;

			if (type == CAMERA_FIRST_PERSON || type == CAMERA_DEBUG) {
				auto* player = PlayerTable::get_player_table().get_client_player();
				if (player) {
					auto* object = ObjectTable::get_object_table().get_dynamic_object(player->object_id);
					if (object) {
						vehicle_first_person = !obj_parent(object).is_null();
						if (type == CAMERA_DEBUG && obj_health(object) >= 0.0f) {
							skip = true; return;
						}
						if (type == CAMERA_FIRST_PERSON && !vehicle_first_person &&
							distance_squared(previous_tick->data.position, current_tick->data.position) > 0.5f * 0.5f &&
							magnitude_squared(obj_velocity(object)) <= 0.5f * 0.5f)
						{
							skip = true; return;
						}
					}
				}
			}

			interpolate_point(previous_tick->data.position, current_tick->data.position, data.position, interpolation_tick_progress);
			if (type != CAMERA_FIRST_PERSON || vehicle_first_person || spectate_enabled) {
				interpolate_point(previous_tick->data.orientation[0], current_tick->data.orientation[0], data.orientation[0], interpolation_tick_progress);
				interpolate_point(previous_tick->data.orientation[1], current_tick->data.orientation[1], data.orientation[1], interpolation_tick_progress);
				rollback = true;
			}
		}
		static void after() noexcept
		{
			if (skip || game_paused()) return;
			auto& data = camera_data();
			data.position = current_tick->data.position;
			if (rollback) {
				std::copy(current_tick->data.orientation, current_tick->data.orientation + 1, data.orientation);
				rollback = false;
			}
		}
		static void clear() noexcept { skip = true; std::memset(buffers, 0, sizeof(buffers)); }
		static void on_tick() noexcept { tick_passed = true; }
	}

	//--- object ---------------------------------------------------------
	namespace object_sys {
		constexpr std::size_t OBJECT_BUFFER_SIZE = 2048;
		struct InterpolatedObject {
			bool interpolate = false;
			bool interpolated_this_frame = false;
			std::size_t children_count = 0;
			std::size_t children[OBJECT_BUFFER_SIZE];
			TagID tag_id;
			std::uint16_t index;
			Point3D center;
			std::size_t node_count;
			ModelNode nodes[MAX_NODES];
		};
		static InterpolatedObject buffers[2][OBJECT_BUFFER_SIZE];
		static InterpolatedObject* current_tick = buffers[0];
		static InterpolatedObject* previous_tick = buffers[1];
		static bool tick_passed = false;

		static void interpolate_one(std::size_t index)
		{
			if (index >= OBJECT_BUFFER_SIZE) return;
			auto& cur = current_tick[index];
			auto& prev = previous_tick[index];
			if (!cur.interpolate || cur.interpolated_this_frame) return;

			auto* object = ObjectTable::get_object_table().get_dynamic_object(static_cast<std::uint32_t>(index));
			if (!object) return;

			auto& tag_id = obj_tag_id(object);
			if (tag_id != cur.tag_id || prev.tag_id != tag_id) return;
			if (cur.index != prev.index) return;
			if (prev.node_count != cur.node_count) return;

			cur.interpolated_this_frame = true;

			for (std::size_t i = 0; i < cur.children_count; i++) {
				interpolate_one(cur.children[i]);
			}

			interpolate_point(prev.center, cur.center, obj_center(object), interpolation_tick_progress);

			auto* nodes = obj_nodes(object);
			if (!nodes) return;
			for (std::size_t n = 0; n < cur.node_count; n++) {
				auto& node = nodes[n];
				auto& nc = cur.nodes[n];
				auto& nb = prev.nodes[n];
				interpolate_point(nb.position, nc.position, node.position, interpolation_tick_progress);
				node.scale = nb.scale + (nc.scale - nb.scale) * interpolation_tick_progress;
				Quaternion qc = quat_from_matrix(nc.rotation);
				Quaternion qb = quat_from_matrix(nb.rotation);
				Quaternion qi;
				interpolate_quat(qb, qc, qi, interpolation_tick_progress);
				node.rotation = matrix_from_quat(qi);
			}
		}

		static void copy_objects() noexcept
		{
			auto& table = ObjectTable::get_object_table();
			ObjectID parent_array[OBJECT_BUFFER_SIZE];
			for (std::size_t i = 0; i < OBJECT_BUFFER_SIZE; i++) parent_array[i] = HaloID::null_id();

			auto max_size = table.current_size;
			for (std::size_t i = 0; i < OBJECT_BUFFER_SIZE; i++) {
				auto& cur = current_tick[i];
				cur.interpolated_this_frame = false;
				cur.interpolate = false;
				// table.first_element[] is sized to max_elements; reading
				// past current_size yields stale-but-allocated entries.
				cur.index = table.first_element[i].id;
				cur.children_count = 0;

				auto* object = table.get_dynamic_object(static_cast<std::uint32_t>(i));
				if (!object) continue;

				ObjectType ot = obj_type(object);
				bool is_weapon = (ot == OBJECT_TYPE_WEAPON);
				if (is_weapon && obj_no_collision(object)) continue;

				auto* nodes = obj_nodes(object);
				if (!nodes) continue;

				cur.tag_id = obj_tag_id(object);

				// Look up node_count from the object's model tag. Projectiles
				// always have exactly one node (per Chimera).
				if (ot == OBJECT_TYPE_PROJECTILE) {
					cur.node_count = 1;
				}
				else {
					auto* object_tag = get_tag(cur.tag_id);
					if (!object_tag || !object_tag->data) continue;
					// Object-tag layout: model TagID lives at +0x34 inside the
					// shared base_object data block (offset 0x28 + 0xC).
					const auto& model_tag_id = *reinterpret_cast<const TagID*>(object_tag->data + 0x34);
					auto* model_tag = get_tag(model_tag_id);
					if (!model_tag || !model_tag->data) continue;
					cur.node_count = *reinterpret_cast<std::uint32_t*>(model_tag->data + 0xB8);
				}

				if (cur.node_count == 0 || cur.node_count > MAX_NODES) continue;

				std::copy(nodes, nodes + cur.node_count, cur.nodes);
				cur.center = obj_center(object);

				// Bipeds get a 2.5-unit-per-tick speed cap; everything else 7.5.
				static const float MAX_DIST[] = { 7.5f * 7.5f, 2.5f * 2.5f };
				cur.interpolate = distance_squared(cur.center, previous_tick[i].center)
					< MAX_DIST[ot == OBJECT_TYPE_BIPED];

				if (ot == OBJECT_TYPE_DEVICE_MACHINE) {
					// Chimera's bodge here checks DeviceMachineDynamicObject::device_position
					// for a 0.9 -> 0.002 snap (closing doors). Under RTX Remix we cannot
					// safely use it: rotating scenery devices (e.g. the main menu's Mark V
					// ring) reuse device_position as a 0..1 rotation phase that wraps each
					// cycle, which trips the snap test and causes interpolation to toggle
					// off for one tick per rotation -- visible as flicker once Remix
					// recomputes the skinned vertex hash. Disable interpolation entirely
					// for device machines, which matches the pre-port behaviour and is
					// indistinguishable visually for doors (they're already very fast).
					cur.interpolate = false;
				}

				ObjectID parent = obj_parent(object);
				if (!parent.is_null()) parent_array[i] = parent;
			}

			for (std::size_t i = 0; i < max_size && i < OBJECT_BUFFER_SIZE; i++) {
				if (parent_array[i].is_null()) continue;
				auto& p = current_tick[parent_array[i].index.index];
				p.children[p.children_count++] = i;
			}
		}

		static void before() noexcept
		{
			if (tick_passed) {
				std::swap(current_tick, previous_tick);
				copy_objects();
				tick_passed = false;
			}

			// Resolve the visible-object list lazily, with a real null guard
			// on the raw signature before doing any pointer arithmetic.
			static std::uint32_t** visible_object_count = nullptr;
			static ObjectID**      visible_object_array = nullptr;
			static bool            visible_resolved = false;
			if (!visible_resolved) {
				visible_resolved = true;
				auto* count_sig = get_signature("visible_object_count_sig");
				auto* ptr_sig   = get_signature("visible_object_ptr_sig");
				if (count_sig && ptr_sig) {
					visible_object_count = reinterpret_cast<std::uint32_t**>(count_sig + 3);
					visible_object_array = reinterpret_cast<ObjectID**>(ptr_sig + 3);
				}
			}
			if (!visible_object_count || !visible_object_array) return;

			auto current_count = **visible_object_count;
			auto* arr = *visible_object_array;
			if (!arr) return;
			for (std::size_t i = 0; i < current_count; i++) {
				interpolate_one(arr[i].index.index);
			}
		}

		static void after() noexcept
		{
			// Deliberately do NOT roll the interpolated node/center state back to the
			// tick snapshot here (Chimera's interpolate_object_after restores them).
			//
			// Under RTX Remix, doing the rollback at on_frame breaks motion vectors:
			// Halo issues several draw calls per object per frame (shadow, main, mirror).
			// Remix's InstanceManager::updateInstance() calls RtInstance::move() on the
			// first draw of a frame (which latches prevObjectToWorld := last frame's
			// final transform) and RtInstance::moveAgain() on subsequent draws (which
			// updates only the current transform). If on_frame fires between two of
			// these draws, the second draw observes the rolled-back raw transform and
			// the next frame's motion vector is sampled from raw -> interp instead of
			// interp -> interp, producing visible jitter on every rotating/moving
			// object (cf. dxvk-remix/src/dxvk/rtx_render/rtx_instance_manager.cpp
			// move() / moveAgain()).
			//
			// Leaving the interpolated state in place between frames is safe because
			// Halo's simulation only reads/writes object state from inside the tick
			// hook (which runs *before* our next preframe). copy_objects() therefore
			// still sees fresh raw tick data on the next tick boundary.
			//
			// We still need to clear interpolated_this_frame so interpolate_one() runs
			// again next frame.
			auto& table = ObjectTable::get_object_table();
			auto max_objects = table.current_size;
			for (std::size_t i = 0; i < max_objects && i < OBJECT_BUFFER_SIZE; i++) {
				current_tick[i].interpolated_this_frame = false;
			}
		}

		static void clear() noexcept
		{
			for (std::size_t i = 0; i < OBJECT_BUFFER_SIZE; i++) {
				current_tick[i].interpolate = false;
				current_tick[i].interpolated_this_frame = false;
				current_tick[i].index = 0;
			}
		}

		static void on_tick() noexcept { tick_passed = true; }
	}

	//====================================================================
	// Orchestration (interpolate.cpp).
	//====================================================================

	static float* first_person_camera_tick_rate = nullptr;
	static Hook   g_fp_interp_hook;

	static void on_tick_cb() noexcept
	{
		if (game_paused()) return;
		antenna_sys::on_tick();
		flag_sys::on_tick();
		fp_sys::on_tick();
		light_sys::on_tick();
		// object_sys::on_tick();  // object interpolation disabled (Remix incompatibility: jitter / menu ring flicker)
		camera_sys::on_tick();
		particle_sys::on_tick();
		interpolation_tick_progress = 0.0f;

		float current_rate = effective_tick_rate();
		if (first_person_camera_tick_rate && *first_person_camera_tick_rate != current_rate) {
			overwrite(first_person_camera_tick_rate, current_rate);
		}
	}

	static void on_preframe_cb() noexcept
	{
		if (game_paused()) return;
		interpolation_tick_progress = get_tick_progress();

		antenna_sys::before();
		flag_sys::before();
		light_sys::before();
		// object_sys::before();  // object interpolation disabled
		particle_sys::run();
	}

	static void on_frame_cb() noexcept
	{
		if (game_paused()) return;
		antenna_sys::after();
		// object_sys::after();  // object interpolation disabled
		particle_sys::after();
	}

	static void on_clear_buffers() noexcept
	{
		object_sys::clear();
		particle_sys::clear();
		light_sys::clear();
		flag_sys::clear();
		camera_sys::clear();
		fp_sys::clear();
	}

	} // anonymous namespace


	//====================================================================
	// Public API
	//====================================================================

	bool is_enabled() noexcept { return interpolation_enabled; }

	void set_up_interpolation() noexcept
	{
		if (interpolation_enabled) return;

		shared::common::log("Interpolate", "Setting up animation interpolation ...", shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);

		auto* fp_interp_ptr  = get_signature("fp_interp_sig");
		auto* fp_cam_rate_sig= get_signature("fp_cam_tick_rate_sig");
		auto* cam_interp_sig = get_signature("camera_interpolation_sig");

		if (!fp_interp_ptr || !fp_cam_rate_sig || !cam_interp_sig) {
			shared::common::log("Interpolate",
				"Aborting -- one or more required signatures could not be resolved.",
				shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
			return;
		}

		first_person_camera_tick_rate = *reinterpret_cast<float**>(fp_cam_rate_sig + 2);

		add_tick_event(on_tick_cb);
		add_preframe_event(on_preframe_cb);
		add_frame_event(on_frame_cb);
		add_precamera_event(reinterpret_cast<EventFunction>(camera_sys::before));
		add_camera_event(reinterpret_cast<EventFunction>(camera_sys::after));
		write_jmp_call(fp_interp_ptr, g_fp_interp_hook,
			reinterpret_cast<const void*>(fp_sys::before),
			reinterpret_cast<const void*>(fp_sys::after));

		// Block Halo's built-in fp camera interpolation; we do it ourselves.
		overwrite(cam_interp_sig + 0xF, static_cast<unsigned char>(0xEB));

		add_revert_event(on_clear_buffers);

		interpolation_enabled = true;
		shared::common::log("Interpolate", "Animation interpolation enabled.", shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);
	}

	void disable_interpolation() noexcept
	{
		if (!interpolation_enabled) return;

		g_fp_interp_hook.rollback();
		// camera_interpolation_sig overwrite is a single-byte 74 -> EB swap; restoring
		// would need the original byte. Skipped (matches Chimera's reliance on its
		// Signature::rollback machinery, which we do not replicate).

		remove_tick_event(on_tick_cb);
		remove_preframe_event(on_preframe_cb);
		remove_frame_event(on_frame_cb);
		remove_precamera_event(reinterpret_cast<EventFunction>(camera_sys::before));
		remove_camera_event(reinterpret_cast<EventFunction>(camera_sys::after));
		remove_revert_event(on_clear_buffers);

		interpolation_enabled = false;
	}
}

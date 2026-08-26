#pragma once
#include <format>
#include <type_traits>
#include <utility>

#include <framework/export_defs.h>

#include <DirectXMath.h>
#include <SimpleMath.h>

namespace feather {

#ifdef DOUBLE_PRECISION
using real_t = double;
#else
using real_t = float;
#endif

using Matrix = DirectX::SimpleMath::Matrix;
using Vector2 = DirectX::SimpleMath::Vector2;
using Vector3 = DirectX::SimpleMath::Vector3;
using Vector4 = DirectX::SimpleMath::Vector4;
using Color = DirectX::SimpleMath::Color;
using Quaternion = DirectX::SimpleMath::Quaternion;
using UByteColor = std::array<uint8_t, 4>;

// // TODO: Add better support for math and conversion operations using these types
using Vector4ui = DirectX::XMUINT4;
using Vector3ui = DirectX::XMUINT3;
using Vector2ui = DirectX::XMUINT2;

using Vector4i = DirectX::XMINT4;
using Vector3i = DirectX::XMINT3;
using Vector2i = DirectX::XMINT2;

struct FEATHER_API Vertex {
	Vector3 position;
	Vector3 normal;
	Vector2 uv;

	Vertex() = default;
	constexpr Vertex(Vector3 pos, Vector3 normal, Vector2 uv = Vector2::zero) : position(pos), normal(normal), uv(uv) {}
	constexpr Vertex(real_t px, real_t py, real_t pz, real_t nx, real_t ny, real_t nz, real_t u = 0, real_t v = 0)
			: position(px, py, pz)
			, normal(nx, ny, nz)
			, uv(u, v) {}
	Vertex(const Vertex&) = default;
	Vertex& operator=(const Vertex&) = default;
	Vertex(Vertex&&) = default;
	Vertex& operator=(Vertex&&) = default;

	bool operator==(const Vertex&) const;
};

struct FEATHER_API AABB {
	Vector3 min;
	Vector3 max;

	AABB() = default;
	constexpr AABB(Vector3 min, Vector3 max) : min(min), max(max) {}
	constexpr AABB(real_t min_x, real_t min_y, real_t min_z, real_t max_x, real_t max_y, real_t max_z)
			: min(min_x, min_y, min_z)
			, max(max_x, max_y, max_z) {}
	AABB(const AABB&) = default;
	AABB& operator=(const AABB&) = default;
	AABB(AABB&&) = default;
	AABB& operator=(AABB&&) = default;

	bool operator==(const AABB&) const;

	[[nodiscard]] bool intersects(const AABB& other) const;
	[[nodiscard]] bool intersects(const Vector3& point) const;
};

// Helper Functions
FEATHER_API float deg_to_rad(float degrees);
FEATHER_API float rad_to_deg(float radians);

FEATHER_API Vector3 deg_to_rad(const Vector3& degrees);
FEATHER_API Vector3 rad_to_deg(const Vector3& radians);

FEATHER_API uint32_t round_up_to_next_pow_2(uint32_t x);

template <class T>
	requires std::is_integral_v<T>
bool is_power_of_two(T x) {
	return (x & (x - 1)) == 0;
}

constexpr uint32_t raise_to_next_multiple_of(uint32_t val, uint32_t multiple);

Matrix convert_direction_vector_to_rotation_matrix(Vector3 forward);

inline bool is_power_of_two(int n) {
	if (n == 0) {
		return false;
	}

	return ceil(log2(n)) == floor(log2(n));
}

namespace math::matrices {

inline void remove_scaling(Matrix& m) {
	const float square_su_m0 = (m.m[0][0] * m.m[0][0]) + (m.m[0][1] * m.m[0][1]) + (m.m[0][2] * m.m[0][2]);
	const float square_su_m1 = (m.m[1][0] * m.m[1][0]) + (m.m[1][1] * m.m[1][1]) + (m.m[1][2] * m.m[1][2]);
	const float square_su_m2 = (m.m[2][0] * m.m[2][0]) + (m.m[2][1] * m.m[2][1]) + (m.m[2][2] * m.m[2][2]);
	const float scal_e0 = square_su_m0 >= 0 ? std::sqrt(square_su_m0) : 1;
	const float scal_e1 = square_su_m1 >= 0 ? std::sqrt(square_su_m1) : 1;
	const float scal_e2 = square_su_m2 >= 0 ? std::sqrt(square_su_m2) : 1;
	m.m[0][0] *= scal_e0;
	m.m[0][1] *= scal_e0;
	m.m[0][2] *= scal_e0;
	m.m[1][0] *= scal_e1;
	m.m[1][1] *= scal_e1;
	m.m[1][2] *= scal_e1;
	m.m[2][0] *= scal_e2;
	m.m[2][1] *= scal_e2;
	m.m[2][2] *= scal_e2;
}

inline void set_axis(Matrix& m, uint32_t i, const Vector3& axis) {
	m.m[i][0] = axis.x;
	m.m[i][1] = axis.y;
	m.m[i][2] = axis.z;
}

enum class Axis : uint8_t {
	X = 0,
	Y,
	Z
};

inline Vector3 get_axis(const Matrix& mat, Axis axis) {
	const uint8_t i = std::to_underlying(axis);
	return { mat.m[i][0], mat.m[i][1], mat.m[i][2] };
}

inline Vector3 get_origin(const Matrix& mat) {
	return { mat.m[3][0], mat.m[3][1], mat.m[3][2] };
}

} //namespace math::matrices

inline constexpr float small_number = 1.e-4f;
inline constexpr float quaternion_normalize_threshhold = 0.01f;

} //namespace feather

template <> struct std::formatter<feather::Quaternion> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

	static auto format(const feather::Quaternion& quat, std::format_context& ctx) {
		return std::format_to(ctx.out(), "X={}, Y={}, Z={}, W={}", quat.x, quat.y, quat.z, quat.w);
	}
};

template <> struct std::formatter<feather::Vector4> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

	static auto format(const feather::Vector4& vec, std::format_context& ctx) {
		return std::format_to(ctx.out(), "X={}, Y={}, Z={}, W={}", vec.x, vec.y, vec.z, vec.w);
	}
};

template <> struct std::formatter<feather::Vector3> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

	static auto format(const feather::Vector3& vec, std::format_context& ctx) {
		return std::format_to(ctx.out(), "X={}, Y={}, Z={}", vec.x, vec.y, vec.z);
	}
};

template <> struct std::formatter<feather::Vector2> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

	static auto format(const feather::Vector2& vec, std::format_context& ctx) {
		return std::format_to(ctx.out(), "X={}, Y={}", vec.x, vec.y);
	}
};

template <> struct std::formatter<feather::Color> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

	static auto format(const feather::Color& col, std::format_context& ctx) {
		return std::format_to(ctx.out(), "R={}, G={}, B={}, A={}", col.x, col.y, col.z, col.w);
	}
};
#pragma once

#include "rtm_interop.h"

#include <framework/reflection_macros.h>

#include <cmath>
#include <format>

#ifndef FEATHER_REFLECTION_PARSER
#include "vector3.gen.h"
#endif

namespace feather {

// A 3-component vector, stored as three reals and computed through RTM.
//
// Every operation loads into RTM's register type, works there, and stores back,
// so the arithmetic is the SIMD implementation while the type stays a plain
// struct the engine can reflect, serialize and hand across the C ABI.
struct Vector3f {
	FSTRUCT();

	[[get, set]] float x = 0.0f;
	[[get, set]] float y = 0.0f;
	[[get, set]] float z = 0.0f;

	constexpr Vector3f() = default;
	constexpr Vector3f(float x, float y, float z) : x(x), y(y), z(z) {}
	explicit constexpr Vector3f(float value) : x(value), y(value), z(value) {}

	// RTM's register type, and back. The [w] lane is unused and left at zero.
	[[nodiscard]] rtm::vector4f to_rtm() const { return rtm::vector_set(x, y, z, 0.0f); }
	static Vector3f from_rtm(rtm::vector4f v) {
		return { rtm::vector_get_x(v), rtm::vector_get_y(v), rtm::vector_get_z(v) };
	}

	// ---- Arithmetic --------------------------------------------------------

	Vector3f operator+(const Vector3f& o) const { return from_rtm(rtm::vector_add(to_rtm(), o.to_rtm())); }
	Vector3f operator-(const Vector3f& o) const { return from_rtm(rtm::vector_sub(to_rtm(), o.to_rtm())); }
	Vector3f operator*(const Vector3f& o) const { return from_rtm(rtm::vector_mul(to_rtm(), o.to_rtm())); }
	Vector3f operator/(const Vector3f& o) const { return from_rtm(rtm::vector_div(to_rtm(), o.to_rtm())); }
	Vector3f operator*(float s) const { return from_rtm(rtm::vector_mul(to_rtm(), s)); }
	Vector3f operator/(float s) const { return from_rtm(rtm::vector_div(to_rtm(), rtm::vector_set(s))); }
	Vector3f operator-() const { return from_rtm(rtm::vector_neg(to_rtm())); }
	Vector3f operator+() const { return *this; }

	Vector3f& operator+=(const Vector3f& o) { return *this = *this + o; }
	Vector3f& operator-=(const Vector3f& o) { return *this = *this - o; }
	Vector3f& operator*=(const Vector3f& o) { return *this = *this * o; }
	Vector3f& operator/=(const Vector3f& o) { return *this = *this / o; }
	Vector3f& operator*=(float s) { return *this = *this * s; }
	Vector3f& operator/=(float s) { return *this = *this / s; }

	// ---- Comparison --------------------------------------------------------

	bool operator==(const Vector3f& o) const { return x == o.x && y == o.y && z == o.z; }
	bool operator!=(const Vector3f& o) const { return !(*this == o); }

	// Component-wise within `tolerance`, which is what a comparison of computed
	// vectors usually wants; operator== stays exact.
	[[nodiscard]] bool is_near(const Vector3f& o, float tolerance = 1.e-4f) const {
		return std::abs(x - o.x) <= tolerance && std::abs(y - o.y) <= tolerance && std::abs(z - o.z) <= tolerance;
	}

	// ---- Element access ----------------------------------------------------

	float& operator[](size_t i) { return (&x)[i]; }
	const float& operator[](size_t i) const { return (&x)[i]; }

	// ---- Operations --------------------------------------------------------

	[[nodiscard]] float length() const { return rtm::vector_length3(to_rtm()); }
	[[nodiscard]] float length_squared() const { return rtm::vector_length_squared3(to_rtm()); }
	[[nodiscard]] float dot(const Vector3f& o) const { return rtm::vector_dot3(to_rtm(), o.to_rtm()); }
	[[nodiscard]] Vector3f cross(const Vector3f& o) const {
		return from_rtm(rtm::vector_cross3(to_rtm(), o.to_rtm()));
	}
	[[nodiscard]] Vector3f normalized() const { return from_rtm(rtm::vector_normalize3(to_rtm())); }
	void normalize() { *this = normalized(); }
	[[nodiscard]] float distance(const Vector3f& o) const { return (*this - o).length(); }
	[[nodiscard]] Vector3f min(const Vector3f& o) const { return from_rtm(rtm::vector_min(to_rtm(), o.to_rtm())); }
	[[nodiscard]] Vector3f max(const Vector3f& o) const { return from_rtm(rtm::vector_max(to_rtm(), o.to_rtm())); }
	[[nodiscard]] Vector3f abs() const { return from_rtm(rtm::vector_abs(to_rtm())); }
	[[nodiscard]] Vector3f lerp(const Vector3f& o, float alpha) const {
		return from_rtm(rtm::vector_lerp(to_rtm(), o.to_rtm(), alpha));
	}

	// ---- Constants ---------------------------------------------------------
	// Feather is right-handed: +X right, +Y up, -Z forward.

	static const Vector3f zero;
	static const Vector3f one;
	static const Vector3f right;
	static const Vector3f left;
	static const Vector3f up;
	static const Vector3f down;
	static const Vector3f forward;
	static const Vector3f backward;
};

FEATHER_ASSERT_RTM_LAYOUT(Vector3f, float, 3);

inline Vector3f operator*(float s, const Vector3f& v) {
	return v * s;
}

// The double-precision counterpart. Same shape; kept separate rather than
// templated so both stay reflectable value types.
struct Vector3d {
	FSTRUCT();

	[[get, set]] double x = 0.0;
	[[get, set]] double y = 0.0;
	[[get, set]] double z = 0.0;

	constexpr Vector3d() = default;
	constexpr Vector3d(double x, double y, double z) : x(x), y(y), z(z) {}
	explicit constexpr Vector3d(double value) : x(value), y(value), z(value) {}

	[[nodiscard]] rtm::vector4d to_rtm() const { return rtm::vector_set(x, y, z, 0.0); }
	static Vector3d from_rtm(const rtm::vector4d& v) {
		return { rtm::vector_get_x(v), rtm::vector_get_y(v), rtm::vector_get_z(v) };
	}

	Vector3d operator+(const Vector3d& o) const { return from_rtm(rtm::vector_add(to_rtm(), o.to_rtm())); }
	Vector3d operator-(const Vector3d& o) const { return from_rtm(rtm::vector_sub(to_rtm(), o.to_rtm())); }
	Vector3d operator*(const Vector3d& o) const { return from_rtm(rtm::vector_mul(to_rtm(), o.to_rtm())); }
	Vector3d operator/(const Vector3d& o) const { return from_rtm(rtm::vector_div(to_rtm(), o.to_rtm())); }
	Vector3d operator*(double s) const { return from_rtm(rtm::vector_mul(to_rtm(), s)); }
	Vector3d operator/(double s) const { return from_rtm(rtm::vector_div(to_rtm(), rtm::vector_set(s))); }
	Vector3d operator-() const { return from_rtm(rtm::vector_neg(to_rtm())); }
	Vector3d operator+() const { return *this; }

	Vector3d& operator+=(const Vector3d& o) { return *this = *this + o; }
	Vector3d& operator-=(const Vector3d& o) { return *this = *this - o; }
	Vector3d& operator*=(const Vector3d& o) { return *this = *this * o; }
	Vector3d& operator/=(const Vector3d& o) { return *this = *this / o; }
	Vector3d& operator*=(double s) { return *this = *this * s; }
	Vector3d& operator/=(double s) { return *this = *this / s; }

	bool operator==(const Vector3d& o) const { return x == o.x && y == o.y && z == o.z; }
	bool operator!=(const Vector3d& o) const { return !(*this == o); }
	[[nodiscard]] bool is_near(const Vector3d& o, double tolerance = 1.e-8) const {
		return std::abs(x - o.x) <= tolerance && std::abs(y - o.y) <= tolerance && std::abs(z - o.z) <= tolerance;
	}

	double& operator[](size_t i) { return (&x)[i]; }
	const double& operator[](size_t i) const { return (&x)[i]; }

	[[nodiscard]] double length() const { return rtm::vector_length3(to_rtm()); }
	[[nodiscard]] double length_squared() const { return rtm::vector_length_squared3(to_rtm()); }
	[[nodiscard]] double dot(const Vector3d& o) const { return rtm::vector_dot3(to_rtm(), o.to_rtm()); }
	[[nodiscard]] Vector3d cross(const Vector3d& o) const {
		return from_rtm(rtm::vector_cross3(to_rtm(), o.to_rtm()));
	}
	[[nodiscard]] Vector3d normalized() const { return from_rtm(rtm::vector_normalize3(to_rtm())); }
	void normalize() { *this = normalized(); }
	[[nodiscard]] double distance(const Vector3d& o) const { return (*this - o).length(); }
	[[nodiscard]] Vector3d min(const Vector3d& o) const { return from_rtm(rtm::vector_min(to_rtm(), o.to_rtm())); }
	[[nodiscard]] Vector3d max(const Vector3d& o) const { return from_rtm(rtm::vector_max(to_rtm(), o.to_rtm())); }
	[[nodiscard]] Vector3d abs() const { return from_rtm(rtm::vector_abs(to_rtm())); }
	[[nodiscard]] Vector3d lerp(const Vector3d& o, double alpha) const {
		return from_rtm(rtm::vector_lerp(to_rtm(), o.to_rtm(), alpha));
	}

	static const Vector3d zero;
	static const Vector3d one;
	static const Vector3d right;
	static const Vector3d left;
	static const Vector3d up;
	static const Vector3d down;
	static const Vector3d forward;
	static const Vector3d backward;
};

FEATHER_ASSERT_RTM_LAYOUT(Vector3d, double, 3);

inline Vector3d operator*(double s, const Vector3d& v) {
	return v * s;
}

} //namespace feather

template <> struct std::formatter<feather::Vector3f> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
	static auto format(const feather::Vector3f& v, std::format_context& ctx) {
		return std::format_to(ctx.out(), "X={}, Y={}, Z={}", v.x, v.y, v.z);
	}
};

template <> struct std::formatter<feather::Vector3d> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
	static auto format(const feather::Vector3d& v, std::format_context& ctx) {
		return std::format_to(ctx.out(), "X={}, Y={}, Z={}", v.x, v.y, v.z);
	}
};

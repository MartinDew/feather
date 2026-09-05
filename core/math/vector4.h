#pragma once

#include "rtm_interop.h"

#include <framework/reflection_macros.h>

#include <cmath>
#include <format>

#ifndef FEATHER_REFLECTION_PARSER
#include "vector4.gen.h"
#endif

namespace feather {

// A 4-component vector, stored as 4 reals and computed through RTM.
struct Vector4f {
	FSTRUCT();

	[[get, set]] float x = 0.0f;
	[[get, set]] float y = 0.0f;
	[[get, set]] float z = 0.0f;
	[[get, set]] float w = 0.0f;

	constexpr Vector4f() = default;
	constexpr Vector4f(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
	explicit constexpr Vector4f(float value) : x(value), y(value), z(value), w(value) {}

	[[nodiscard]] rtm::vector4f to_rtm() const { return rtm::vector_set(x, y, z, w); }
	static Vector4f from_rtm(const rtm::vector4f& v) { return { rtm::vector_get_x(v), rtm::vector_get_y(v), rtm::vector_get_z(v), rtm::vector_get_w(v) }; }

	Vector4f operator+(const Vector4f& o) const { return from_rtm(rtm::vector_add(to_rtm(), o.to_rtm())); }
	Vector4f operator-(const Vector4f& o) const { return from_rtm(rtm::vector_sub(to_rtm(), o.to_rtm())); }
	Vector4f operator*(const Vector4f& o) const { return from_rtm(rtm::vector_mul(to_rtm(), o.to_rtm())); }
	Vector4f operator/(const Vector4f& o) const { return from_rtm(rtm::vector_div(to_rtm(), o.to_rtm())); }
	Vector4f operator*(float s) const { return from_rtm(rtm::vector_mul(to_rtm(), s)); }
	Vector4f operator/(float s) const { return from_rtm(rtm::vector_div(to_rtm(), rtm::vector_set(s))); }
	Vector4f operator-() const { return from_rtm(rtm::vector_neg(to_rtm())); }
	Vector4f operator+() const { return *this; }

	Vector4f& operator+=(const Vector4f& o) { return *this = *this + o; }
	Vector4f& operator-=(const Vector4f& o) { return *this = *this - o; }
	Vector4f& operator*=(const Vector4f& o) { return *this = *this * o; }
	Vector4f& operator/=(const Vector4f& o) { return *this = *this / o; }
	Vector4f& operator*=(float s) { return *this = *this * s; }
	Vector4f& operator/=(float s) { return *this = *this / s; }

	bool operator==(const Vector4f& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
	bool operator!=(const Vector4f& o) const { return !(*this == o); }
	// Component-wise within `tolerance`; operator== stays exact.
	[[nodiscard]] bool is_near(const Vector4f& o, float tolerance = 1.e-4f) const { return std::abs(x - o.x) <= tolerance && std::abs(y - o.y) <= tolerance && std::abs(z - o.z) <= tolerance && std::abs(w - o.w) <= tolerance; }

	float& operator[](size_t i) { return (&x)[i]; }
	const float& operator[](size_t i) const { return (&x)[i]; }

	[[nodiscard]] float length() const { return rtm::vector_length(to_rtm()); }
	[[nodiscard]] float length_squared() const { return rtm::vector_length_squared(to_rtm()); }
	[[nodiscard]] float dot(const Vector4f& o) const { return rtm::vector_dot(to_rtm(), o.to_rtm()); }
	[[nodiscard]] Vector4f normalized() const { return from_rtm(rtm::vector_normalize(to_rtm())); }
	void normalize() { *this = normalized(); }
	[[nodiscard]] float distance(const Vector4f& o) const { return (*this - o).length(); }
	[[nodiscard]] Vector4f min(const Vector4f& o) const { return from_rtm(rtm::vector_min(to_rtm(), o.to_rtm())); }
	[[nodiscard]] Vector4f max(const Vector4f& o) const { return from_rtm(rtm::vector_max(to_rtm(), o.to_rtm())); }
	[[nodiscard]] Vector4f abs() const { return from_rtm(rtm::vector_abs(to_rtm())); }
	[[nodiscard]] Vector4f lerp(const Vector4f& o, float alpha) const { return from_rtm(rtm::vector_lerp(to_rtm(), o.to_rtm(), alpha)); }

	static const Vector4f zero;
	static const Vector4f one;
};

FEATHER_ASSERT_RTM_LAYOUT(Vector4f, float, 4);

inline Vector4f operator*(float s, const Vector4f& v) { return v * s; }

// The double-precision counterpart. Kept as its own type rather than templated
// so both stay reflectable value types.
// A 4-component vector, stored as 4 reals and computed through RTM.
struct Vector4d {
	FSTRUCT();

	[[get, set]] double x = 0.0;
	[[get, set]] double y = 0.0;
	[[get, set]] double z = 0.0;
	[[get, set]] double w = 0.0;

	constexpr Vector4d() = default;
	constexpr Vector4d(double x, double y, double z, double w) : x(x), y(y), z(z), w(w) {}
	explicit constexpr Vector4d(double value) : x(value), y(value), z(value), w(value) {}

	[[nodiscard]] rtm::vector4d to_rtm() const { return rtm::vector_set(x, y, z, w); }
	static Vector4d from_rtm(const rtm::vector4d& v) { return { rtm::vector_get_x(v), rtm::vector_get_y(v), rtm::vector_get_z(v), rtm::vector_get_w(v) }; }

	Vector4d operator+(const Vector4d& o) const { return from_rtm(rtm::vector_add(to_rtm(), o.to_rtm())); }
	Vector4d operator-(const Vector4d& o) const { return from_rtm(rtm::vector_sub(to_rtm(), o.to_rtm())); }
	Vector4d operator*(const Vector4d& o) const { return from_rtm(rtm::vector_mul(to_rtm(), o.to_rtm())); }
	Vector4d operator/(const Vector4d& o) const { return from_rtm(rtm::vector_div(to_rtm(), o.to_rtm())); }
	Vector4d operator*(double s) const { return from_rtm(rtm::vector_mul(to_rtm(), s)); }
	Vector4d operator/(double s) const { return from_rtm(rtm::vector_div(to_rtm(), rtm::vector_set(s))); }
	Vector4d operator-() const { return from_rtm(rtm::vector_neg(to_rtm())); }
	Vector4d operator+() const { return *this; }

	Vector4d& operator+=(const Vector4d& o) { return *this = *this + o; }
	Vector4d& operator-=(const Vector4d& o) { return *this = *this - o; }
	Vector4d& operator*=(const Vector4d& o) { return *this = *this * o; }
	Vector4d& operator/=(const Vector4d& o) { return *this = *this / o; }
	Vector4d& operator*=(double s) { return *this = *this * s; }
	Vector4d& operator/=(double s) { return *this = *this / s; }

	bool operator==(const Vector4d& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
	bool operator!=(const Vector4d& o) const { return !(*this == o); }
	// Component-wise within `tolerance`; operator== stays exact.
	[[nodiscard]] bool is_near(const Vector4d& o, double tolerance = 1.e-8) const { return std::abs(x - o.x) <= tolerance && std::abs(y - o.y) <= tolerance && std::abs(z - o.z) <= tolerance && std::abs(w - o.w) <= tolerance; }

	double& operator[](size_t i) { return (&x)[i]; }
	const double& operator[](size_t i) const { return (&x)[i]; }

	[[nodiscard]] double length() const { return rtm::vector_length(to_rtm()); }
	[[nodiscard]] double length_squared() const { return rtm::vector_length_squared(to_rtm()); }
	[[nodiscard]] double dot(const Vector4d& o) const { return rtm::vector_dot(to_rtm(), o.to_rtm()); }
	[[nodiscard]] Vector4d normalized() const { return from_rtm(rtm::vector_normalize(to_rtm())); }
	void normalize() { *this = normalized(); }
	[[nodiscard]] double distance(const Vector4d& o) const { return (*this - o).length(); }
	[[nodiscard]] Vector4d min(const Vector4d& o) const { return from_rtm(rtm::vector_min(to_rtm(), o.to_rtm())); }
	[[nodiscard]] Vector4d max(const Vector4d& o) const { return from_rtm(rtm::vector_max(to_rtm(), o.to_rtm())); }
	[[nodiscard]] Vector4d abs() const { return from_rtm(rtm::vector_abs(to_rtm())); }
	[[nodiscard]] Vector4d lerp(const Vector4d& o, double alpha) const { return from_rtm(rtm::vector_lerp(to_rtm(), o.to_rtm(), alpha)); }

	static const Vector4d zero;
	static const Vector4d one;
};

FEATHER_ASSERT_RTM_LAYOUT(Vector4d, double, 4);

inline Vector4d operator*(double s, const Vector4d& v) { return v * s; }

} //namespace feather

template <> struct std::formatter<feather::Vector4f> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
	static auto format(const feather::Vector4f& v, std::format_context& ctx) {
		return std::format_to(ctx.out(), "X={}, Y={}, Z={}, W={}", v.x, v.y, v.z, v.w);
	}
};

template <> struct std::formatter<feather::Vector4d> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
	static auto format(const feather::Vector4d& v, std::format_context& ctx) {
		return std::format_to(ctx.out(), "X={}, Y={}, Z={}, W={}", v.x, v.y, v.z, v.w);
	}
};

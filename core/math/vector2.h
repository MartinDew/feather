#pragma once

#include "rtm_interop.h"

#include <framework/reflection_macros.h>

#include <cmath>
#include <format>

#ifndef FEATHER_REFLECTION_PARSER
#include "vector2.gen.h"
#endif

namespace feather {

// A 2-component vector, stored as 2 reals and computed through RTM.
struct Vector2f {
	FSTRUCT();

	[[get, set]] float x = 0.0f;
	[[get, set]] float y = 0.0f;

	constexpr Vector2f() = default;
	constexpr Vector2f(float x, float y) : x(x), y(y) {}
	explicit constexpr Vector2f(float value) : x(value), y(value) {}

	[[nodiscard]] rtm::vector4f to_rtm() const { return rtm::vector_set(x, y, 0.0f, 0.0f); }
	static Vector2f from_rtm(const rtm::vector4f& v) { return { rtm::vector_get_x(v), rtm::vector_get_y(v) }; }

	Vector2f operator+(const Vector2f& o) const { return from_rtm(rtm::vector_add(to_rtm(), o.to_rtm())); }
	Vector2f operator-(const Vector2f& o) const { return from_rtm(rtm::vector_sub(to_rtm(), o.to_rtm())); }
	Vector2f operator*(const Vector2f& o) const { return from_rtm(rtm::vector_mul(to_rtm(), o.to_rtm())); }
	Vector2f operator/(const Vector2f& o) const { return from_rtm(rtm::vector_div(to_rtm(), o.to_rtm())); }
	Vector2f operator*(float s) const { return from_rtm(rtm::vector_mul(to_rtm(), s)); }
	Vector2f operator/(float s) const { return from_rtm(rtm::vector_div(to_rtm(), rtm::vector_set(s))); }
	Vector2f operator-() const { return from_rtm(rtm::vector_neg(to_rtm())); }
	Vector2f operator+() const { return *this; }

	Vector2f& operator+=(const Vector2f& o) { return *this = *this + o; }
	Vector2f& operator-=(const Vector2f& o) { return *this = *this - o; }
	Vector2f& operator*=(const Vector2f& o) { return *this = *this * o; }
	Vector2f& operator/=(const Vector2f& o) { return *this = *this / o; }
	Vector2f& operator*=(float s) { return *this = *this * s; }
	Vector2f& operator/=(float s) { return *this = *this / s; }

	bool operator==(const Vector2f& o) const { return x == o.x && y == o.y; }
	bool operator!=(const Vector2f& o) const { return !(*this == o); }
	// Component-wise within `tolerance`; operator== stays exact.
	[[nodiscard]] bool is_near(const Vector2f& o, float tolerance = 1.e-4f) const { return std::abs(x - o.x) <= tolerance && std::abs(y - o.y) <= tolerance; }

	float& operator[](size_t i) { return (&x)[i]; }
	const float& operator[](size_t i) const { return (&x)[i]; }

	[[nodiscard]] float length() const { return rtm::vector_length3(to_rtm()); }
	[[nodiscard]] float length_squared() const { return rtm::vector_length_squared3(to_rtm()); }
	[[nodiscard]] float dot(const Vector2f& o) const { return rtm::vector_dot3(to_rtm(), o.to_rtm()); }
	[[nodiscard]] Vector2f normalized() const { return from_rtm(rtm::vector_normalize3(to_rtm())); }
	void normalize() { *this = normalized(); }
	[[nodiscard]] float distance(const Vector2f& o) const { return (*this - o).length(); }
	[[nodiscard]] Vector2f min(const Vector2f& o) const { return from_rtm(rtm::vector_min(to_rtm(), o.to_rtm())); }
	[[nodiscard]] Vector2f max(const Vector2f& o) const { return from_rtm(rtm::vector_max(to_rtm(), o.to_rtm())); }
	[[nodiscard]] Vector2f abs() const { return from_rtm(rtm::vector_abs(to_rtm())); }
	[[nodiscard]] Vector2f lerp(const Vector2f& o, float alpha) const { return from_rtm(rtm::vector_lerp(to_rtm(), o.to_rtm(), alpha)); }

	static const Vector2f zero;
	static const Vector2f one;
	static const Vector2f right;
	static const Vector2f left;
	static const Vector2f up;
	static const Vector2f down;
};

FEATHER_ASSERT_RTM_LAYOUT(Vector2f, float, 2);

inline Vector2f operator*(float s, const Vector2f& v) { return v * s; }

// The double-precision counterpart. Kept as its own type rather than templated
// so both stay reflectable value types.
// A 2-component vector, stored as 2 reals and computed through RTM.
struct Vector2d {
	FSTRUCT();

	[[get, set]] double x = 0.0;
	[[get, set]] double y = 0.0;

	constexpr Vector2d() = default;
	constexpr Vector2d(double x, double y) : x(x), y(y) {}
	explicit constexpr Vector2d(double value) : x(value), y(value) {}

	[[nodiscard]] rtm::vector4d to_rtm() const { return rtm::vector_set(x, y, 0.0, 0.0); }
	static Vector2d from_rtm(const rtm::vector4d& v) { return { rtm::vector_get_x(v), rtm::vector_get_y(v) }; }

	Vector2d operator+(const Vector2d& o) const { return from_rtm(rtm::vector_add(to_rtm(), o.to_rtm())); }
	Vector2d operator-(const Vector2d& o) const { return from_rtm(rtm::vector_sub(to_rtm(), o.to_rtm())); }
	Vector2d operator*(const Vector2d& o) const { return from_rtm(rtm::vector_mul(to_rtm(), o.to_rtm())); }
	Vector2d operator/(const Vector2d& o) const { return from_rtm(rtm::vector_div(to_rtm(), o.to_rtm())); }
	Vector2d operator*(double s) const { return from_rtm(rtm::vector_mul(to_rtm(), s)); }
	Vector2d operator/(double s) const { return from_rtm(rtm::vector_div(to_rtm(), rtm::vector_set(s))); }
	Vector2d operator-() const { return from_rtm(rtm::vector_neg(to_rtm())); }
	Vector2d operator+() const { return *this; }

	Vector2d& operator+=(const Vector2d& o) { return *this = *this + o; }
	Vector2d& operator-=(const Vector2d& o) { return *this = *this - o; }
	Vector2d& operator*=(const Vector2d& o) { return *this = *this * o; }
	Vector2d& operator/=(const Vector2d& o) { return *this = *this / o; }
	Vector2d& operator*=(double s) { return *this = *this * s; }
	Vector2d& operator/=(double s) { return *this = *this / s; }

	bool operator==(const Vector2d& o) const { return x == o.x && y == o.y; }
	bool operator!=(const Vector2d& o) const { return !(*this == o); }
	// Component-wise within `tolerance`; operator== stays exact.
	[[nodiscard]] bool is_near(const Vector2d& o, double tolerance = 1.e-8) const { return std::abs(x - o.x) <= tolerance && std::abs(y - o.y) <= tolerance; }

	double& operator[](size_t i) { return (&x)[i]; }
	const double& operator[](size_t i) const { return (&x)[i]; }

	[[nodiscard]] double length() const { return rtm::vector_length3(to_rtm()); }
	[[nodiscard]] double length_squared() const { return rtm::vector_length_squared3(to_rtm()); }
	[[nodiscard]] double dot(const Vector2d& o) const { return rtm::vector_dot3(to_rtm(), o.to_rtm()); }
	[[nodiscard]] Vector2d normalized() const { return from_rtm(rtm::vector_normalize3(to_rtm())); }
	void normalize() { *this = normalized(); }
	[[nodiscard]] double distance(const Vector2d& o) const { return (*this - o).length(); }
	[[nodiscard]] Vector2d min(const Vector2d& o) const { return from_rtm(rtm::vector_min(to_rtm(), o.to_rtm())); }
	[[nodiscard]] Vector2d max(const Vector2d& o) const { return from_rtm(rtm::vector_max(to_rtm(), o.to_rtm())); }
	[[nodiscard]] Vector2d abs() const { return from_rtm(rtm::vector_abs(to_rtm())); }
	[[nodiscard]] Vector2d lerp(const Vector2d& o, double alpha) const { return from_rtm(rtm::vector_lerp(to_rtm(), o.to_rtm(), alpha)); }

	static const Vector2d zero;
	static const Vector2d one;
	static const Vector2d right;
	static const Vector2d left;
	static const Vector2d up;
	static const Vector2d down;
};

FEATHER_ASSERT_RTM_LAYOUT(Vector2d, double, 2);

inline Vector2d operator*(double s, const Vector2d& v) { return v * s; }

} //namespace feather

template <> struct std::formatter<feather::Vector2f> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
	static auto format(const feather::Vector2f& v, std::format_context& ctx) {
		return std::format_to(ctx.out(), "X={}, Y={}", v.x, v.y);
	}
};

template <> struct std::formatter<feather::Vector2d> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
	static auto format(const feather::Vector2d& v, std::format_context& ctx) {
		return std::format_to(ctx.out(), "X={}, Y={}", v.x, v.y);
	}
};

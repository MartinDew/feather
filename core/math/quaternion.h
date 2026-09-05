#pragma once

#include "rtm_interop.h"
#include "vector3.h"

#include <framework/reflection_macros.h>

#include <cmath>
#include <format>

#ifndef FEATHER_REFLECTION_PARSER
#include "quaternion.gen.h"
#endif

namespace feather {

// A rotation, stored as four reals and computed through RTM.
//
// Laid out like a 4-component vector, which is what lets it cross the C ABI and
// reach RTM's register type without a conversion step.
struct Quaternionf {
	FSTRUCT();

	[[get, set]] float x = 0.0f;
	[[get, set]] float y = 0.0f;
	[[get, set]] float z = 0.0f;
	[[get, set]] float w = 1.0f;

	constexpr Quaternionf() = default;
	constexpr Quaternionf(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

	[[nodiscard]] rtm::quatf to_rtm() const { return rtm::quat_set(x, y, z, w); }
	static Quaternionf from_rtm(const rtm::quatf& q) {
		return { rtm::quat_get_x(q), rtm::quat_get_y(q), rtm::quat_get_z(q), rtm::quat_get_w(q) };
	}

	// ---- Construction ------------------------------------------------------

	static Quaternionf from_axis_angle(const Vector3f& axis, float radians) {
		return from_rtm(rtm::quat_from_axis_angle(axis.to_rtm(), radians));
	}
	// Angles in radians, applied as yaw (Y), pitch (X), then roll (Z).
	static Quaternionf from_euler(float yaw, float pitch, float roll) {
		return from_rtm(rtm::quat_from_euler(pitch, yaw, roll));
	}

	// ---- Arithmetic --------------------------------------------------------

	// Composition: `a * b` applies b, then a.
	Quaternionf operator*(const Quaternionf& o) const { return from_rtm(rtm::quat_mul(to_rtm(), o.to_rtm())); }
	Quaternionf& operator*=(const Quaternionf& o) { return *this = *this * o; }
	Quaternionf operator-() const { return { -x, -y, -z, -w }; }

	// Rotates a vector by this rotation.
	Vector3f operator*(const Vector3f& v) const {
		return Vector3f::from_rtm(rtm::quat_mul_vector3(v.to_rtm(), to_rtm()));
	}

	// ---- Comparison --------------------------------------------------------

	bool operator==(const Quaternionf& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
	bool operator!=(const Quaternionf& o) const { return !(*this == o); }
	[[nodiscard]] bool is_near(const Quaternionf& o, float tolerance = 1.e-4f) const {
		return rtm::quat_near_equal(to_rtm(), o.to_rtm(), tolerance);
	}

	// ---- Operations --------------------------------------------------------

	[[nodiscard]] float length() const { return rtm::quat_length(to_rtm()); }
	[[nodiscard]] float length_squared() const { return rtm::quat_length_squared(to_rtm()); }
	[[nodiscard]] float dot(const Quaternionf& o) const { return rtm::quat_dot(to_rtm(), o.to_rtm()); }
	[[nodiscard]] Quaternionf normalized() const { return from_rtm(rtm::quat_normalize(to_rtm())); }
	void normalize() { *this = normalized(); }
	// The inverse of a unit rotation; conjugate is the cheap form of it.
	[[nodiscard]] Quaternionf conjugate() const { return from_rtm(rtm::quat_conjugate(to_rtm())); }
	[[nodiscard]] Quaternionf inverse() const { return normalized().conjugate(); }
	[[nodiscard]] bool is_normalized(float threshold = 1.e-4f) const {
		return rtm::quat_is_normalized(to_rtm(), threshold);
	}
	[[nodiscard]] Vector3f axis() const { return Vector3f::from_rtm(rtm::quat_get_axis(to_rtm())); }
	[[nodiscard]] float angle() const { return rtm::quat_get_angle(to_rtm()); }
	[[nodiscard]] Quaternionf lerp(const Quaternionf& o, float alpha) const {
		return from_rtm(rtm::quat_lerp(to_rtm(), o.to_rtm(), alpha));
	}
	[[nodiscard]] Quaternionf slerp(const Quaternionf& o, float alpha) const {
		return from_rtm(rtm::quat_slerp(to_rtm(), o.to_rtm(), alpha));
	}

	static const Quaternionf identity;
};

FEATHER_ASSERT_RTM_LAYOUT(Quaternionf, float, 4);

// The double-precision counterpart.
struct Quaterniond {
	FSTRUCT();

	[[get, set]] double x = 0.0;
	[[get, set]] double y = 0.0;
	[[get, set]] double z = 0.0;
	[[get, set]] double w = 1.0;

	constexpr Quaterniond() = default;
	constexpr Quaterniond(double x, double y, double z, double w) : x(x), y(y), z(z), w(w) {}

	[[nodiscard]] rtm::quatd to_rtm() const { return rtm::quat_set(x, y, z, w); }
	static Quaterniond from_rtm(const rtm::quatd& q) {
		return { rtm::quat_get_x(q), rtm::quat_get_y(q), rtm::quat_get_z(q), rtm::quat_get_w(q) };
	}

	static Quaterniond from_axis_angle(const Vector3d& axis, double radians) {
		return from_rtm(rtm::quat_from_axis_angle(axis.to_rtm(), radians));
	}
	static Quaterniond from_euler(double yaw, double pitch, double roll) {
		return from_rtm(rtm::quat_from_euler(pitch, yaw, roll));
	}

	Quaterniond operator*(const Quaterniond& o) const { return from_rtm(rtm::quat_mul(to_rtm(), o.to_rtm())); }
	Quaterniond& operator*=(const Quaterniond& o) { return *this = *this * o; }
	Quaterniond operator-() const { return { -x, -y, -z, -w }; }
	Vector3d operator*(const Vector3d& v) const {
		return Vector3d::from_rtm(rtm::quat_mul_vector3(v.to_rtm(), to_rtm()));
	}

	bool operator==(const Quaterniond& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
	bool operator!=(const Quaterniond& o) const { return !(*this == o); }
	[[nodiscard]] bool is_near(const Quaterniond& o, double tolerance = 1.e-8) const {
		return rtm::quat_near_equal(to_rtm(), o.to_rtm(), tolerance);
	}

	[[nodiscard]] double length() const { return rtm::quat_length(to_rtm()); }
	[[nodiscard]] double length_squared() const { return rtm::quat_length_squared(to_rtm()); }
	[[nodiscard]] double dot(const Quaterniond& o) const { return rtm::quat_dot(to_rtm(), o.to_rtm()); }
	[[nodiscard]] Quaterniond normalized() const { return from_rtm(rtm::quat_normalize(to_rtm())); }
	void normalize() { *this = normalized(); }
	[[nodiscard]] Quaterniond conjugate() const { return from_rtm(rtm::quat_conjugate(to_rtm())); }
	[[nodiscard]] Quaterniond inverse() const { return normalized().conjugate(); }
	[[nodiscard]] bool is_normalized(double threshold = 1.e-8) const {
		return rtm::quat_is_normalized(to_rtm(), threshold);
	}
	[[nodiscard]] Vector3d axis() const { return Vector3d::from_rtm(rtm::quat_get_axis(to_rtm())); }
	[[nodiscard]] double angle() const { return rtm::quat_get_angle(to_rtm()); }
	[[nodiscard]] Quaterniond lerp(const Quaterniond& o, double alpha) const {
		return from_rtm(rtm::quat_lerp(to_rtm(), o.to_rtm(), alpha));
	}
	[[nodiscard]] Quaterniond slerp(const Quaterniond& o, double alpha) const {
		return from_rtm(rtm::quat_slerp(to_rtm(), o.to_rtm(), alpha));
	}

	static const Quaterniond identity;
};

FEATHER_ASSERT_RTM_LAYOUT(Quaterniond, double, 4);

} //namespace feather

template <> struct std::formatter<feather::Quaternionf> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
	static auto format(const feather::Quaternionf& q, std::format_context& ctx) {
		return std::format_to(ctx.out(), "X={}, Y={}, Z={}, W={}", q.x, q.y, q.z, q.w);
	}
};

template <> struct std::formatter<feather::Quaterniond> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
	static auto format(const feather::Quaterniond& q, std::format_context& ctx) {
		return std::format_to(ctx.out(), "X={}, Y={}, Z={}, W={}", q.x, q.y, q.z, q.w);
	}
};

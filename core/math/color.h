#pragma once

#include "rtm_interop.h"

#include <framework/reflection_macros.h>

#include <algorithm>
#include <cstdint>
#include <format>

#ifndef FEATHER_REFLECTION_PARSER
#include "color.gen.h"
#endif

namespace feather {

// A linear RGBA color, stored as four floats.
//
// Float only, deliberately: a color is a display quantity, and doubling its
// precision buys nothing a renderer can use. It is laid out like a 4-component
// vector so it reaches RTM's register type for free.
struct Colorf {
	FSTRUCT();

	[[get, set]] float r = 0.0f;
	[[get, set]] float g = 0.0f;
	[[get, set]] float b = 0.0f;
	[[get, set]] float a = 1.0f;

	constexpr Colorf() = default;
	constexpr Colorf(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

	[[nodiscard]] rtm::vector4f to_rtm() const { return rtm::vector_set(r, g, b, a); }
	static Colorf from_rtm(const rtm::vector4f& v) {
		return { rtm::vector_get_x(v), rtm::vector_get_y(v), rtm::vector_get_z(v), rtm::vector_get_w(v) };
	}

	// 0-255 per channel, in the order a texture or a UI usually gives them.
	static constexpr Colorf from_bytes(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
		return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
	}

	Colorf operator+(const Colorf& o) const { return from_rtm(rtm::vector_add(to_rtm(), o.to_rtm())); }
	Colorf operator-(const Colorf& o) const { return from_rtm(rtm::vector_sub(to_rtm(), o.to_rtm())); }
	Colorf operator*(const Colorf& o) const { return from_rtm(rtm::vector_mul(to_rtm(), o.to_rtm())); }
	Colorf operator*(float s) const { return from_rtm(rtm::vector_mul(to_rtm(), s)); }
	Colorf operator/(float s) const { return from_rtm(rtm::vector_div(to_rtm(), rtm::vector_set(s))); }

	Colorf& operator+=(const Colorf& o) { return *this = *this + o; }
	Colorf& operator-=(const Colorf& o) { return *this = *this - o; }
	Colorf& operator*=(const Colorf& o) { return *this = *this * o; }
	Colorf& operator*=(float s) { return *this = *this * s; }
	Colorf& operator/=(float s) { return *this = *this / s; }

	bool operator==(const Colorf& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
	bool operator!=(const Colorf& o) const { return !(*this == o); }
	[[nodiscard]] bool is_near(const Colorf& o, float tolerance = 1.e-4f) const {
		return rtm::vector_all_near_equal(to_rtm(), o.to_rtm(), tolerance);
	}

	float& operator[](size_t i) { return (&r)[i]; }
	const float& operator[](size_t i) const { return (&r)[i]; }

	// Clamped to the unit range, which is what a renderer expects of a color
	// that has been through arithmetic.
	[[nodiscard]] Colorf saturated() const {
		return { std::clamp(r, 0.0f, 1.0f),
				 std::clamp(g, 0.0f, 1.0f),
				 std::clamp(b, 0.0f, 1.0f),
				 std::clamp(a, 0.0f, 1.0f) };
	}
	[[nodiscard]] Colorf lerp(const Colorf& o, float alpha) const {
		return from_rtm(rtm::vector_lerp(to_rtm(), o.to_rtm(), alpha));
	}
	[[nodiscard]] Colorf with_alpha(float alpha) const { return { r, g, b, alpha }; }

	static const Colorf black;
	static const Colorf white;
	static const Colorf red;
	static const Colorf green;
	static const Colorf blue;
	static const Colorf transparent;
};

FEATHER_ASSERT_RTM_LAYOUT(Colorf, float, 4);

inline Colorf operator*(float s, const Colorf& c) {
	return c * s;
}

} //namespace feather

template <> struct std::formatter<feather::Colorf> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
	static auto format(const feather::Colorf& c, std::format_context& ctx) {
		return std::format_to(ctx.out(), "R={}, G={}, B={}, A={}", c.r, c.g, c.b, c.a);
	}
};

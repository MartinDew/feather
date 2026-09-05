#pragma once

#include <rtm/matrix3x4d.h>
#include <rtm/matrix3x4f.h>
#include <rtm/matrix4x4d.h>
#include <rtm/matrix4x4f.h>
#include <rtm/quatd.h>
#include <rtm/quatf.h>
#include <rtm/scalard.h>
#include <rtm/scalarf.h>
#include <rtm/types.h>
#include <rtm/vector4d.h>
#include <rtm/vector4f.h>

#include <cstddef>
#include <type_traits>

// How Feather's math types reach Realtime Math.
//
// RTM computes on register types (rtm::vector4f is __m128, not a struct), and
// stores through plain structs (rtm::float3f). Feather's types declare the same
// fields those storage structs do rather than deriving from them: a derived type
// could not carry [[get]]/[[set]] on members it inherits, and reflection would
// record a base class that is not part of the engine. Layout is asserted equal
// instead, which is what makes the conversions below free.
namespace feather::math {

// The register type each precision computes in.
template <class TReal>
struct RtmTraits;

template <>
struct RtmTraits<float> {
	using Vector = rtm::vector4f;
	using Quat = rtm::quatf;
	using Matrix = rtm::matrix4x4f;
	using Scalar = float;

	static Vector set(float x, float y, float z, float w) { return rtm::vector_set(x, y, z, w); }
	static float get_x(Vector v) { return rtm::vector_get_x(v); }
	static float get_y(Vector v) { return rtm::vector_get_y(v); }
	static float get_z(Vector v) { return rtm::vector_get_z(v); }
	static float get_w(Vector v) { return rtm::vector_get_w(v); }
};

template <>
struct RtmTraits<double> {
	using Vector = rtm::vector4d;
	using Quat = rtm::quatd;
	using Matrix = rtm::matrix4x4d;
	using Scalar = double;

	static Vector set(double x, double y, double z, double w) { return rtm::vector_set(x, y, z, w); }
	static double get_x(Vector v) { return rtm::vector_get_x(v); }
	static double get_y(Vector v) { return rtm::vector_get_y(v); }
	static double get_z(Vector v) { return rtm::vector_get_z(v); }
	static double get_w(Vector v) { return rtm::vector_get_w(v); }
};

// The storage struct RTM would use for the same fields, so a Feather type can
// assert it matches one and hand its address straight to RTM.
template <class TReal, size_t TCount>
struct RtmStorage;

template <> struct RtmStorage<float, 2> { using Type = rtm::float2f; };
template <> struct RtmStorage<float, 3> { using Type = rtm::float3f; };
template <> struct RtmStorage<float, 4> { using Type = rtm::float4f; };
template <> struct RtmStorage<double, 2> { using Type = rtm::float2d; };
template <> struct RtmStorage<double, 3> { using Type = rtm::float3d; };
template <> struct RtmStorage<double, 4> { using Type = rtm::float4d; };

// Asserts a Feather math type is laid out exactly like the RTM storage struct
// it mirrors. Placed in each type's header, right after the declaration.
#define FEATHER_ASSERT_RTM_LAYOUT(feather_type, real_type, count)                                                      \
	static_assert(                                                                                                     \
			sizeof(feather_type) == sizeof(typename ::feather::math::RtmStorage<real_type, count>::Type),               \
			#feather_type " must be the size of the RTM storage struct it mirrors"                                      \
	);                                                                                                                 \
	static_assert(                                                                                                     \
			alignof(feather_type) == alignof(typename ::feather::math::RtmStorage<real_type, count>::Type),             \
			#feather_type " must be aligned like the RTM storage struct it mirrors"                                     \
	);                                                                                                                 \
	static_assert(std::is_standard_layout_v<feather_type>, #feather_type " must stay standard-layout")

} //namespace feather::math

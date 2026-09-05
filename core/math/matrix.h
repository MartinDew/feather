#pragma once

#include "quaternion.h"
#include "rtm_interop.h"
#include "vector3.h"
#include "vector4.h"

#include <framework/reflection_macros.h>

#include <format>

#ifndef FEATHER_REFLECTION_PARSER
#include "matrix.gen.h"
#endif

namespace feather {

// A 4x4 transformation matrix, stored row-major as four rows.
//
// Unlike the vector types this holds RTM's own register rows rather than plain
// reals: a matrix is only ever computed with, never inspected field by field, so
// there is nothing to gain from unpacking it and a great deal of loading and
// storing to lose. It is therefore 16-byte aligned, which the ECS is told about
// through the alignment its ClassInfo carries.
struct Matrix4x4f {
	FSTRUCT();

	Vector4f x_axis { 1.0f, 0.0f, 0.0f, 0.0f };
	Vector4f y_axis { 0.0f, 1.0f, 0.0f, 0.0f };
	Vector4f z_axis { 0.0f, 0.0f, 1.0f, 0.0f };
	Vector4f w_axis { 0.0f, 0.0f, 0.0f, 1.0f };

	constexpr Matrix4x4f() = default;
	constexpr Matrix4x4f(const Vector4f& x, const Vector4f& y, const Vector4f& z, const Vector4f& w) :
		x_axis(x), y_axis(y), z_axis(z), w_axis(w) {}

	[[nodiscard]] rtm::matrix4x4f to_rtm() const {
		return rtm::matrix_set(x_axis.to_rtm(), y_axis.to_rtm(), z_axis.to_rtm(), w_axis.to_rtm());
	}
	static Matrix4x4f from_rtm(const rtm::matrix4x4f& m) {
		return { Vector4f::from_rtm(m.x_axis),
				 Vector4f::from_rtm(m.y_axis),
				 Vector4f::from_rtm(m.z_axis),
				 Vector4f::from_rtm(m.w_axis) };
	}

	// ---- Construction ------------------------------------------------------

	static Matrix4x4f identity_matrix() { return {}; }
	static Matrix4x4f from_translation(const Vector3f& t) {
		Matrix4x4f m;
		m.w_axis = { t.x, t.y, t.z, 1.0f };
		return m;
	}
	static Matrix4x4f from_rotation(const Quaternionf& r) {
		const rtm::matrix3x4f m = rtm::matrix_from_quat(r.to_rtm());
		return from_rtm(rtm::matrix_cast(m));
	}
	static Matrix4x4f from_scale(const Vector3f& s) {
		Matrix4x4f m;
		m.x_axis = { s.x, 0.0f, 0.0f, 0.0f };
		m.y_axis = { 0.0f, s.y, 0.0f, 0.0f };
		m.z_axis = { 0.0f, 0.0f, s.z, 0.0f };
		return m;
	}
	// Scale, then rotate, then translate -- the order a transform composes in.
	static Matrix4x4f from_transform(const Vector3f& translation, const Quaternionf& rotation, const Vector3f& scale) {
		const rtm::matrix3x4f m = rtm::matrix_from_qvv(rotation.to_rtm(), translation.to_rtm(), scale.to_rtm());
		return from_rtm(rtm::matrix_cast(m));
	}

	// ---- Arithmetic --------------------------------------------------------

	Matrix4x4f operator*(const Matrix4x4f& o) const { return from_rtm(rtm::matrix_mul(to_rtm(), o.to_rtm())); }
	Matrix4x4f& operator*=(const Matrix4x4f& o) { return *this = *this * o; }

	// Transforms a point: the translation row applies.
	[[nodiscard]] Vector3f transform_point(const Vector3f& p) const {
		const rtm::vector4f v = rtm::vector_set(p.x, p.y, p.z, 1.0f);
		return Vector3f::from_rtm(rtm::matrix_mul_vector(v, to_rtm()));
	}
	// Transforms a direction: the translation row does not apply.
	[[nodiscard]] Vector3f transform_direction(const Vector3f& d) const {
		const rtm::vector4f v = rtm::vector_set(d.x, d.y, d.z, 0.0f);
		return Vector3f::from_rtm(rtm::matrix_mul_vector(v, to_rtm()));
	}
	Vector4f operator*(const Vector4f& v) const {
		return Vector4f::from_rtm(rtm::matrix_mul_vector(v.to_rtm(), to_rtm()));
	}

	// ---- Comparison --------------------------------------------------------

	bool operator==(const Matrix4x4f& o) const {
		return x_axis == o.x_axis && y_axis == o.y_axis && z_axis == o.z_axis && w_axis == o.w_axis;
	}
	bool operator!=(const Matrix4x4f& o) const { return !(*this == o); }
	[[nodiscard]] bool is_near(const Matrix4x4f& o, float tolerance = 1.e-4f) const {
		return x_axis.is_near(o.x_axis, tolerance) && y_axis.is_near(o.y_axis, tolerance) &&
			   z_axis.is_near(o.z_axis, tolerance) && w_axis.is_near(o.w_axis, tolerance);
	}

	// ---- Operations --------------------------------------------------------

	[[nodiscard]] Matrix4x4f transposed() const { return from_rtm(rtm::matrix_transpose(to_rtm())); }
	[[nodiscard]] Matrix4x4f inverse() const { return from_rtm(rtm::matrix_inverse(to_rtm())); }
	[[nodiscard]] float determinant() const { return rtm::scalar_cast(rtm::matrix_determinant(to_rtm())); }

	[[nodiscard]] Vector3f translation() const { return { w_axis.x, w_axis.y, w_axis.z }; }
	void set_translation(const Vector3f& t) { w_axis = { t.x, t.y, t.z, w_axis.w }; }

	// Row `i`, which is also axis `i` for the first three.
	Vector4f& operator[](size_t i) { return (&x_axis)[i]; }
	const Vector4f& operator[](size_t i) const { return (&x_axis)[i]; }

	static const Matrix4x4f identity;
};

static_assert(sizeof(Matrix4x4f) == 4 * sizeof(Vector4f), "Matrix4x4f must be exactly its four rows");
static_assert(std::is_standard_layout_v<Matrix4x4f>, "Matrix4x4f must stay standard-layout");

} //namespace feather

template <> struct std::formatter<feather::Matrix4x4f> {
	static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
	static auto format(const feather::Matrix4x4f& m, std::format_context& ctx) {
		return std::format_to(
				ctx.out(), "[{} | {} | {} | {}]",
				std::format("{}", m.x_axis), std::format("{}", m.y_axis),
				std::format("{}", m.z_axis), std::format("{}", m.w_axis)
		);
	}
};

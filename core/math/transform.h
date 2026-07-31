#pragma once

#include "math_defs.h"
#include <framework/reflection_macros.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "transform.gen.h"
#endif

namespace feather {

struct Transform {
	FSTRUCT(Component);

	// Feather uses right-handed coordinates
	// up = Y
	// right = X
	// forward = -Z
	[[get, set]] Vector3 position = Vector3::zero; // X, Y, Z
	[[get, set]] Quaternion rotation = Quaternion::identity; // Quaternion
	[[get, set]] Vector3 scale = Vector3::one; // X, Y, Z

	Transform() = default;
	explicit Transform(const Matrix& transformationMat);
	Transform(const Vector3& position, const Quaternion& rotation, const Vector3& scale);
	static Transform construct_from_matrices_and_scale(const Matrix& mat1, const Matrix& mat2, Vector3 desiredScale);

	static Transform multiply(const Transform& a, const Transform& b);

	Transform get_relative_transform(const Transform& other) const;
	Transform get_relative_transform_reverse(const Transform& other) const;
	void set_to_relative_transform(const Transform& parent);

	void translate(const Vector3& translation) noexcept { position += translation; }
	void apply_scaling(const Vector3& scaling) noexcept { scale *= scaling; }

	Vector3 get_forward_vector() const noexcept;
	Vector3 get_up_vector() const noexcept;
	Vector3 get_right_vector() const noexcept;

	Transform inverse() const noexcept;

	void look_at(const Vector3& eye, const Vector3& target, const Vector3& up);
	void look_at(const Vector3& target, const Vector3& up);

	void look_towards(const Vector3& direction, const Vector3& up);

	void rotate(const int dx, const int dy);
	void rotate(const Quaternion& rotation) noexcept;
	void rotate_to(const Vector3& eulerAngles);

	bool is_rotation_normalized() const;

	// Better when the three are needed { up, right, forward }
	std::tuple<Vector3, Vector3, Vector3> get_axis() const noexcept;

	static Transform create_look_at(const Vector3& eye, const Vector3& target, const Vector3& up);

	bool operator==(const Transform&) const;
	Transform operator*(const Transform& other) const { return multiply(*this, other); }

	Transform& operator*=(const Transform& other) {
		*this = multiply(*this, other);
		return *this;
	}

	// Matrices
	Matrix to_matrix_with_scale() const noexcept;
	Matrix to_matrix_no_scale() const noexcept;
	static Transform multiply_using_matrix_with_scale(const Transform& a, const Transform& b);
	static Transform get_relative_transform_using_matrix_with_scale(const Transform& base, const Transform& relative);
};

Transform make_transform_screen_space_sized_billboard(Transform objectTransform,
													  Vector3 cameraPosition,
													  float fovDeg,
													  Vector2 minScreenSpaceSize,
													  Vector2 screenSize);
Transform make_transform_billboard(Transform objectTransform, Vector3 cameraPosition);

} //namespace feather
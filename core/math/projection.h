#pragma once

#include "math_defs.h"
#include <framework/export_defs.h>

namespace feather {

enum class ProjectionType : uint8_t {
	Perspective,
	Orthographic
};

class FEATHER_API Projection {
	Matrix _projection_matrix = Matrix::identity;
	ProjectionType _type = ProjectionType::Perspective;

	// Common parameters
	float _near_plane = 0.1f;
	float _far_plane = 1000.0f;
	float _aspect_ratio = 16.0f / 9.0f;

	// Perspective parameters
	float _fov_y = 1.047f;

	// Orthographic parameters
	float _width = 10.0f;
	float _height = 10.0f;
	// Off-center parameters (for both types)
	bool _is_off_center = false;
	float _left = 0.0f;
	float _right = 0.0f;
	float _bottom = 0.0f;
	float _top = 0.0f;

	bool _is_reverse_z = false;

	void _rebuild_perspective();
	void _rebuild_orthographic();

public:
	Projection() = default;

	// Perspective projection constructors
	static Projection create_perspective(float fov_y_radians, float aspect_ratio, float near_plane, float far_plane);
	static Projection
	create_perspective_fov(float fov_y_degrees, float aspect_ratio, float near_plane, float far_plane);
	static Projection
	create_perspective_off_center(float left, float right, float bottom, float top, float near_plane, float far_plane);

	// Orthographic projection constructors
	static Projection create_orthographic(float width, float height, float near_plane, float far_plane);
	static Projection
	create_orthographic_off_center(float left, float right, float bottom, float top, float near_plane, float far_plane);

	// Getters
	const Matrix& get_matrix() const noexcept { return _projection_matrix; }
	ProjectionType get_type() const noexcept { return _type; }

	float get_near_plane() const noexcept { return _near_plane; }
	float get_far_plane() const noexcept { return _far_plane; }
	float get_aspect_ratio() const noexcept { return _aspect_ratio; }

	// Perspective-specific getters
	float get_fov_y() const noexcept { return _fov_y; }
	float get_fov_x() const noexcept;

	// Orthographic-specific getters
	float get_width() const noexcept { return _width; }
	float get_height() const noexcept { return _height; }

	// Setters that rebuild the projection matrix
	void set_aspect_ratio(float aspect_ratio);
	void set_near_far_planes(float near_plane, float far_plane);
	void set_fov_y(float fov_y_radians);
	void set_fov_y_degrees(float fov_y_degrees);
	void set_orthographic_size(float width, float height);

	// Utility functions
	Vector3 project_point(const Vector3& world_point, const Matrix& view_matrix) const;
	Vector3 unproject_point(const Vector3& screen_point, const Matrix& view_matrix, const Vector2& viewport_size) const;

	// Get frustum corners in view space
	std::array<Vector3, 8> get_frustum_corners() const;

	// Check if this is a reverse-Z projection (far = 0, near = 1)
	bool is_reverse_z() const noexcept { return _is_reverse_z; }

	// Create a reverse-Z version (useful for better depth precision)
	Projection create_reverse_z() const;

	bool operator==(const Projection& other) const;
};

} // namespace feather
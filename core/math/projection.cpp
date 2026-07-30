#include "projection.h"
#include "DirectXMath.h"
#include "math_defs.h"

#include <array>
#include <numbers>

namespace feather {

// Perspective projection constructors
Projection Projection::create_perspective(float fov_y_radians, float aspect_ratio, float near_plane, float far_plane) {
	Projection proj;
	proj._type = ProjectionType::Perspective;
	proj._fov_y = fov_y_radians;
	proj._aspect_ratio = aspect_ratio;
	proj._near_plane = near_plane;
	proj._far_plane = far_plane;
	proj._is_off_center = false;
	proj._rebuild_perspective();
	return proj;
}

Projection
Projection::create_perspective_fov(float fov_y_degrees, float aspect_ratio, float near_plane, float far_plane) {
	return create_perspective(deg_to_rad(fov_y_degrees), aspect_ratio, near_plane, far_plane);
}

Projection Projection::create_perspective_off_center(float left,
													 float right,
													 float bottom,
													 float top,
													 float near_plane,
													 float far_plane) {
	Projection proj;
	proj._type = ProjectionType::Perspective;
	proj._near_plane = near_plane;
	proj._far_plane = far_plane;
	proj._is_off_center = true;
	proj._left = left;
	proj._right = right;
	proj._bottom = bottom;
	proj._top = top;
	proj._aspect_ratio = (right - left) / (top - bottom);
	proj._rebuild_perspective();
	return proj;
}

// Orthographic projection constructors
Projection Projection::create_orthographic(float width, float height, float near_plane, float far_plane) {
	Projection proj;
	proj._type = ProjectionType::Orthographic;
	proj._width = width;
	proj._height = height;
	proj._near_plane = near_plane;
	proj._far_plane = far_plane;
	proj._aspect_ratio = width / height;
	proj._is_off_center = false;
	proj._rebuild_orthographic();
	return proj;
}

Projection Projection::create_orthographic_off_center(float left,
													  float right,
													  float bottom,
													  float top,
													  float near_plane,
													  float far_plane) {
	Projection proj;
	proj._type = ProjectionType::Orthographic;
	proj._near_plane = near_plane;
	proj._far_plane = far_plane;
	proj._is_off_center = true;
	proj._left = left;
	proj._right = right;
	proj._bottom = bottom;
	proj._top = top;
	proj._width = right - left;
	proj._height = top - bottom;
	proj._aspect_ratio = proj._width / proj._height;
	proj._rebuild_orthographic();
	return proj;
}

// Getters
float Projection::get_fov_x() const noexcept {
	if (_type != ProjectionType::Perspective)
		return 0.0f;
	return 2.0f * std::atan(std::tan(_fov_y * 0.5f) * _aspect_ratio);
}

// Setters
void Projection::set_aspect_ratio(float aspect_ratio) {
	_aspect_ratio = aspect_ratio;
	if (_type == ProjectionType::Perspective) {
		_rebuild_perspective();
	}
	else {
		_height = _width / _aspect_ratio;
		_rebuild_orthographic();
	}
}

void Projection::set_near_far_planes(float near_plane, float far_plane) {
	_near_plane = near_plane;
	_far_plane = far_plane;
	if (_type == ProjectionType::Perspective) {
		_rebuild_perspective();
	}
	else {
		_rebuild_orthographic();
	}
}

void Projection::set_fov_y(float fov_y_radians) {
	if (_type != ProjectionType::Perspective)
		return;
	_fov_y = fov_y_radians;
	_rebuild_perspective();
}

void Projection::set_fov_y_degrees(float fov_y_degrees) {
	set_fov_y(deg_to_rad(fov_y_degrees));
}

void Projection::set_orthographic_size(float width, float height) {
	if (_type != ProjectionType::Orthographic)
		return;
	_width = width;
	_height = height;
	_aspect_ratio = width / height;
	_rebuild_orthographic();
}

// Private rebuild functions
void Projection::_rebuild_perspective() {
	if (_is_off_center) {
		_projection_matrix =
				Matrix::create_perspective_off_center(_left, _right, _bottom, _top, _near_plane, _far_plane);
	}
	else {
		_projection_matrix = Matrix::create_perspective_field_of_view(_fov_y, _aspect_ratio, _near_plane, _far_plane);
	}
}

void Projection::_rebuild_orthographic() {
	if (_is_off_center) {
		_projection_matrix =
				Matrix::create_orthographic_off_center(_left, _right, _bottom, _top, _near_plane, _far_plane);
	}
	else {
		_projection_matrix = Matrix::create_orthographic(_width, _height, _near_plane, _far_plane);
	}
}

// Utility functions
Vector3 Projection::project_point(const Vector3& world_point, const Matrix& view_matrix) const {
	Matrix view_proj = view_matrix * _projection_matrix;
	Vector4 clip_space = Vector4::transform(Vector4(world_point.x, world_point.y, world_point.z, 1.0f), view_proj);

	// Perspective divide
	if (std::abs(clip_space.w) > 0.0001f) {
		clip_space.x /= clip_space.w;
		clip_space.y /= clip_space.w;
		clip_space.z /= clip_space.w;
	}

	return Vector3(clip_space.x, clip_space.y, clip_space.z);
}

Vector3 Projection::unproject_point(const Vector3& screen_point,
									const Matrix& view_matrix,
									const Vector2& viewport_size) const {
	// Convert screen coordinates to NDC (-1 to 1)
	Vector3 ndc;
	ndc.x = (screen_point.x / viewport_size.x) * 2.0f - 1.0f;
	ndc.y = 1.0f - (screen_point.y / viewport_size.y) * 2.0f; // Flip Y
	ndc.z = screen_point.z;

	// Create inverse view-projection matrix
	Matrix view_proj = view_matrix * _projection_matrix;
	Matrix inv_view_proj = view_proj.invert();

	// Transform from NDC to world space
	Vector4 world_point = Vector4::transform(Vector4(ndc.x, ndc.y, ndc.z, 1.0f), inv_view_proj);

	// Perspective divide
	if (std::abs(world_point.w) > 0.0001f) {
		world_point.x /= world_point.w;
		world_point.y /= world_point.w;
		world_point.z /= world_point.w;
	}

	return Vector3(world_point.x, world_point.y, world_point.z);
}

std::array<Vector3, 8> Projection::get_frustum_corners() const {
	std::array<Vector3, 8> corners;
	Matrix inv_proj = _projection_matrix.invert();

	// NDC corners of the frustum
	const Vector3 ndc_corners[8] = {
		// Near plane
		Vector3(-1.0f, -1.0f, 0.0f), // bottom-left
		Vector3(1.0f, -1.0f, 0.0f), // bottom-right
		Vector3(1.0f, 1.0f, 0.0f), // top-right
		Vector3(-1.0f, 1.0f, 0.0f), // top-left
		// Far plane
		Vector3(-1.0f, -1.0f, 1.0f), // bottom-left
		Vector3(1.0f, -1.0f, 1.0f), // bottom-right
		Vector3(1.0f, 1.0f, 1.0f), // top-right
		Vector3(-1.0f, 1.0f, 1.0f) // top-left
	};

	for (int i = 0; i < 8; ++i) {
		Vector4 view_corner =
				Vector4::transform(Vector4(ndc_corners[i].x, ndc_corners[i].y, ndc_corners[i].z, 1.0f), inv_proj);

		// Perspective divide
		if (std::abs(view_corner.w) > 0.0001f) {
			corners[i] = Vector3(
					view_corner.x / view_corner.w, view_corner.y / view_corner.w, view_corner.z / view_corner.w);
		}
	}

	return corners;
}

Projection Projection::create_reverse_z() const {
	Projection reverse_proj = *this;
	reverse_proj._is_reverse_z = true;

	// Swap near and far for reverse-Z
	std::swap(reverse_proj._near_plane, reverse_proj._far_plane);

	if (reverse_proj._type == ProjectionType::Perspective) {
		reverse_proj._rebuild_perspective();
	}
	else {
		reverse_proj._rebuild_orthographic();
	}

	return reverse_proj;
}

bool Projection::operator==(const Projection& other) const {
	if (_type != other._type)
		return false;
	if (_is_off_center != other._is_off_center)
		return false;
	if (_is_reverse_z != other._is_reverse_z)
		return false;

	if (std::abs(_near_plane - other._near_plane) > small_number)
		return false;
	if (std::abs(_far_plane - other._far_plane) > small_number)
		return false;

	if (_type == ProjectionType::Perspective) {
		if (std::abs(_fov_y - other._fov_y) > small_number)
			return false;
		if (std::abs(_aspect_ratio - other._aspect_ratio) > small_number)
			return false;
	}
	else {
		if (std::abs(_width - other._width) > small_number)
			return false;
		if (std::abs(_height - other._height) > small_number)
			return false;
	}

	if (_is_off_center) {
		if (std::abs(_left - other._left) > small_number)
			return false;
		if (std::abs(_right - other._right) > small_number)
			return false;
		if (std::abs(_bottom - other._bottom) > small_number)
			return false;
		if (std::abs(_top - other._top) > small_number)
			return false;
	}

	return true;
}

} // namespace feather
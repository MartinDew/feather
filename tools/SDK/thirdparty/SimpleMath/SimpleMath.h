//-------------------------------------------------------------------------------------
// SimpleMath.h -- Simplified C++ Math wrapper for DirectXMath
//
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=248929
// http://go.microsoft.com/fwlink/?LinkID=615561
//-------------------------------------------------------------------------------------

#pragma once

#if (defined(_WIN32) || defined(WINAPI_FAMILY)) && !(defined(_XBOX_ONE) && defined(_TITLE)) && !defined(_GAMING_XBOX)
#include <dxgi1_2.h>
#endif

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>

#if (__cplusplus >= 202002L)
#include <compare>
#endif

#include <DirectXCollision.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>

#ifndef DIRECTX_TOOLKIT_API
#ifdef DIRECTX_TOOLKIT_EXPORT
#ifdef __GNUC__
#define DIRECTX_TOOLKIT_API __attribute__((dllexport))
#else
#define DIRECTX_TOOLKIT_API __declspec(dllexport)
#endif
#elif defined(DIRECTX_TOOLKIT_IMPORT)
#ifdef __GNUC__
#define DIRECTX_TOOLKIT_API __attribute__((dllimport))
#else
#define DIRECTX_TOOLKIT_API __declspec(dllimport)
#endif
#else
#define DIRECTX_TOOLKIT_API
#endif
#endif

#if defined(DIRECTX_TOOLKIT_IMPORT) && defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4251 4275)
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif

#ifndef _MSC_VER

#ifndef __cdecl
#define __cdecl

#endif

using UINT = uint32_t;
using LONG = long;

#ifndef WIN32

typedef struct tagRECT {
	long left;
	long top;
	long right;
	long bottom;
} RECT, *PRECT, *NPRECT, *LPRECT;

#endif

#endif

namespace DirectX {
namespace SimpleMath {
struct Vector2;
struct Vector4;
struct Matrix;
struct Quaternion;
struct Plane;

//------------------------------------------------------------------------------
// 2D rectangle
struct DIRECTX_TOOLKIT_API Rectangle {
	long x;
	long y;
	long width;
	long height;

	// Creators
	Rectangle() noexcept :
			x(0), y(0), width(0), height(0) {}
	constexpr Rectangle(long ix, long iy, long iw, long ih) noexcept :
			x(ix), y(iy), width(iw), height(ih) {}
	explicit Rectangle(const RECT& rct) noexcept :
			x(rct.left), y(rct.top), width(rct.right - rct.left), height(rct.bottom - rct.top) {}

	Rectangle(const Rectangle&) = default;
	Rectangle& operator=(const Rectangle&) = default;

	Rectangle(Rectangle&&) = default;
	Rectangle& operator=(Rectangle&&) = default;

	operator RECT() noexcept {
		RECT rct;
		rct.left = x;
		rct.top = y;
		rct.right = (x + width);
		rct.bottom = (y + height);
		return rct;
	}
#ifdef __cplusplus_winrt
	operator Windows::Foundation::Rect() noexcept { return Windows::Foundation::Rect(float(x), float(y), float(width), float(height)); }
#endif

	// Comparison operators
#if (__cplusplus >= 202002L)
	bool operator==(const Rectangle&) const = default;
	auto operator<=>(const Rectangle&) const = default;
#else
	bool operator==(const Rectangle& r) const noexcept { return (x == r.x) && (y == r.y) && (width == r.width) && (height == r.height); }
	bool operator!=(const Rectangle& r) const noexcept { return (x != r.x) || (y != r.y) || (width != r.width) || (height != r.height); }
#endif
	bool operator==(const RECT& rct) const noexcept { return (x == rct.left) && (y == rct.top) && (width == (rct.right - rct.left)) && (height == (rct.bottom - rct.top)); }
	bool operator!=(const RECT& rct) const noexcept { return (x != rct.left) || (y != rct.top) || (width != (rct.right - rct.left)) || (height != (rct.bottom - rct.top)); }

	// Assignment operators
	Rectangle& operator=(_In_ const RECT& rct) noexcept {
		x = rct.left;
		y = rct.top;
		width = (rct.right - rct.left);
		height = (rct.bottom - rct.top);
		return *this;
	}

	// Rectangle operations
	Vector2 location() const noexcept;
	Vector2 center() const noexcept;

	bool is_empty() const noexcept { return (width == 0 && height == 0 && x == 0 && y == 0); }

	bool contains(long ix, long iy) const noexcept { return (x <= ix) && (ix < (x + width)) && (y <= iy) && (iy < (y + height)); }
	bool contains(const Vector2& point) const noexcept;
	bool contains(const Rectangle& r) const noexcept { return (x <= r.x) && ((r.x + r.width) <= (x + width)) && (y <= r.y) && ((r.y + r.height) <= (y + height)); }
	bool contains(const RECT& rct) const noexcept { return (x <= rct.left) && (rct.right <= (x + width)) && (y <= rct.top) && (rct.bottom <= (y + height)); }

	void inflate(long horiz_amount, long vert_amount) noexcept;

	bool intersects(const Rectangle& r) const noexcept { return (r.x < (x + width)) && (x < (r.x + r.width)) && (r.y < (y + height)) && (y < (r.y + r.height)); }
	bool intersects(const RECT& rct) const noexcept { return (rct.left < (x + width)) && (x < rct.right) && (rct.top < (y + height)) && (y < rct.bottom); }

	void offset(long ox, long oy) noexcept {
		x += ox;
		y += oy;
	}

	// Static functions
	static Rectangle intersect(const Rectangle& ra, const Rectangle& rb) noexcept;
	static RECT intersect(const RECT& rcta, const RECT& rctb) noexcept;

	static Rectangle Union(const Rectangle& ra, const Rectangle& rb) noexcept;
	static RECT Union(const RECT& rcta, const RECT& rctb) noexcept;
};

//------------------------------------------------------------------------------
// 2D vector
struct DIRECTX_TOOLKIT_API Vector2 : public XMFLOAT2 {
	Vector2() noexcept :
			XMFLOAT2(0.f, 0.f) {}
	constexpr explicit Vector2(float ix) noexcept :
			XMFLOAT2(ix, ix) {}
	constexpr Vector2(float ix, float iy) noexcept :
			XMFLOAT2(ix, iy) {}
	explicit Vector2(_In_reads_(2) const float* p_array) noexcept :
			XMFLOAT2(p_array) {}
	Vector2(FXMVECTOR v) noexcept { XMStoreFloat2(this, v); }
	Vector2(const XMFLOAT2& v) noexcept {
		this->x = v.x;
		this->y = v.y;
	}
	explicit Vector2(const XMVECTORF32& f) noexcept {
		this->x = f.f[0];
		this->y = f.f[1];
	}

	Vector2(const Vector2&) = default;
	Vector2& operator=(const Vector2&) = default;

	Vector2(Vector2&&) = default;
	Vector2& operator=(Vector2&&) = default;

	operator XMVECTOR() const noexcept { return XMLoadFloat2(this); }

	// Comparison operators
	bool operator==(const Vector2& v) const noexcept { return ((x == v.x) && (y == v.y)); }
	bool operator!=(const Vector2& v) const noexcept { return ((x != v.x) || (y != v.y)); }

	// Assignment operators
	Vector2& operator=(const XMVECTORF32& f) noexcept {
		x = f.f[0];
		y = f.f[1];
		return *this;
	}
	Vector2& operator+=(const Vector2& v) noexcept {
		x += v.x;
		y += v.y;
		return *this;
	}
	Vector2& operator-=(const Vector2& v) noexcept {
		x -= v.x;
		y -= v.y;
		return *this;
	}
	Vector2& operator*=(const Vector2& v) noexcept {
		x *= v.x;
		y *= v.y;
		return *this;
	}
	Vector2& operator*=(float s) noexcept {
		x *= s;
		y *= s;
		return *this;
	}
	Vector2& operator/=(float s) noexcept {
		x /= s;
		y /= s;
		return *this;
	}

	// Unary operators
	Vector2 operator+() const noexcept { return *this; }
	Vector2 operator-() const noexcept { return Vector2(-x, -y); }

	// Vector operations
	[[nodiscard]] bool in_bounds(const Vector2& bounds) const noexcept;

	[[nodiscard]] float length() const noexcept { return std::sqrt((x * x) + (y * y)); }
	float length_squared() const noexcept { return (x * x) + (y * y); }

	float dot(const Vector2& v) const noexcept { return (x * v.x) + (y * v.y); }
	void cross(const Vector2& v, Vector2& result) const noexcept { result.x = result.y = (x * v.y) - (y * v.x); }
	Vector2 cross(const Vector2& v) const noexcept {
		float c = (x * v.y) - (y * v.x);
		return Vector2(c, c);
	}

	void normalize() noexcept;
	void normalize(Vector2& result) const noexcept;

	void clamp(const Vector2& vmin, const Vector2& vmax) noexcept;
	void clamp(const Vector2& vmin, const Vector2& vmax, Vector2& result) const noexcept;

	// Static functions
	static float distance(const Vector2& v1, const Vector2& v2) noexcept;
	static float distance_squared(const Vector2& v1, const Vector2& v2) noexcept;

	static void min(const Vector2& v1, const Vector2& v2, Vector2& result) noexcept;
	static Vector2 min(const Vector2& v1, const Vector2& v2) noexcept;

	static void max(const Vector2& v1, const Vector2& v2, Vector2& result) noexcept;
	static Vector2 max(const Vector2& v1, const Vector2& v2) noexcept;

	static void lerp(const Vector2& v1, const Vector2& v2, float t, Vector2& result) noexcept;
	static Vector2 lerp(const Vector2& v1, const Vector2& v2, float t) noexcept;

	static void smooth_step(const Vector2& v1, const Vector2& v2, float t, Vector2& result) noexcept;
	static Vector2 smooth_step(const Vector2& v1, const Vector2& v2, float t) noexcept;

	static void barycentric(const Vector2& v1, const Vector2& v2, const Vector2& v3, float f, float g, Vector2& result) noexcept;
	static Vector2 barycentric(const Vector2& v1, const Vector2& v2, const Vector2& v3, float f, float g) noexcept;

	static void catmull_rom(const Vector2& v1, const Vector2& v2, const Vector2& v3, const Vector2& v4, float t, Vector2& result) noexcept;
	static Vector2 catmull_rom(const Vector2& v1, const Vector2& v2, const Vector2& v3, const Vector2& v4, float t) noexcept;

	static void hermite(const Vector2& v1, const Vector2& t1, const Vector2& v2, const Vector2& t2, float t, Vector2& result) noexcept;
	static Vector2 hermite(const Vector2& v1, const Vector2& t1, const Vector2& v2, const Vector2& t2, float t) noexcept;

	static void reflect(const Vector2& ivec, const Vector2& nvec, Vector2& result) noexcept;
	static Vector2 reflect(const Vector2& ivec, const Vector2& nvec) noexcept;

	static void refract(const Vector2& ivec, const Vector2& nvec, float refraction_index, Vector2& result) noexcept;
	static Vector2 refract(const Vector2& ivec, const Vector2& nvec, float refraction_index) noexcept;

	static void transform(const Vector2& v, const Quaternion& quat, Vector2& result) noexcept;
	static Vector2 transform(const Vector2& v, const Quaternion& quat) noexcept;

	static void transform(const Vector2& v, const Matrix& m, Vector2& result) noexcept;
	static Vector2 transform(const Vector2& v, const Matrix& m) noexcept;
	static void transform(_In_reads_(count) const Vector2* varray, size_t count, const Matrix& m, _Out_writes_(count) Vector2* result_array) noexcept;

	static void transform(const Vector2& v, const Matrix& m, Vector4& result) noexcept;
	static void transform(_In_reads_(count) const Vector2* varray, size_t count, const Matrix& m, _Out_writes_(count) Vector4* result_array) noexcept;

	static void transform_normal(const Vector2& v, const Matrix& m, Vector2& result) noexcept;
	static Vector2 transform_normal(const Vector2& v, const Matrix& m) noexcept;
	static void transform_normal(_In_reads_(count) const Vector2* varray, size_t count, const Matrix& m, _Out_writes_(count) Vector2* result_array) noexcept;

	// Constants
	static const Vector2 zero;
	static const Vector2 one;
	static const Vector2 unit_x;
	static const Vector2 unit_y;
};

// Binary operators
DIRECTX_TOOLKIT_API inline Vector2 operator+(const Vector2& v1, const Vector2& v2) noexcept { return Vector2(v1.x + v2.x, v1.y + v2.y); }
DIRECTX_TOOLKIT_API inline Vector2 operator-(const Vector2& v1, const Vector2& v2) noexcept { return Vector2(v1.x - v2.x, v1.y - v2.y); }
DIRECTX_TOOLKIT_API inline Vector2 operator*(const Vector2& v1, const Vector2& v2) noexcept { return Vector2(v1.x * v2.x, v1.y * v2.y); }
DIRECTX_TOOLKIT_API inline Vector2 operator*(const Vector2& v, float s) noexcept { return Vector2(v.x * s, v.y * s); }
DIRECTX_TOOLKIT_API inline Vector2 operator*(float s, const Vector2& v) noexcept { return Vector2(s * v.x, s * v.y); }
DIRECTX_TOOLKIT_API inline Vector2 operator/(const Vector2& v1, const Vector2& v2) noexcept { return Vector2(v1.x / v2.x, v1.y / v2.y); }
DIRECTX_TOOLKIT_API inline Vector2 operator/(const Vector2& v, float s) noexcept { return Vector2(v.x / s, v.y / s); }
DIRECTX_TOOLKIT_API inline Vector2 operator/(float s, const Vector2& v) noexcept { return Vector2(s / v.x, s / v.y); };

//------------------------------------------------------------------------------
// 3D vector
struct DIRECTX_TOOLKIT_API Vector3 : public XMFLOAT3 {
	Vector3() noexcept :
			XMFLOAT3(0.f, 0.f, 0.f) {}
	constexpr explicit Vector3(float ix) noexcept :
			XMFLOAT3(ix, ix, ix) {}
	constexpr Vector3(float ix, float iy, float iz) noexcept :
			XMFLOAT3(ix, iy, iz) {}
	explicit Vector3(_In_reads_(3) const float* p_array) noexcept :
			XMFLOAT3(p_array) {}
	Vector3(FXMVECTOR v) noexcept { XMStoreFloat3(this, v); }
	Vector3(const XMFLOAT3& v) noexcept {
		this->x = v.x;
		this->y = v.y;
		this->z = v.z;
	}
	explicit Vector3(const XMVECTORF32& f) noexcept {
		this->x = f.f[0];
		this->y = f.f[1];
		this->z = f.f[2];
	}

	Vector3(const Vector3&) = default;
	Vector3& operator=(const Vector3&) = default;

	Vector3(Vector3&&) = default;
	Vector3& operator=(Vector3&&) = default;

	operator XMVECTOR() const noexcept { return XMLoadFloat3(this); }

	// Comparison operators
	bool operator==(const Vector3& v) const noexcept { return ((x == v.x) && (y == v.y) && (z == v.z)); }
	bool operator!=(const Vector3& v) const noexcept { return ((x != v.x) || (y != v.y) || (z != v.z)); }

	// Assignment operators
	Vector3& operator=(const XMVECTORF32& f) noexcept {
		x = f.f[0];
		y = f.f[1];
		z = f.f[2];
		return *this;
	}
	Vector3& operator+=(const Vector3& v) noexcept {
		x += v.x;
		y += v.y;
		z += v.z;
		return *this;
	}
	Vector3& operator-=(const Vector3& v) noexcept {
		x -= v.x;
		y -= v.y;
		z -= v.z;
		return *this;
	}
	Vector3& operator*=(const Vector3& v) noexcept {
		x *= v.x;
		y *= v.y;
		z *= v.z;
		return *this;
	}
	Vector3& operator*=(float s) noexcept {
		x *= s;
		y *= s;
		z *= s;
		return *this;
	}
	Vector3& operator/=(float s) noexcept {
		x /= s;
		y /= s;
		z /= s;
		return *this;
	}

	// Unary operators
	Vector3 operator+() const noexcept { return *this; }
	Vector3 operator-() const noexcept { return Vector3(-x, -y, -z); }

	// Vector operations
	bool in_bounds(const Vector3& bounds) const noexcept;

	float length() const noexcept { return std::sqrt((x * x) + (y * y) + (z * z)); }
	float length_squared() const noexcept { return (x * x) + (y * y) + (z * z); }

	float dot(const Vector3& v) const noexcept { return (x * v.x) + (y * v.y) + (z * v.z); }
	void cross(const Vector3& v, Vector3& result) const noexcept {
		result.x = y * v.z - z * v.y;
		result.y = z * v.x - x * v.z;
		result.z = x * v.y - y * v.x;
	}
	Vector3 cross(const Vector3& v) const noexcept { return Vector3(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x); }

	void normalize() noexcept;
	void normalize(Vector3& result) const noexcept;

	void clamp(const Vector3& vmin, const Vector3& vmax) noexcept;
	void clamp(const Vector3& vmin, const Vector3& vmax, Vector3& result) const noexcept;

	// Static functions
	static float distance(const Vector3& v1, const Vector3& v2) noexcept;
	static float distance_squared(const Vector3& v1, const Vector3& v2) noexcept;

	static void min(const Vector3& v1, const Vector3& v2, Vector3& result) noexcept;
	static Vector3 min(const Vector3& v1, const Vector3& v2) noexcept;

	static void max(const Vector3& v1, const Vector3& v2, Vector3& result) noexcept;
	static Vector3 max(const Vector3& v1, const Vector3& v2) noexcept;

	static void lerp(const Vector3& v1, const Vector3& v2, float t, Vector3& result) noexcept;
	static Vector3 lerp(const Vector3& v1, const Vector3& v2, float t) noexcept;

	static void smooth_step(const Vector3& v1, const Vector3& v2, float t, Vector3& result) noexcept;
	static Vector3 smooth_step(const Vector3& v1, const Vector3& v2, float t) noexcept;

	static void barycentric(const Vector3& v1, const Vector3& v2, const Vector3& v3, float f, float g, Vector3& result) noexcept;
	static Vector3 barycentric(const Vector3& v1, const Vector3& v2, const Vector3& v3, float f, float g) noexcept;

	static void catmull_rom(const Vector3& v1, const Vector3& v2, const Vector3& v3, const Vector3& v4, float t, Vector3& result) noexcept;
	static Vector3 catmull_rom(const Vector3& v1, const Vector3& v2, const Vector3& v3, const Vector3& v4, float t) noexcept;

	static void hermite(const Vector3& v1, const Vector3& t1, const Vector3& v2, const Vector3& t2, float t, Vector3& result) noexcept;
	static Vector3 hermite(const Vector3& v1, const Vector3& t1, const Vector3& v2, const Vector3& t2, float t) noexcept;

	static void reflect(const Vector3& ivec, const Vector3& nvec, Vector3& result) noexcept;
	static Vector3 reflect(const Vector3& ivec, const Vector3& nvec) noexcept;

	static void refract(const Vector3& ivec, const Vector3& nvec, float refraction_index, Vector3& result) noexcept;
	static Vector3 refract(const Vector3& ivec, const Vector3& nvec, float refraction_index) noexcept;

	static void transform(const Vector3& v, const Quaternion& quat, Vector3& result) noexcept;
	static Vector3 transform(const Vector3& v, const Quaternion& quat) noexcept;

	static void transform(const Vector3& v, const Matrix& m, Vector3& result) noexcept;
	static Vector3 transform(const Vector3& v, const Matrix& m) noexcept;
	static void transform(_In_reads_(count) const Vector3* varray, size_t count, const Matrix& m, _Out_writes_(count) Vector3* result_array) noexcept;

	static void transform(const Vector3& v, const Matrix& m, Vector4& result) noexcept;
	static void transform(_In_reads_(count) const Vector3* varray, size_t count, const Matrix& m, _Out_writes_(count) Vector4* result_array) noexcept;

	static void transform_normal(const Vector3& v, const Matrix& m, Vector3& result) noexcept;
	static Vector3 transform_normal(const Vector3& v, const Matrix& m) noexcept;
	static void transform_normal(_In_reads_(count) const Vector3* varray, size_t count, const Matrix& m, _Out_writes_(count) Vector3* result_array) noexcept;

	// Constants
	static const Vector3 zero;
	static const Vector3 one;
	static const Vector3 unit_x;
	static const Vector3 unit_y;
	static const Vector3 unit_z;
	static const Vector3 up;
	static const Vector3 down;
	static const Vector3 right;
	static const Vector3 left;
	static const Vector3 forward;
	static const Vector3 backward;
};

// Binary operators
DIRECTX_TOOLKIT_API inline Vector3 operator+(const Vector3& v1, const Vector3& v2) noexcept { return Vector3(v1.x + v2.x, v1.y + v2.y, v1.z + v2.z); }
DIRECTX_TOOLKIT_API inline Vector3 operator-(const Vector3& v1, const Vector3& v2) noexcept { return Vector3(v1.x - v2.x, v1.y - v2.y, v1.z - v2.z); }
DIRECTX_TOOLKIT_API inline Vector3 operator*(const Vector3& v1, const Vector3& v2) noexcept { return Vector3(v1.x * v2.x, v1.y * v2.y, v1.z * v2.z); }
DIRECTX_TOOLKIT_API inline Vector3 operator*(const Vector3& v, float s) noexcept { return Vector3(v.x * s, v.y * s, v.z * s); }
DIRECTX_TOOLKIT_API inline Vector3 operator*(float s, const Vector3& v) noexcept { return Vector3(s * v.x, s * v.y, s * v.z); }
DIRECTX_TOOLKIT_API inline Vector3 operator/(const Vector3& v1, const Vector3& v2) noexcept { return Vector3(v1.x / v2.x, v1.y / v2.y, v1.z / v2.z); }
DIRECTX_TOOLKIT_API inline Vector3 operator/(const Vector3& v, float s) noexcept { return Vector3(v.x / s, v.y / s, v.z / s); }
DIRECTX_TOOLKIT_API inline Vector3 operator/(float s, const Vector3& v) noexcept { return Vector3(s / v.x, s / v.y, s / v.z); }

//------------------------------------------------------------------------------
// 4D vector
struct DIRECTX_TOOLKIT_API Vector4 : public XMFLOAT4 {
	Vector4() noexcept :
			XMFLOAT4(0.f, 0.f, 0.f, 0.f) {}
	constexpr explicit Vector4(float ix) noexcept :
			XMFLOAT4(ix, ix, ix, ix) {}
	constexpr Vector4(float ix, float iy, float iz, float iw) noexcept :
			XMFLOAT4(ix, iy, iz, iw) {}
	explicit Vector4(_In_reads_(4) const float* p_array) noexcept :
			XMFLOAT4(p_array) {}
	Vector4(FXMVECTOR v) noexcept { XMStoreFloat4(this, v); }
	Vector4(const XMFLOAT4& v) noexcept {
		this->x = v.x;
		this->y = v.y;
		this->z = v.z;
		this->w = v.w;
	}
	explicit Vector4(const XMVECTORF32& f) noexcept {
		this->x = f.f[0];
		this->y = f.f[1];
		this->z = f.f[2];
		this->w = f.f[3];
	}

	Vector4(const Vector4&) = default;
	Vector4& operator=(const Vector4&) = default;

	Vector4(Vector4&&) = default;
	Vector4& operator=(Vector4&&) = default;

	operator XMVECTOR() const noexcept { return XMLoadFloat4(this); }

	// Comparison operators
	bool operator==(const Vector4& v) const noexcept;
	bool operator!=(const Vector4& v) const noexcept;

	// Assignment operators
	Vector4& operator=(const XMVECTORF32& f) noexcept {
		x = f.f[0];
		y = f.f[1];
		z = f.f[2];
		w = f.f[3];
		return *this;
	}
	Vector4& operator+=(const Vector4& v) noexcept;
	Vector4& operator-=(const Vector4& v) noexcept;
	Vector4& operator*=(const Vector4& v) noexcept;
	Vector4& operator*=(float s) noexcept;
	Vector4& operator/=(float s) noexcept;

	// Unary operators
	Vector4 operator+() const noexcept { return *this; }
	Vector4 operator-() const noexcept { return Vector4(-x, -y, -z, -w); }

	// Vector operations
	bool in_bounds(const Vector4& bounds) const noexcept;

	float length() const noexcept;
	float length_squared() const noexcept;

	float dot(const Vector4& v) const noexcept;
	void cross(const Vector4& v1, const Vector4& v2, Vector4& result) const noexcept;
	Vector4 cross(const Vector4& v1, const Vector4& v2) const noexcept;

	void normalize() noexcept;
	void normalize(Vector4& result) const noexcept;

	void clamp(const Vector4& vmin, const Vector4& vmax) noexcept;
	void clamp(const Vector4& vmin, const Vector4& vmax, Vector4& result) const noexcept;

	// Static functions
	static float distance(const Vector4& v1, const Vector4& v2) noexcept;
	static float distance_squared(const Vector4& v1, const Vector4& v2) noexcept;

	static void min(const Vector4& v1, const Vector4& v2, Vector4& result) noexcept;
	static Vector4 min(const Vector4& v1, const Vector4& v2) noexcept;

	static void max(const Vector4& v1, const Vector4& v2, Vector4& result) noexcept;
	static Vector4 max(const Vector4& v1, const Vector4& v2) noexcept;

	static void lerp(const Vector4& v1, const Vector4& v2, float t, Vector4& result) noexcept;
	static Vector4 lerp(const Vector4& v1, const Vector4& v2, float t) noexcept;

	static void smooth_step(const Vector4& v1, const Vector4& v2, float t, Vector4& result) noexcept;
	static Vector4 smooth_step(const Vector4& v1, const Vector4& v2, float t) noexcept;

	static void barycentric(const Vector4& v1, const Vector4& v2, const Vector4& v3, float f, float g, Vector4& result) noexcept;
	static Vector4 barycentric(const Vector4& v1, const Vector4& v2, const Vector4& v3, float f, float g) noexcept;

	static void catmull_rom(const Vector4& v1, const Vector4& v2, const Vector4& v3, const Vector4& v4, float t, Vector4& result) noexcept;
	static Vector4 catmull_rom(const Vector4& v1, const Vector4& v2, const Vector4& v3, const Vector4& v4, float t) noexcept;

	static void hermite(const Vector4& v1, const Vector4& t1, const Vector4& v2, const Vector4& t2, float t, Vector4& result) noexcept;
	static Vector4 hermite(const Vector4& v1, const Vector4& t1, const Vector4& v2, const Vector4& t2, float t) noexcept;

	static void reflect(const Vector4& ivec, const Vector4& nvec, Vector4& result) noexcept;
	static Vector4 reflect(const Vector4& ivec, const Vector4& nvec) noexcept;

	static void refract(const Vector4& ivec, const Vector4& nvec, float refraction_index, Vector4& result) noexcept;
	static Vector4 refract(const Vector4& ivec, const Vector4& nvec, float refraction_index) noexcept;

	static void transform(const Vector2& v, const Quaternion& quat, Vector4& result) noexcept;
	static Vector4 transform(const Vector2& v, const Quaternion& quat) noexcept;

	static void transform(const Vector3& v, const Quaternion& quat, Vector4& result) noexcept;
	static Vector4 transform(const Vector3& v, const Quaternion& quat) noexcept;

	static void transform(const Vector4& v, const Quaternion& quat, Vector4& result) noexcept;
	static Vector4 transform(const Vector4& v, const Quaternion& quat) noexcept;

	static void transform(const Vector4& v, const Matrix& m, Vector4& result) noexcept;
	static Vector4 transform(const Vector4& v, const Matrix& m) noexcept;
	static void transform(_In_reads_(count) const Vector4* varray, size_t count, const Matrix& m, _Out_writes_(count) Vector4* result_array) noexcept;

	// Constants
	static const Vector4 zero;
	static const Vector4 one;
	static const Vector4 unit_x;
	static const Vector4 unit_y;
	static const Vector4 unit_z;
	static const Vector4 unit_w;
};

// Binary operators
DIRECTX_TOOLKIT_API Vector4 operator+(const Vector4& v1, const Vector4& v2) noexcept;
DIRECTX_TOOLKIT_API Vector4 operator-(const Vector4& v1, const Vector4& v2) noexcept;
DIRECTX_TOOLKIT_API Vector4 operator*(const Vector4& v1, const Vector4& v2) noexcept;
DIRECTX_TOOLKIT_API Vector4 operator*(const Vector4& v, float s) noexcept;
DIRECTX_TOOLKIT_API Vector4 operator*(float s, const Vector4& v) noexcept;
DIRECTX_TOOLKIT_API Vector4 operator/(const Vector4& v1, const Vector4& v2) noexcept;
DIRECTX_TOOLKIT_API Vector4 operator/(const Vector4& v, float s) noexcept;
DIRECTX_TOOLKIT_API Vector4 operator/(float s, const Vector4& v) noexcept;

//------------------------------------------------------------------------------
// 4x4 Matrix (assumes right-handed cooordinates)
struct DIRECTX_TOOLKIT_API Matrix : public XMFLOAT4X4 {
	Matrix() noexcept
			:
			XMFLOAT4X4(1.f, 0, 0, 0,
					0, 1.f, 0, 0,
					0, 0, 1.f, 0,
					0, 0, 0, 1.f) {}
	constexpr Matrix(float m00, float m01, float m02, float m03,
			float m10, float m11, float m12, float m13,
			float m20, float m21, float m22, float m23,
			float m30, float m31, float m32, float m33) noexcept
			:
			XMFLOAT4X4(m00, m01, m02, m03,
					m10, m11, m12, m13,
					m20, m21, m22, m23,
					m30, m31, m32, m33) {}
	explicit Matrix(const Vector3& r0, const Vector3& r1, const Vector3& r2) noexcept
			:
			XMFLOAT4X4(r0.x, r0.y, r0.z, 0,
					r1.x, r1.y, r1.z, 0,
					r2.x, r2.y, r2.z, 0,
					0, 0, 0, 1.f) {}
	explicit Matrix(const Vector4& r0, const Vector4& r1, const Vector4& r2, const Vector4& r3) noexcept
			:
			XMFLOAT4X4(r0.x, r0.y, r0.z, r0.w,
					r1.x, r1.y, r1.z, r1.w,
					r2.x, r2.y, r2.z, r2.w,
					r3.x, r3.y, r3.z, r3.w) {}
	Matrix(const XMFLOAT4X4& m) noexcept { memcpy(this, &m, sizeof(XMFLOAT4X4)); }
	Matrix(const XMFLOAT3X3& m) noexcept;
	Matrix(const XMFLOAT4X3& m) noexcept;

	explicit Matrix(_In_reads_(16) const float* p_array) noexcept :
			XMFLOAT4X4(p_array) {}
	Matrix(CXMMATRIX m) noexcept { XMStoreFloat4x4(this, m); }

	Matrix(const Matrix&) = default;
	Matrix& operator=(const Matrix&) = default;

	Matrix(Matrix&&) = default;
	Matrix& operator=(Matrix&&) = default;

	operator XMMATRIX() const noexcept { return XMLoadFloat4x4(this); }

	// Comparison operators
	bool operator==(const Matrix& m) const noexcept;
	bool operator!=(const Matrix& m) const noexcept;

	// Assignment operators
	Matrix& operator=(const XMFLOAT3X3& m) noexcept;
	Matrix& operator=(const XMFLOAT4X3& m) noexcept;
	Matrix& operator+=(const Matrix& m) noexcept;
	Matrix& operator-=(const Matrix& m) noexcept;
	Matrix& operator*=(const Matrix& m) noexcept;
	Matrix& operator*=(float s) noexcept;
	Matrix& operator/=(float s) noexcept;

	Matrix& operator/=(const Matrix& m) noexcept;
	// Element-wise divide

	// Unary operators
	Matrix operator+() const noexcept { return *this; }
	Matrix operator-() const noexcept;

	// Properties
	Vector3 up() const noexcept { return Vector3(_21, _22, _23); }
	void up(const Vector3& v) noexcept {
		_21 = v.x;
		_22 = v.y;
		_23 = v.z;
	}

	Vector3 down() const noexcept { return Vector3(-_21, -_22, -_23); }
	void down(const Vector3& v) noexcept {
		_21 = -v.x;
		_22 = -v.y;
		_23 = -v.z;
	}

	Vector3 right() const noexcept { return Vector3(_11, _12, _13); }
	void right(const Vector3& v) noexcept {
		_11 = v.x;
		_12 = v.y;
		_13 = v.z;
	}

	Vector3 left() const noexcept { return Vector3(-_11, -_12, -_13); }
	void left(const Vector3& v) noexcept {
		_11 = -v.x;
		_12 = -v.y;
		_13 = -v.z;
	}

	Vector3 forward() const noexcept { return Vector3(-_31, -_32, -_33); }
	void forward(const Vector3& v) noexcept {
		_31 = -v.x;
		_32 = -v.y;
		_33 = -v.z;
	}

	Vector3 backward() const noexcept { return Vector3(_31, _32, _33); }
	void backward(const Vector3& v) noexcept {
		_31 = v.x;
		_32 = v.y;
		_33 = v.z;
	}

	Vector3 translation() const noexcept { return Vector3(_41, _42, _43); }
	void translation(const Vector3& v) noexcept {
		_41 = v.x;
		_42 = v.y;
		_43 = v.z;
	}

	// Matrix operations
	bool decompose(Vector3& scale, Quaternion& rotation, Vector3& translation) noexcept;

	Matrix transpose() const noexcept;
	void transpose(Matrix& result) const noexcept;

	Matrix invert() const noexcept;
	void invert(Matrix& result) const noexcept;

	float determinant() const noexcept;

	// Computes rotation about y-axis (y), then x-axis (x), then z-axis (z)
	Vector3 to_euler() const noexcept;

	// Static functions
	static Matrix create_billboard(
			const Vector3& object, const Vector3& camera_position, const Vector3& camera_up, _In_opt_ const Vector3* camera_forward = nullptr) noexcept;

	static Matrix create_constrained_billboard(
			const Vector3& object, const Vector3& camera_position, const Vector3& rotate_axis,
			_In_opt_ const Vector3* camera_forward = nullptr, _In_opt_ const Vector3* object_forward = nullptr) noexcept;

	static Matrix create_translation(const Vector3& position) noexcept;
	static Matrix create_translation(float x, float y, float z) noexcept;

	static Matrix create_scale(const Vector3& scales) noexcept;
	static Matrix create_scale(float xs, float ys, float zs) noexcept;
	static Matrix create_scale(float scale) noexcept;

	static Matrix create_rotation_x(float radians) noexcept;
	static Matrix create_rotation_y(float radians) noexcept;
	static Matrix create_rotation_z(float radians) noexcept;

	static Matrix create_from_axis_angle(const Vector3& axis, float angle) noexcept;

	static Matrix create_perspective_field_of_view(float fov, float aspect_ratio, float near_plane, float far_plane) noexcept;
	static Matrix create_perspective(float width, float height, float near_plane, float far_plane) noexcept;
	static Matrix create_perspective_off_center(float left, float right, float bottom, float top, float near_plane, float far_plane) noexcept;
	static Matrix create_orthographic(float width, float height, float z_near_plane, float z_far_plane) noexcept;
	static Matrix create_orthographic_off_center(float left, float right, float bottom, float top, float z_near_plane, float z_far_plane) noexcept;

	static Matrix create_look_at(const Vector3& position, const Vector3& target, const Vector3& up) noexcept;
	static Matrix create_world(const Vector3& position, const Vector3& forward, const Vector3& up) noexcept;

	static Matrix create_from_quaternion(const Quaternion& quat) noexcept;

	// Rotates about y-axis (yaw), then x-axis (pitch), then z-axis (roll)
	static Matrix create_from_yaw_pitch_roll(float yaw, float pitch, float roll) noexcept;

	// Rotates about y-axis (angles.y), then x-axis (angles.x), then z-axis (angles.z)
	static Matrix create_from_yaw_pitch_roll(const Vector3& angles) noexcept;

	static Matrix create_shadow(const Vector3& light_dir, const Plane& plane) noexcept;

	static Matrix create_reflection(const Plane& plane) noexcept;

	static void lerp(const Matrix& m1, const Matrix& m2, float t, Matrix& result) noexcept;
	static Matrix lerp(const Matrix& m1, const Matrix& m2, float t) noexcept;

	static void transform(const Matrix& m, const Quaternion& rotation, Matrix& result) noexcept;
	static Matrix transform(const Matrix& m, const Quaternion& rotation) noexcept;

	// Constants
	static const Matrix identity;
};

// Binary operators
DIRECTX_TOOLKIT_API Matrix operator+(const Matrix& m1, const Matrix& m2) noexcept;
DIRECTX_TOOLKIT_API Matrix operator-(const Matrix& m1, const Matrix& m2) noexcept;
DIRECTX_TOOLKIT_API Matrix operator*(const Matrix& m1, const Matrix& m2) noexcept;
DIRECTX_TOOLKIT_API Matrix operator*(const Matrix& m, float s) noexcept;
DIRECTX_TOOLKIT_API Matrix operator*(float s, const Matrix& m) noexcept;
DIRECTX_TOOLKIT_API Matrix operator/(const Matrix& m, float s) noexcept;
DIRECTX_TOOLKIT_API Matrix operator/(const Matrix& m1, const Matrix& m2) noexcept;
// Element-wise divide
DIRECTX_TOOLKIT_API Matrix operator/(float s, const Matrix& m) noexcept;

//-----------------------------------------------------------------------------
// Plane
struct DIRECTX_TOOLKIT_API Plane : public XMFLOAT4 {
	Plane() noexcept :
			XMFLOAT4(0.f, 1.f, 0.f, 0.f) {}
	constexpr Plane(float ix, float iy, float iz, float iw) noexcept :
			XMFLOAT4(ix, iy, iz, iw) {}
	Plane(const Vector3& normal, float d) noexcept :
			XMFLOAT4(normal.x, normal.y, normal.z, d) {}
	Plane(const Vector3& point1, const Vector3& point2, const Vector3& point3) noexcept;
	Plane(const Vector3& point, const Vector3& normal) noexcept;
	explicit Plane(const Vector4& v) noexcept :
			XMFLOAT4(v.x, v.y, v.z, v.w) {}
	explicit Plane(_In_reads_(4) const float* p_array) noexcept :
			XMFLOAT4(p_array) {}
	Plane(FXMVECTOR v) noexcept { XMStoreFloat4(this, v); }
	Plane(const XMFLOAT4& p) noexcept {
		this->x = p.x;
		this->y = p.y;
		this->z = p.z;
		this->w = p.w;
	}
	explicit Plane(const XMVECTORF32& f) noexcept {
		this->x = f.f[0];
		this->y = f.f[1];
		this->z = f.f[2];
		this->w = f.f[3];
	}

	Plane(const Plane&) = default;
	Plane& operator=(const Plane&) = default;

	Plane(Plane&&) = default;
	Plane& operator=(Plane&&) = default;

	operator XMVECTOR() const noexcept { return XMLoadFloat4(this); }

	// Comparison operators
	bool operator==(const Plane& p) const noexcept;
	bool operator!=(const Plane& p) const noexcept;

	// Assignment operators
	Plane& operator=(const XMVECTORF32& f) noexcept {
		x = f.f[0];
		y = f.f[1];
		z = f.f[2];
		w = f.f[3];
		return *this;
	}

	// Properties
	Vector3 normal() const noexcept { return Vector3(x, y, z); }
	void normal(const Vector3& normal) noexcept {
		x = normal.x;
		y = normal.y;
		z = normal.z;
	}

	float d() const noexcept { return w; }
	void d(float d) noexcept { w = d; }

	// Plane operations
	void normalize() noexcept;
	void normalize(Plane& result) const noexcept;

	float dot(const Vector4& v) const noexcept;
	float dot_coordinate(const Vector3& position) const noexcept;
	float dot_normal(const Vector3& normal) const noexcept;

	// Static functions
	static void transform(const Plane& plane, const Matrix& m, Plane& result) noexcept;
	static Plane transform(const Plane& plane, const Matrix& m) noexcept;

	static void transform(const Plane& plane, const Quaternion& rotation, Plane& result) noexcept;
	static Plane transform(const Plane& plane, const Quaternion& rotation) noexcept;
	// Input quaternion must be the inverse transpose of the transformation
};

//------------------------------------------------------------------------------
// Quaternion
struct DIRECTX_TOOLKIT_API Quaternion : public XMFLOAT4 {
	Quaternion() noexcept :
			XMFLOAT4(0, 0, 0, 1.f) {}
	constexpr Quaternion(float ix, float iy, float iz, float iw) noexcept :
			XMFLOAT4(ix, iy, iz, iw) {}
	Quaternion(const Vector3& v, float scalar) noexcept :
			XMFLOAT4(v.x, v.y, v.z, scalar) {}
	explicit Quaternion(const Vector4& v) noexcept :
			XMFLOAT4(v.x, v.y, v.z, v.w) {}
	explicit Quaternion(_In_reads_(4) const float* p_array) noexcept :
			XMFLOAT4(p_array) {}
	Quaternion(FXMVECTOR v) noexcept { XMStoreFloat4(this, v); }
	Quaternion(const XMFLOAT4& q) noexcept {
		this->x = q.x;
		this->y = q.y;
		this->z = q.z;
		this->w = q.w;
	}
	explicit Quaternion(const XMVECTORF32& f) noexcept {
		this->x = f.f[0];
		this->y = f.f[1];
		this->z = f.f[2];
		this->w = f.f[3];
	}

	Quaternion(const Quaternion&) = default;
	Quaternion& operator=(const Quaternion&) = default;

	Quaternion(Quaternion&&) = default;
	Quaternion& operator=(Quaternion&&) = default;

	operator XMVECTOR() const noexcept { return XMLoadFloat4(this); }

	// Comparison operators
	bool operator==(const Quaternion& q) const noexcept;
	bool operator!=(const Quaternion& q) const noexcept;

	// Assignment operators
	Quaternion& operator=(const XMVECTORF32& f) noexcept {
		x = f.f[0];
		y = f.f[1];
		z = f.f[2];
		w = f.f[3];
		return *this;
	}
	Quaternion& operator+=(const Quaternion& q) noexcept;
	Quaternion& operator-=(const Quaternion& q) noexcept;
	Quaternion& operator*=(const Quaternion& q) noexcept;
	Quaternion& operator*=(float s) noexcept;
	Quaternion& operator/=(const Quaternion& q) noexcept;

	// Unary operators
	Quaternion operator+() const noexcept { return *this; }
	Quaternion operator-() const noexcept { return Quaternion(-x, -y, -z, -w); }

	// Quaternion operations
	float length() const noexcept;
	float length_squared() const noexcept;

	void normalize() noexcept;
	void normalize(Quaternion& result) const noexcept;

	void conjugate() noexcept;
	void conjugate(Quaternion& result) const noexcept;

	void inverse(Quaternion& result) const noexcept;

	float dot(const Quaternion& q) const noexcept;

	void rotate_towards(const Quaternion& target, float max_angle) noexcept;
	void __cdecl rotate_towards(const Quaternion& target, float max_angle, Quaternion& result) const noexcept;

	// Computes rotation about y-axis (y), then x-axis (x), then z-axis (z)
	Vector3 to_euler() const noexcept;

	// Static functions
	static Quaternion create_from_axis_angle(const Vector3& axis, float angle) noexcept;

	// Rotates about y-axis (yaw), then x-axis (pitch), then z-axis (roll)
	static Quaternion create_from_yaw_pitch_roll(float yaw, float pitch, float roll) noexcept;

	// Rotates about y-axis (angles.y), then x-axis (angles.x), then z-axis (angles.z)
	static Quaternion create_from_yaw_pitch_roll(const Vector3& angles) noexcept;

	static Quaternion create_from_rotation_matrix(const Matrix& m) noexcept;

	static void lerp(const Quaternion& q1, const Quaternion& q2, float t, Quaternion& result) noexcept;
	static Quaternion lerp(const Quaternion& q1, const Quaternion& q2, float t) noexcept;

	static void slerp(const Quaternion& q1, const Quaternion& q2, float t, Quaternion& result) noexcept;
	static Quaternion slerp(const Quaternion& q1, const Quaternion& q2, float t) noexcept;

	static void concatenate(const Quaternion& q1, const Quaternion& q2, Quaternion& result) noexcept;
	static Quaternion concatenate(const Quaternion& q1, const Quaternion& q2) noexcept;

	static void __cdecl from_to_rotation(const Vector3& from_dir, const Vector3& to_dir, Quaternion& result) noexcept;
	static Quaternion from_to_rotation(const Vector3& from_dir, const Vector3& to_dir) noexcept;

	static void __cdecl look_rotation(const Vector3& forward, const Vector3& up, Quaternion& result) noexcept;
	static Quaternion look_rotation(const Vector3& forward, const Vector3& up) noexcept;

	static float angle(const Quaternion& q1, const Quaternion& q2) noexcept;

	// Constants
	static const Quaternion identity;
};

// Binary operators
DIRECTX_TOOLKIT_API Quaternion operator+(const Quaternion& q1, const Quaternion& q2) noexcept;
DIRECTX_TOOLKIT_API Quaternion operator-(const Quaternion& q1, const Quaternion& q2) noexcept;
DIRECTX_TOOLKIT_API Quaternion operator*(const Quaternion& q1, const Quaternion& q2) noexcept;
DIRECTX_TOOLKIT_API Quaternion operator*(const Quaternion& q, float s) noexcept;
DIRECTX_TOOLKIT_API Quaternion operator*(float s, const Quaternion& q) noexcept;
DIRECTX_TOOLKIT_API Quaternion operator/(const Quaternion& q1, const Quaternion& q2) noexcept;

//------------------------------------------------------------------------------
// Color
struct DIRECTX_TOOLKIT_API Color : public XMFLOAT4 {
	Color() noexcept :
			XMFLOAT4(0, 0, 0, 1.f) {}
	constexpr Color(float r, float g, float b) noexcept :
			XMFLOAT4(r, g, b, 1.f) {}
	constexpr Color(float r, float g, float b, float a) noexcept :
			XMFLOAT4(r, g, b, a) {}
	explicit Color(const Vector3& clr) noexcept :
			XMFLOAT4(clr.x, clr.y, clr.z, 1.f) {}
	explicit Color(const Vector4& clr) noexcept :
			XMFLOAT4(clr.x, clr.y, clr.z, clr.w) {}
	explicit Color(_In_reads_(4) const float* p_array) noexcept :
			XMFLOAT4(p_array) {}
	Color(FXMVECTOR v) noexcept { XMStoreFloat4(this, v); }
	Color(const XMFLOAT4& c) noexcept {
		this->x = c.x;
		this->y = c.y;
		this->z = c.z;
		this->w = c.w;
	}
	explicit Color(const XMVECTORF32& f) noexcept {
		this->x = f.f[0];
		this->y = f.f[1];
		this->z = f.f[2];
		this->w = f.f[3];
	}

	// BGRA Direct3D 9 D3DCOLOR packed color
	explicit Color(const DirectX::PackedVector::XMCOLOR& packed) noexcept;

	// RGBA XNA Game Studio packed color
	explicit Color(const DirectX::PackedVector::XMUBYTEN4& packed) noexcept;

	Color(const Color&) = default;
	Color& operator=(const Color&) = default;

	Color(Color&&) = default;
	Color& operator=(Color&&) = default;

	operator XMVECTOR() const noexcept { return XMLoadFloat4(this); }
	operator const float*() const noexcept { return reinterpret_cast<const float*>(this); }

	// Comparison operators
	bool operator==(const Color& c) const noexcept;
	bool operator!=(const Color& c) const noexcept;

	// Assignment operators
	Color& operator=(const XMVECTORF32& f) noexcept {
		x = f.f[0];
		y = f.f[1];
		z = f.f[2];
		w = f.f[3];
		return *this;
	}
	Color& operator=(const DirectX::PackedVector::XMCOLOR& packed) noexcept;
	Color& operator=(const DirectX::PackedVector::XMUBYTEN4& packed) noexcept;
	Color& operator+=(const Color& c) noexcept;
	Color& operator-=(const Color& c) noexcept;
	Color& operator*=(const Color& c) noexcept;
	Color& operator*=(float s) noexcept;
	Color& operator/=(const Color& c) noexcept;

	// Unary operators
	Color operator+() const noexcept { return *this; }
	Color operator-() const noexcept { return Color(-x, -y, -z, -w); }

	// Properties
	float r() const noexcept { return x; }
	void r(float r) noexcept { x = r; }

	float g() const noexcept { return y; }
	void g(float g) noexcept { y = g; }

	float b() const noexcept { return z; }
	void b(float b) noexcept { z = b; }

	float a() const noexcept { return w; }
	void a(float a) noexcept { w = a; }

	// Color operations
	DirectX::PackedVector::XMCOLOR bgra() const noexcept;
	DirectX::PackedVector::XMUBYTEN4 rgba() const noexcept;

	Vector3 to_vector3() const noexcept { return Vector3(x, y, z); }
	Vector4 to_vector4() const noexcept { return Vector4(x, y, z, w); }

	void negate() noexcept;
	void negate(Color& result) const noexcept;

	void saturate() noexcept;
	void saturate(Color& result) const noexcept;

	void premultiply() noexcept;
	void premultiply(Color& result) const noexcept;

	void adjust_saturation(float sat) noexcept;
	void adjust_saturation(float sat, Color& result) const noexcept;

	void adjust_contrast(float contrast) noexcept;
	void adjust_contrast(float contrast, Color& result) const noexcept;

	// Static functions
	static void modulate(const Color& c1, const Color& c2, Color& result) noexcept;
	static Color modulate(const Color& c1, const Color& c2) noexcept;

	static void lerp(const Color& c1, const Color& c2, float t, Color& result) noexcept;
	static Color lerp(const Color& c1, const Color& c2, float t) noexcept;
};

// Binary operators
DIRECTX_TOOLKIT_API Color operator+(const Color& c1, const Color& c2) noexcept;
DIRECTX_TOOLKIT_API Color operator-(const Color& c1, const Color& c2) noexcept;
DIRECTX_TOOLKIT_API Color operator*(const Color& c1, const Color& c2) noexcept;
DIRECTX_TOOLKIT_API Color operator*(const Color& c, float s) noexcept;
DIRECTX_TOOLKIT_API Color operator*(float s, const Color& c) noexcept;
DIRECTX_TOOLKIT_API Color operator/(const Color& c1, const Color& c2) noexcept;

//------------------------------------------------------------------------------
// Ray
class DIRECTX_TOOLKIT_API Ray {
public:
	Vector3 position;
	Vector3 direction;

	Ray() noexcept :
			position(0, 0, 0), direction(0, 0, 1) {}
	Ray(const Vector3& pos, const Vector3& dir) noexcept :
			position(pos), direction(dir) {}

	Ray(const Ray&) = default;
	Ray& operator=(const Ray&) = default;

	Ray(Ray&&) = default;
	Ray& operator=(Ray&&) = default;

	// Comparison operators
	bool operator==(const Ray& r) const noexcept;
	bool operator!=(const Ray& r) const noexcept;

	// Ray operations
	bool intersects(const BoundingSphere& sphere, _Out_ float& dist) const noexcept;
	bool intersects(const BoundingBox& box, _Out_ float& dist) const noexcept;
	bool intersects(const Vector3& tri0, const Vector3& tri1, const Vector3& tri2, _Out_ float& dist) const noexcept;
	bool intersects(const Plane& plane, _Out_ float& dist) const noexcept;
};

//------------------------------------------------------------------------------
// Viewport
class DIRECTX_TOOLKIT_API Viewport {
public:
	float x;
	float y;
	float width;
	float height;
	float min_depth;
	float max_depth;

	Viewport() noexcept :
			x(0.f), y(0.f), width(0.f), height(0.f), min_depth(0.f), max_depth(1.f) {}
	constexpr Viewport(float ix, float iy, float iw, float ih, float iminz = 0.f, float imaxz = 1.f) noexcept :
			x(ix), y(iy), width(iw), height(ih), min_depth(iminz), max_depth(imaxz) {}
	explicit Viewport(const RECT& rct) noexcept :
			x(float(rct.left)), y(float(rct.top)), width(float(rct.right - rct.left)), height(float(rct.bottom - rct.top)), min_depth(0.f), max_depth(1.f) {}

#if defined(__d3d11_h__) || defined(__d3d11_x_h__)
	// Direct3D 11 interop
	explicit Viewport(const D3D11_VIEWPORT& vp) noexcept :
			x(vp.TopLeftX), y(vp.TopLeftY), width(vp.Width), height(vp.Height), min_depth(vp.MinDepth), max_depth(vp.MaxDepth) {}

	operator D3D11_VIEWPORT() noexcept { return *reinterpret_cast<const D3D11_VIEWPORT*>(this); }
	const D3D11_VIEWPORT* Get11() const noexcept { return reinterpret_cast<const D3D11_VIEWPORT*>(this); }
	Viewport& operator=(const D3D11_VIEWPORT& vp) noexcept;
#endif

#if defined(__d3d12_h__) || defined(__d3d12_x_h__) || defined(__XBOX_D3D12_X__)
	// Direct3D 12 interop
	explicit Viewport(const D3D12_VIEWPORT& vp) noexcept :
			x(vp.TopLeftX), y(vp.TopLeftY), width(vp.Width), height(vp.Height), min_depth(vp.MinDepth), max_depth(vp.MaxDepth) {}

	operator D3D12_VIEWPORT() noexcept { return *reinterpret_cast<const D3D12_VIEWPORT*>(this); }
	const D3D12_VIEWPORT* Get12() const noexcept { return reinterpret_cast<const D3D12_VIEWPORT*>(this); }
	Viewport& operator=(const D3D12_VIEWPORT& vp) noexcept;
#endif

	Viewport(const Viewport&) = default;
	Viewport& operator=(const Viewport&) = default;

	Viewport(Viewport&&) = default;
	Viewport& operator=(Viewport&&) = default;

	// Comparison operators
#if (__cplusplus >= 202002L)
	bool operator==(const Viewport&) const = default;
	auto operator<=>(const Viewport&) const = default;
#else
	bool operator==(const Viewport& vp) const noexcept;
	bool operator!=(const Viewport& vp) const noexcept;
#endif

	// Assignment operators
	Viewport& operator=(const RECT& rct) noexcept;

	// Viewport operations
	float aspect_ratio() const noexcept;

	Vector3 project(const Vector3& p, const Matrix& proj, const Matrix& view, const Matrix& world) const noexcept;
	void project(const Vector3& p, const Matrix& proj, const Matrix& view, const Matrix& world, Vector3& result) const noexcept;

	Vector3 unproject(const Vector3& p, const Matrix& proj, const Matrix& view, const Matrix& world) const noexcept;
	void unproject(const Vector3& p, const Matrix& proj, const Matrix& view, const Matrix& world, Vector3& result) const noexcept;

	// Static methods
#if defined(__dxgi1_2_h__) || defined(__d3d11_x_h__) || defined(__d3d12_x_h__) || defined(__XBOX_D3D12_X__)
	static RECT __cdecl ComputeDisplayArea(DXGI_SCALING scaling, UINT backBufferWidth, UINT backBufferHeight, int outputWidth, int outputHeight) noexcept;
#endif
	static RECT __cdecl compute_title_safe_area(UINT back_buffer_width, UINT back_buffer_height) noexcept;
};

#include "SimpleMath.inl"

} // namespace SimpleMath

} // namespace DirectX

//------------------------------------------------------------------------------
// Support for SimpleMath and Standard C++ Library containers
namespace std {

template <>
struct less<DirectX::SimpleMath::Rectangle> {
	bool operator()(const DirectX::SimpleMath::Rectangle& r1, const DirectX::SimpleMath::Rectangle& r2) const noexcept {
		return ((r1.x < r2.x) || ((r1.x == r2.x) && (r1.y < r2.y)) || ((r1.x == r2.x) && (r1.y == r2.y) && (r1.width < r2.width)) || ((r1.x == r2.x) && (r1.y == r2.y) && (r1.width == r2.width) && (r1.height < r2.height)));
	}
};

template <>
struct less<DirectX::SimpleMath::Vector2> {
	bool operator()(const DirectX::SimpleMath::Vector2& v1, const DirectX::SimpleMath::Vector2& v2) const noexcept {
		return ((v1.x < v2.x) || ((v1.x == v2.x) && (v1.y < v2.y)));
	}
};

template <>
struct less<DirectX::SimpleMath::Vector3> {
	bool operator()(const DirectX::SimpleMath::Vector3& v1, const DirectX::SimpleMath::Vector3& v2) const noexcept {
		return ((v1.x < v2.x) || ((v1.x == v2.x) && (v1.y < v2.y)) || ((v1.x == v2.x) && (v1.y == v2.y) && (v1.z < v2.z)));
	}
};

template <>
struct less<DirectX::SimpleMath::Vector4> {
	bool operator()(const DirectX::SimpleMath::Vector4& v1, const DirectX::SimpleMath::Vector4& v2) const noexcept {
		return ((v1.x < v2.x) || ((v1.x == v2.x) && (v1.y < v2.y)) || ((v1.x == v2.x) && (v1.y == v2.y) && (v1.z < v2.z)) || ((v1.x == v2.x) && (v1.y == v2.y) && (v1.z == v2.z) && (v1.w < v2.w)));
	}
};

template <>
struct less<DirectX::SimpleMath::Matrix> {
	bool operator()(const DirectX::SimpleMath::Matrix& m1, const DirectX::SimpleMath::Matrix& m2) const noexcept {
		if (m1._11 != m2._11)
			return m1._11 < m2._11;
		if (m1._12 != m2._12)
			return m1._12 < m2._12;
		if (m1._13 != m2._13)
			return m1._13 < m2._13;
		if (m1._14 != m2._14)
			return m1._14 < m2._14;
		if (m1._21 != m2._21)
			return m1._21 < m2._21;
		if (m1._22 != m2._22)
			return m1._22 < m2._22;
		if (m1._23 != m2._23)
			return m1._23 < m2._23;
		if (m1._24 != m2._24)
			return m1._24 < m2._24;
		if (m1._31 != m2._31)
			return m1._31 < m2._31;
		if (m1._32 != m2._32)
			return m1._32 < m2._32;
		if (m1._33 != m2._33)
			return m1._33 < m2._33;
		if (m1._34 != m2._34)
			return m1._34 < m2._34;
		if (m1._41 != m2._41)
			return m1._41 < m2._41;
		if (m1._42 != m2._42)
			return m1._42 < m2._42;
		if (m1._43 != m2._43)
			return m1._43 < m2._43;
		if (m1._44 != m2._44)
			return m1._44 < m2._44;

		return false;
	}
};

template <>
struct less<DirectX::SimpleMath::Plane> {
	bool operator()(const DirectX::SimpleMath::Plane& p1, const DirectX::SimpleMath::Plane& p2) const noexcept {
		return ((p1.x < p2.x) || ((p1.x == p2.x) && (p1.y < p2.y)) || ((p1.x == p2.x) && (p1.y == p2.y) && (p1.z < p2.z)) || ((p1.x == p2.x) && (p1.y == p2.y) && (p1.z == p2.z) && (p1.w < p2.w)));
	}
};

template <>
struct less<DirectX::SimpleMath::Quaternion> {
	bool operator()(const DirectX::SimpleMath::Quaternion& q1, const DirectX::SimpleMath::Quaternion& q2) const noexcept {
		return ((q1.x < q2.x) || ((q1.x == q2.x) && (q1.y < q2.y)) || ((q1.x == q2.x) && (q1.y == q2.y) && (q1.z < q2.z)) || ((q1.x == q2.x) && (q1.y == q2.y) && (q1.z == q2.z) && (q1.w < q2.w)));
	}
};

template <>
struct less<DirectX::SimpleMath::Color> {
	bool operator()(const DirectX::SimpleMath::Color& c1, const DirectX::SimpleMath::Color& c2) const noexcept {
		return ((c1.x < c2.x) || ((c1.x == c2.x) && (c1.y < c2.y)) || ((c1.x == c2.x) && (c1.y == c2.y) && (c1.z < c2.z)) || ((c1.x == c2.x) && (c1.y == c2.y) && (c1.z == c2.z) && (c1.w < c2.w)));
	}
};

template <>
struct less<DirectX::SimpleMath::Ray> {
	bool operator()(const DirectX::SimpleMath::Ray& r1, const DirectX::SimpleMath::Ray& r2) const noexcept {
		if (r1.position.x != r2.position.x)
			return r1.position.x < r2.position.x;
		if (r1.position.y != r2.position.y)
			return r1.position.y < r2.position.y;
		if (r1.position.z != r2.position.z)
			return r1.position.z < r2.position.z;

		if (r1.direction.x != r2.direction.x)
			return r1.direction.x < r2.direction.x;
		if (r1.direction.y != r2.direction.y)
			return r1.direction.y < r2.direction.y;
		if (r1.direction.z != r2.direction.z)
			return r1.direction.z < r2.direction.z;

		return false;
	}
};

template <>
struct less<DirectX::SimpleMath::Viewport> {
	bool operator()(const DirectX::SimpleMath::Viewport& vp1, const DirectX::SimpleMath::Viewport& vp2) const noexcept {
		if (vp1.x != vp2.x)
			return (vp1.x < vp2.x);
		if (vp1.y != vp2.y)
			return (vp1.y < vp2.y);

		if (vp1.width != vp2.width)
			return (vp1.width < vp2.width);
		if (vp1.height != vp2.height)
			return (vp1.height < vp2.height);

		if (vp1.min_depth != vp2.min_depth)
			return (vp1.min_depth < vp2.min_depth);
		if (vp1.max_depth != vp2.max_depth)
			return (vp1.max_depth < vp2.max_depth);

		return false;
	}
};

} // namespace std

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#if defined(DIRECTX_TOOLKIT_IMPORT) && defined(_MSC_VER)
#pragma warning(pop)
#endif

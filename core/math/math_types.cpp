#include "color.h"
#include "matrix.h"
#include "quaternion.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"

// Out-of-line so each constant has one definition, and so a header include does
// not drag a static initializer into every translation unit that uses one.
namespace feather {

const Vector2f Vector2f::zero { 0.0f, 0.0f };
const Vector2f Vector2f::one { 1.0f, 1.0f };
const Vector2f Vector2f::right { 1.0f, 0.0f };
const Vector2f Vector2f::left { -1.0f, 0.0f };
const Vector2f Vector2f::up { 0.0f, 1.0f };
const Vector2f Vector2f::down { 0.0f, -1.0f };

const Vector2d Vector2d::zero { 0.0, 0.0 };
const Vector2d Vector2d::one { 1.0, 1.0 };
const Vector2d Vector2d::right { 1.0, 0.0 };
const Vector2d Vector2d::left { -1.0, 0.0 };
const Vector2d Vector2d::up { 0.0, 1.0 };
const Vector2d Vector2d::down { 0.0, -1.0 };

// Right-handed: +X right, +Y up, -Z forward.
const Vector3f Vector3f::zero { 0.0f, 0.0f, 0.0f };
const Vector3f Vector3f::one { 1.0f, 1.0f, 1.0f };
const Vector3f Vector3f::right { 1.0f, 0.0f, 0.0f };
const Vector3f Vector3f::left { -1.0f, 0.0f, 0.0f };
const Vector3f Vector3f::up { 0.0f, 1.0f, 0.0f };
const Vector3f Vector3f::down { 0.0f, -1.0f, 0.0f };
const Vector3f Vector3f::forward { 0.0f, 0.0f, -1.0f };
const Vector3f Vector3f::backward { 0.0f, 0.0f, 1.0f };

const Vector3d Vector3d::zero { 0.0, 0.0, 0.0 };
const Vector3d Vector3d::one { 1.0, 1.0, 1.0 };
const Vector3d Vector3d::right { 1.0, 0.0, 0.0 };
const Vector3d Vector3d::left { -1.0, 0.0, 0.0 };
const Vector3d Vector3d::up { 0.0, 1.0, 0.0 };
const Vector3d Vector3d::down { 0.0, -1.0, 0.0 };
const Vector3d Vector3d::forward { 0.0, 0.0, -1.0 };
const Vector3d Vector3d::backward { 0.0, 0.0, 1.0 };

const Vector4f Vector4f::zero { 0.0f, 0.0f, 0.0f, 0.0f };
const Vector4f Vector4f::one { 1.0f, 1.0f, 1.0f, 1.0f };

const Vector4d Vector4d::zero { 0.0, 0.0, 0.0, 0.0 };
const Vector4d Vector4d::one { 1.0, 1.0, 1.0, 1.0 };

const Quaternionf Quaternionf::identity { 0.0f, 0.0f, 0.0f, 1.0f };
const Quaterniond Quaterniond::identity { 0.0, 0.0, 0.0, 1.0 };

const Colorf Colorf::black { 0.0f, 0.0f, 0.0f, 1.0f };
const Colorf Colorf::white { 1.0f, 1.0f, 1.0f, 1.0f };
const Colorf Colorf::red { 1.0f, 0.0f, 0.0f, 1.0f };
const Colorf Colorf::green { 0.0f, 1.0f, 0.0f, 1.0f };
const Colorf Colorf::blue { 0.0f, 0.0f, 1.0f, 1.0f };
const Colorf Colorf::transparent { 0.0f, 0.0f, 0.0f, 0.0f };

const Matrix4x4f Matrix4x4f::identity {};

} //namespace feather

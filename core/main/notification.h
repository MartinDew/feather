#pragma once
#include <map>

namespace feather {

enum class Notification : uint32_t {
	NONE = 0,
	WINDOW_SHOWN,
	WINDOW_RESIZED,
	COUNT
};

} //namespace feather
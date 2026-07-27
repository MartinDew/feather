#pragma once

#include "container_utils.h"
#include "static_indexed_array.h"

#include <functional>
#include <ranges>

namespace feather {

template <class... Args>
class Delegate {
public:
	using DelegateFuncType = std::function<void(Args...)>;
	StaticIndexedArray<DelegateFuncType> listeners;

	using id_t = int32_t;

public:
	id_t subscribe(const DelegateFuncType& callback) { return listeners.add(callback); }

	void execute(const Args&... args) {
		if (listeners.empty())
			return;
		for (auto&& l : listeners) {
			l(std::forward<Args>(args)...);
		}
	}

	void remove(id_t id) {
		if (id < 0 || !listeners.has_value(id))
			return;

		listeners.remove(id);
	}

	void clear() { listeners.clear(); }
};

} // namespace feather
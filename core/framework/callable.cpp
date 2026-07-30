#include "callable.h"

using namespace std::literals;

namespace feather {

Callable::Callable() : _internal_func { nullptr }, _param_amount(0) {
}

Variant Callable::call(std::span<Variant> params) {
	if (!is_valid()) {
		std::println(std::cout, "Attempting to call an invalid Callable");
		return {};
	}

	if (params.size() != _param_amount) {
		fassert(false,
				std::format("Callable called with incorrect number of parameters. Expected {} and got {}",
							_param_amount,
							params.size()));
	}
	return _internal_func(params);
}

bool Callable::is_valid() const {
	return _internal_func != nullptr;
}

} //namespace feather

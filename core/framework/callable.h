#pragma once

#include "assert.h"
#include "variant.h"

#include <cstdint>
#include <functional>
#include <span>
#include <tuple>
#include <type_traits>

namespace feather {

class FEATHER_API Callable {
	std::function<Variant(std::span<Variant>)> _internal_func;
	uint8_t _param_amount;

public:
	Callable();
	template <class TRet, class... TArgs>
		requires(VariantCompatible<std::decay_t<TRet>>) && (VariantCompatible<std::decay_t<TArgs>> && ...)
	Callable(std::function<TRet(TArgs...)> func)
			: _internal_func { [func](std::span<Variant> params) {
				size_t i = 0;
				std::tuple<std::decay_t<TArgs>...> converted_args = {
					params[i++].as<std::decay_t<TArgs>>().value()...
				};
				if constexpr (std::is_void_v<TRet>) {
					std::apply(func, converted_args);
					return Variant();
				}
				else {
					TRet result = std::apply(func, converted_args);
					return Variant(std::move(result));
				}
			} }
			, _param_amount { sizeof...(TArgs) } {}

	template <class TRet, class... TArgs>
		requires(VariantCompatible<std::decay_t<TRet>>) && (VariantCompatible<std::decay_t<TArgs>> && ...)
	Callable(TRet (*func_ptr)(TArgs...)) : Callable(std::function<TRet(TArgs...)>(func_ptr)) {}

	Callable(Callable&&) = default;
	Callable(const Callable&) = default;
	Callable& operator=(const Callable&) = default;
	Callable& operator=(Callable&&) = default;

	Variant call(std::span<Variant> params);

	Variant call(auto&&... args) {
		if constexpr (sizeof...(args) == 0) {
			return call(std::span<Variant>());
		}
		else {
			Variant params[] = { args... };
			return call(std::span<Variant>(params, sizeof...(args)));
		}
	}

	bool is_valid() const;
};

} // namespace feather
#pragma once

// Split out from extension_abi.h (which must stay pure C, includable with no
// linkage on either side of the plugin boundary): computing a build
// fingerprint needs sizeof(std::string) and friends, which pulls in the STL.
// This header is still safe to include from both the engine and a project
// DLL -- everything in it is constexpr/inline, so it never crosses the
// plugin ABI itself.

#include <cstdint>
#include <string>

namespace feather {

// A cheap, deterministic signal of ABI-affecting build config differences
// between the engine and a plugin -- not a proof of full compatibility
// (decision 7's shared-runtime-mode requirement is what actually guarantees
// that), just enough to turn the common "debug plugin against a release
// engine" mistake into a readable rejection at load time instead of the
// silent heap corruption that mistake produced before. sizeof(std::string)
// alone already catches most STL debug-iterator/allocator layout mismatches;
// the MSVC-specific bits below catch the rest on the platform where CRT mode
// mismatches were actually observed in CI.
constexpr uint32_t compute_build_fingerprint() {
	uint32_t fp = 0;
	fp = fp * 31u + static_cast<uint32_t>(sizeof(std::string));
#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL)
	fp = fp * 31u + static_cast<uint32_t>(_ITERATOR_DEBUG_LEVEL);
#endif
#if defined(_DEBUG)
	fp = fp * 31u + 1u;
#endif
	return fp;
}

} // namespace feather

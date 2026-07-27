#pragma once

#include "resource.h"
#include <cstdint>
#include <string>
#include <vector>

#ifndef FEATHER_REFLECTION_PARSER
#include "texture.gen.h"
#endif

namespace feather {

enum class TextureFormat : uint8_t {
	INVALID = 0,
	R8_UNORM,
	RG8_UNORM,
	RGBA8_UNORM,
	RGBA8_SRGB,
	R16_FLOAT,
	RG16_FLOAT,
	RGBA16_FLOAT,
	R32_FLOAT,
	RG32_FLOAT,
	RGBA32_FLOAT,
};

class Texture : public Resource {
	FCLASS();

protected:
	std::vector<uint8_t> _pixel_data;
	[[get(public), set(public)]]
	uint32_t _width = 0;
	[[get(public), set(public)]]
	uint32_t _height = 0;
	TextureFormat _format = TextureFormat::RGBA8_UNORM;
	bool _is_loaded = false;

public:
	Texture() = default;

	// File loading
	bool load_from_file();

	// Direct data setting (for procedural textures)
	void set_data(const std::string& path, const std::vector<uint8_t>& data, uint32_t width, uint32_t height,
			TextureFormat format);

	// Getters
	const std::vector<uint8_t>& get_pixel_data() const { return _pixel_data; }
	TextureFormat get_format() const { return _format; }
	bool is_loaded() const { return _is_loaded; }

	// Get bytes per pixel for format
	static uint32_t get_bytes_per_pixel(TextureFormat format);
};

} //namespace feather

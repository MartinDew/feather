#include "texture.h"
#include <core/framework/variant.h>
#include <main/class_db.h>

// TODO: Add image loading
// For now, basic implementation without actual file loading

namespace feather {

bool Texture::load_from_file() {
	if (get_path().empty()) {
		return false;
	}

	_is_loaded = false;
	return false;
}

void Texture::set_data(
		const std::string& path,
		const std::vector<uint8_t>& data,
		uint32_t width,
		uint32_t height,
		TextureFormat format
) {
	_pixel_data = data;
	_width = width;
	_height = height;
	_format = format;
	_is_loaded = true;
	set_path(path);
}

uint32_t Texture::get_bytes_per_pixel(TextureFormat format) {
	switch (format) {
	case TextureFormat::R8_UNORM:
		return 1;
	case TextureFormat::RG8_UNORM:
		return 2;
	case TextureFormat::RGBA8_UNORM:
	case TextureFormat::RGBA8_SRGB:
		return 4;
	case TextureFormat::R16_FLOAT:
		return 2;
	case TextureFormat::RG16_FLOAT:
		return 4;
	case TextureFormat::RGBA16_FLOAT:
		return 8;
	case TextureFormat::R32_FLOAT:
		return 4;
	case TextureFormat::RG32_FLOAT:
		return 8;
	case TextureFormat::RGBA32_FLOAT:
		return 16;
	default:
		return 0;
	}
}

} //namespace feather

#include "texture_format_loader.h"
#include "texture.h"

namespace feather {

bool TextureFormatLoader::recognize_extension(const std::string& extension) const {
	return extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "tga" || extension == "bmp";
}

std::shared_ptr<Resource> TextureFormatLoader::instantiate(const Path& path) {
	auto texture = std::make_shared<Texture>();
	texture->set_path(path.string());
	return texture;
}

void TextureFormatLoader::load(std::shared_ptr<Resource> resource, const Path& path) {
	std::static_pointer_cast<Texture>(resource)->load_from_file();
}

} // namespace feather

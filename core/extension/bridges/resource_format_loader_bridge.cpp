#include "resource_format_loader_bridge.h"

#include <framework/assert.h>
#include <framework/reflection_utils.h>
#include <resources/resource.h>

namespace feather {

ExtensionResourceFormatLoader::ExtensionResourceFormatLoader(const FeatherExtensionClassInfo& info) : _info(info) {
	_plugin_instance = _info.create_instance ? _info.create_instance(_info.class_userdata, this) : nullptr;
	if (_info.get_virtual) {
		_recognize_extension_fn = _info.get_virtual(_info.class_userdata, "recognize_extension");
		_instantiate_fn = _info.get_virtual(_info.class_userdata, "instantiate");
		_load_fn = _info.get_virtual(_info.class_userdata, "load");
		_requires_immediate_load_fn = _info.get_virtual(_info.class_userdata, "requires_immediate_load");
	}
}

ExtensionResourceFormatLoader::~ExtensionResourceFormatLoader() {
	if (_info.free_instance) {
		_info.free_instance(_info.class_userdata, _plugin_instance);
	}
}

bool ExtensionResourceFormatLoader::recognize_extension(const std::string& extension) const {
	if (!_recognize_extension_fn)
		return false;
	auto fn = reinterpret_cast<FeatherVirtualResourceFormatLoaderRecognizeExtension>(_recognize_extension_fn);
	return fn(_plugin_instance, extension.c_str()) != 0;
}

std::shared_ptr<Resource> ExtensionResourceFormatLoader::instantiate(const Path& path) {
	if (!_instantiate_fn)
		return nullptr;
	auto fn = reinterpret_cast<FeatherVirtualResourceFormatLoaderInstantiate>(_instantiate_fn);
	FeatherObjectPtr raw = fn(_plugin_instance, path.string().c_str());
	if (!raw)
		return nullptr;

	// The plugin must have obtained `raw` from object_create() -- verify
	// rather than trust, since a misbehaving plugin could hand back anything.
	Resource* res = object_cast<Resource>(static_cast<Reflected*>(raw));
	fassert(res, "ResourceFormatLoader plugin virtual 'instantiate' returned a non-Resource object");
	return res ? std::shared_ptr<Resource>(res) : nullptr;
}

void ExtensionResourceFormatLoader::load(std::shared_ptr<Resource> resource, const Path& path) {
	if (!_load_fn)
		return;
	auto fn = reinterpret_cast<FeatherVirtualResourceFormatLoaderLoad>(_load_fn);
	fn(_plugin_instance, resource.get(), path.string().c_str());
}

bool ExtensionResourceFormatLoader::requires_immediate_load() const {
	if (!_requires_immediate_load_fn)
		return false;
	auto fn = reinterpret_cast<FeatherVirtualResourceFormatLoaderRequiresImmediateLoad>(_requires_immediate_load_fn);
	return fn(_plugin_instance) != 0;
}

} // namespace feather

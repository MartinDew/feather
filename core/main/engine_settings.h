// Example usage:
/*
#include <memory>
#include <string>

// Forward declarations
class Renderer;
class OpenGLRenderer;
class VulkanRenderer;

// Static registration anywhere in your codebase
namespace {
	// Simple settings - automatically registered
	feather::Setting<int> max_entities("max_entities", 1000);
	feather::Setting<float> tick_rate("tick_rate", 60.0f);
	feather::Setting<bool> vsync("vsync", true);

	// Resolver setting - automatically registered
	feather::ResolverSetting<std::string, std::unique_ptr<Renderer>>
		renderer_setting("renderer", "opengl");
}

// Register resolvers in initialization code
void init_settings() {
	renderer_setting.register_resolver("opengl", []() {
		return std::make_unique<OpenGLRenderer>();
	});

	renderer_setting.register_resolver("vulkan", []() {
		return std::make_unique<VulkanRenderer>();
	});
}

// Usage examples
void game_loop() {
	// Direct access
	int max = max_entities.get();

	// Assignment
	max_entities = 2000;

	// Implicit conversion
	if (max_entities > 1500) {
		// ...
	}

	// Resolver usage
	auto renderer = renderer_setting.resolve();
	// or
	auto renderer2 = renderer_setting();

	// Change renderer type
	renderer_setting.set_key("vulkan");
	auto vulkan_renderer = renderer_setting();
}
*/
#pragma once

#include <framework/assert.h>
#include <framework/static_string.hpp>
#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace feather {

// Forward declaration
class EngineSettings;

// Base interface for type erasure
class ISettingStorage {
public:
	virtual ~ISettingStorage() = default;
};

// Simple setting that auto-registers itself
template <typename T>
class Setting {
private:
	struct Storage : ISettingStorage {
		T value;
		explicit Storage(T val) : value(std::move(val)) {}
	};

	std::string_view _name;
	Storage* storage_;

public:
	Setting(std::string_view name, T default_val = {}) : _name(name) {
		auto storage = std::make_unique<Storage>(std::move(default_val));
		storage_ = storage.get();
		register_self(std::move(storage));
	}

	T& get() { return storage_->value; }
	const T& get() const { return storage_->value; }
	void set(T new_val) { storage_->value = std::move(new_val); }

	// Implicit conversion for convenient usage
	operator T&() { return get(); }
	operator const T&() const { return get(); }

	Setting& operator=(T new_val) {
		set(std::move(new_val));
		return *this;
	}

private:
	void register_self(std::unique_ptr<ISettingStorage> storage);
};

// Setting with resolver that auto-registers itself
template <typename TKey, typename TResult>
class ResolverSetting {
private:
	using ResolverFunc = std::function<TResult()>;

	struct Storage : ISettingStorage {
		TKey current_key;
		std::unordered_map<TKey, ResolverFunc> resolvers;

		explicit Storage(TKey key) : current_key(std::move(key)) {}
	};

	std::string_view _name {};
	Storage* _storage;

public:
	ResolverSetting(std::string_view name, TKey default_key = {}) : _name(name) {
		auto storage = std::make_unique<Storage>(std::move(default_key));
		_storage = storage.get();
		register_self(std::move(storage));
	}

	// Register a resolver function for a specific key
	void register_resolver(TKey key, ResolverFunc func) { _storage->resolvers[std::move(key)] = std::move(func); }

	// Set current key
	void set_key(TKey key) {
		fassert(_storage->resolvers.contains(key), "Key not registered");
		_storage->current_key = std::move(key);
	}

	// Get current key
	const TKey& get_key() const { return _storage->current_key; }

	// Resolve and create object based on current key
	TResult resolve() const {
		auto it = _storage->resolvers.find(_storage->current_key);
		if (it == _storage->resolvers.end()) {
			fassert(false, "No resolver for current key");
		}
		return it->second();
	}

	// Resolve with specific key
	TResult resolve(const TKey& key) const {
		auto it = _storage->resolvers.find(key);
		if (it == _storage->resolvers.end()) {
			fassert(false, "Key not registered");
		}
		return it->second();
	}

	// Convenient call operator
	TResult operator()() const { return resolve(); }
	TResult operator()(const TKey& key) const { return resolve(key); }

private:
	void register_self(std::unique_ptr<ISettingStorage> storage);
};

class EngineSettings {
	static EngineSettings* _instance;
	std::unordered_map<std::string_view, std::unique_ptr<ISettingStorage>> _settings;

	template <typename T>
	friend class Setting;

	template <typename TKey, typename TResult>
	friend class ResolverSetting;

	// Private registration methods called by Setting/ResolverSetting constructors
	static void _register_setting(std::string_view name, std::unique_ptr<ISettingStorage> storage);

	EngineSettings();

public:
	// Check if setting exists
	static bool has_setting(std::string_view name);

	// Get count of registered settings
	static size_t setting_count();

	static EngineSettings& get();
};

// Implementation of registration methods
template <typename T>
void Setting<T>::register_self(std::unique_ptr<ISettingStorage> storage) {
	EngineSettings::_register_setting(_name, std::move(storage));
}

template <typename TKey, typename TResult>
void ResolverSetting<TKey, TResult>::register_self(std::unique_ptr<ISettingStorage> storage) {
	EngineSettings::_register_setting(_name, std::move(storage));
}

#define REGISTRATION_SCOPE_BEGIN                                                                                       \
	struct REGISTRATION_SCOPE {                                                                                        \
		REGISTRATION_SCOPE() {
#define REGISTRATION_SCOPE_END                                                                                         \
	}                                                                                                                  \
	}                                                                                                                  \
	REGISTRATION_SCOPE_instance;

} // namespace feather

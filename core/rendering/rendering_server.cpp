#include "rendering_server.h"
#include "null_renderer.h"
#include "renderer.h"
#include <main/engine.h>
#include <main/notification.h>
#include <resources/shader.h>

#include <framework/assert.h>
#include <framework/variant.h>
#include <main/class_db.h>
#include <main/launch_settings.h>
#include <world/components/light.h>
#include <framework/static_string.hpp>

#include <string_view>

namespace feather {

RenderingServer* RenderingServer::_instance = nullptr;

void RenderingServer::_run() {
	_render_thread = std::jthread(bind_method(&RenderingServer::_render_function, this));
}

void RenderingServer::_render_function() {
	while (true) {
		bool stop_requested;
		{
			std::unique_lock lock(_wait_mutex);
			_wait_cv.wait(lock, [this] {
				return _dirty.load(std::memory_order_acquire) || _render_thread.get_stop_token().stop_requested();
			});
			_dirty.store(false, std::memory_order_release);
			stop_requested = _render_thread.get_stop_token().stop_requested();
		}

		if (stop_requested) {
			break;
		}

		if (_needs_resize.load(std::memory_order_relaxed)) {
			_renderer->_on_resize();
			_needs_resize.store(false, std::memory_order_relaxed);
		}

		RenderScene scene;
		{
			std::lock_guard lock(_write_lock);
			int read_idx = 1 - _write_idx.load(std::memory_order_relaxed);
			scene = _buffers[read_idx]; // O(1) COW copy
		}

		_renderer->_render_scene(scene);
	}
}

RenderingServer::RenderingServer() {
	fassert(!_instance);
	_instance = this;
}

RenderingServer::~RenderingServer() {
	_render_thread.request_stop();
	_wait_cv.notify_all();
}

void RenderingServer::init() {
	// Use a null renderer in headless mode, otherwise use the renderer specified in the launch settings.
	// Eventually, it will be required to look into booting a renderer even if headless to support compute shaders among
	// other things.
	const std::string_view renderer_name = Engine::is_headless()
			? std::string_view { NullRenderer::get_class_static() }
			: std::string_view { LaunchSettings::get().renderer.Get() };

	_renderer = ClassDB::create_object<Renderer>(renderer_name);
	fassert(_renderer.get(), std::format("Failed to create renderer of type {}", renderer_name));

	Engine::get().get_main_window().register_notification(Notification::WINDOW_RESIZED, [&flag = _needs_resize] {
		flag.store(true, std::memory_order_relaxed);
	});

	// A render thread whose only job is to call a no-op isn't worth spawning.
	if (!Engine::is_headless() && !LaunchSettings::get().force_single_thread.Get())
		_run();
}

void RenderingServer::update(double dt) const {
	fassert(_renderer.get(), "no renderer set");
}

void RenderingServer::stop() {
	_render_thread.request_stop();
	if (_render_thread.joinable())
		_render_thread.join();
}

void RenderingServer::begin_scene_frame() {
	std::lock_guard lock(_write_lock);
	_buffers[_write_idx.load(std::memory_order_relaxed)].clear();
}

void RenderingServer::set_camera_transform(const Transform& transform) {
	std::lock_guard lock(_write_lock);
	_buffers[_write_idx.load(std::memory_order_relaxed)].set_camera_transform(transform);
}

void RenderingServer::set_camera_projection(const Projection& projection) {
	std::lock_guard lock(_write_lock);
	_buffers[_write_idx.load(std::memory_order_relaxed)].set_camera_projection(projection);
}

void RenderingServer::set_environment(const RenderScene::EnvironmentSettings& env) {
	std::lock_guard lock(_write_lock);
	_buffers[_write_idx.load(std::memory_order_relaxed)].set_environment(env);
}

void RenderingServer::add_entity(const RenderScene::EntityRender& entity) {
	std::lock_guard lock(_write_lock);
	_buffers[_write_idx.load(std::memory_order_relaxed)].add_entity(entity);
}

void RenderingServer::add_light(const Light& light) {
	std::lock_guard lock(_write_lock);
	_buffers[_write_idx.load(std::memory_order_relaxed)].add_light(light);
}

void RenderingServer::commit_scene_frame() {
	{
		std::lock_guard lock(_write_lock);
		int old_write = _write_idx.load(std::memory_order_relaxed);
		_write_idx.store(1 - old_write, std::memory_order_release);
		_dirty.store(true, std::memory_order_release);
	}

	if (LaunchSettings::get().force_single_thread.Get()) {
		int read_idx = 1 - _write_idx.load(std::memory_order_acquire);
		_renderer->_render_scene(_buffers[read_idx]);
		_dirty.store(false, std::memory_order_relaxed);
	}
	else {
		_wait_cv.notify_one();
	}
}

void RenderingServer::use_renderer(std::string_view name) {
	_renderer = ClassDB::create_object<Renderer>(name);
	fassert(_renderer.get(), std::format("Failed to create renderer of type {}", name));
}

void RenderingServer::compile_shader(const std::shared_ptr<Shader>& shader) {
	fassert(_renderer.get(), "no renderer set");
	_renderer->_compile_shader(*shader);
}

RenderingServer* RenderingServer::get() {
	fassert(_instance, "instance not yet initialized for RenderingServer");
	return _instance;
}

} //namespace feather

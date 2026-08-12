#include "engine.h"
#include "launch_settings.h"
#include "world/components/light.h"
#include "world/components/scene.h"
#include "world/ecs_defs.h"
#include "world/rendering_world_feature.h"

#include <framework/assert.h>
#include <resources/extension_registry.h>
#include <resources/resource_loader.h>

#include <chrono>
#include <csignal>

#include <rendering/rendering_server.h>
#include <resources/mesh.h>

namespace feather {

namespace {

// Headless has no window, so SDL never delivers SDL_EVENT_WINDOW_CLOSE_REQUESTED
// and the main loop would otherwise have no exit condition at all.
// volatile sig_atomic_t is the only type a signal handler may portably touch.
volatile std::sig_atomic_t g_quit_requested = 0;

void _on_terminate_signal(int) {
	g_quit_requested = 1;
}

} //namespace

Engine* Engine::_instance = nullptr;

Engine::Engine() {
	// Todo replace sdl assert by custom one
	fassert(!_instance);

	_instance = this;
	_rendering_server.init();
}

Engine::~Engine() {
	// unregister_modules();
}

// --demo-mode lets the engine run with no project loaded at all (see
// feather_main.cpp) -- this is what proves that path still works end to end.
// Entity/component setup goes through the same opaque ecs::WorldHandle/
// EntityHandle API a project DLL is limited to (see core/world/ecs_api.h),
// so this exercises exactly what a plugin can do. The system registration
// and debug query at the bottom stay flecs-native via ecs_defs.h's unwrap():
// this file is engine-internal, never compiled into a plugin, so it isn't
// bound by the plugin ABI firewall ecs_api.h exists for.
#if BETA
inline void _setup_demo_scene(WorldSim& world_sim) {
	ecs::WorldHandle world = world_sim.ecs_world();
	ecs::EntityHandle scene = world_sim.current_scene_handle();

	auto material = std::make_shared<PBRMaterial>();
	material->set_base_color_factor({ .7f, .7f, .0f });

	struct Move {};

	auto box1 = ecs::create_child_entity(world, scene, "Box1");
	ecs::entity_emplace<Transform>(world, box1, Vector3 { 0, -1, -3 }, Quaternion::create_from_yaw_pitch_roll(1.f, 0, 0), Vector3::one);
	ecs::entity_emplace<MeshInstance>(world, box1, std::make_shared<BoxMesh>());
	ecs::entity_emplace<MaterialInstance>(world, box1, material);
	unwrap(box1).add<Move>();

	auto box2 = ecs::create_child_entity(world, scene, "Box2");
	ecs::entity_emplace<Transform>(world, box2, Vector3 { -2, -1, -3 }, Quaternion::create_from_yaw_pitch_roll(1.f, 0, 0), Vector3::one);
	ecs::entity_emplace<MeshInstance>(world, box2, std::make_shared<BoxMesh>());
	ecs::entity_emplace<MaterialInstance>(world, box2, material);
	unwrap(box2).add<Move>();

	auto box3 = ecs::create_child_entity(world, scene, "Box3");
	ecs::entity_emplace<Transform>(world, box3, Vector3 { 2, -1, -3 }, Quaternion::create_from_yaw_pitch_roll(1.f, 0, 0), Vector3::one);
	ecs::entity_emplace<MeshInstance>(world, box3, std::make_shared<BoxMesh>());
	ecs::entity_emplace<MaterialInstance>(world, box3, material);
	unwrap(box3).add<Move>();

	// Parented to Box2 rather than the scene -- exercises hierarchy
	// independent of scene membership.
	auto box_child = ecs::create_entity(world, "BoxChild");
	ecs::entity_emplace<Transform>(world, box_child, Vector3 { 0, 2, -3 }, Quaternion::create_from_yaw_pitch_roll(1.f, 0, 0), Vector3::one);
	ecs::entity_emplace<MeshInstance>(world, box_child, std::make_shared<BoxMesh>());
	ecs::entity_child_of(box_child, box2);

	auto floor = ecs::create_child_entity(world, scene, "Floor");
	ecs::entity_emplace<Transform>(
			world, floor,
			Vector3 { 0, -2, 0 },
			Quaternion::create_from_yaw_pitch_roll({ 0, 0, 0 }),
			Vector3 { 200, 0.1f, 200 }
	);
	ecs::entity_emplace<MeshInstance>(world, floor, std::make_shared<BoxMesh>());

	Light l { .type = LightType::Directional,
			  .position = Vector3::zero,
			  .direction = Vector3 { -0.5f, -1.0f, -1.f },
			  .color = Color(1.0f, 1.0f, 1.0f, 1.0f),
			  .intensity = 10.0f,
			  .cast_shadows = true };
	auto sun = ecs::create_child_entity(world, scene, "Directional");
	ecs::entity_emplace<Light>(world, sun, std::move(l));

	flecs::world w = unwrap(world);

	w.system<const MeshInstance, Transform>("Spin")
			.with<Move>()
			.kind(flecs::OnUpdate)
			.write<Transform>()
			.each([](flecs::iter& it, size_t, const MeshInstance& mi, Transform& t) {
				t.rotation = t.rotation *
						Quaternion::create_from_yaw_pitch_roll(Vector3 { 0, static_cast<real_t>(it.delta_time()), 0 });
			});

	auto q = w.query_builder<Transform, MeshInstance, MaterialInstance*>("Test")
					 .with<ActiveScene>()
					 .optional()
					 .parent()
					 .cascade()
					 .build();

	q.each([](Entity e, Transform& t, MeshInstance mi, MaterialInstance* mat) { std::cout << e.name() << std::endl; });
}
#endif

bool Engine::run() {
	auto current_time = start_time;

	bool keep_running = true;

	// initialization
	//
	// index_project() only DISCOVERS resources -- including dlopen'ing every
	// project DLL and reading (not yet calling into) its descriptor -- so
	// that a project's own reflected types (and format loaders, etc.) exist
	// before any extension's initialize() runs, regardless of which order
	// the directory walk happened to visit them in. activate_all() is what
	// actually runs those extensions' initialize() hooks. A second
	// index_project() pass picks up anything whose loader only just
	// registered as a result (e.g. a plugin-defined ResourceFormatLoader
	// subclass, like feather-example-project's GameSettingsFormatLoader,
	// recognizing project.gamecfg for the first time) -- cheap, since
	// index_project() already skips any path already in _path_cache.
	ResourceLoader::get()->index_project();
	ExtensionRegistry::get()->activate_all();
	ResourceLoader::get()->index_project();

	// Debug stuff. Must come after both index_project() passes above and
	// activate_all(): together, those are what dlopen a project's extension
	// DLL and run its entry point, which is where a project's own reflected
	// types (and format loaders, etc.) register themselves with ClassDB.
	// Dumping before it would silently omit every project-defined type.
	if (LaunchSettings::get().dump_db.Get()) {
		ClassDB::get()->print_db();
		return true;
	}

	_world_sim.init();

#if BETA
	if (LaunchSettings::get().demo_mode.Get()) {
		_setup_demo_scene(_world_sim);
	}
#endif

	// Installed in every mode, not just headless: Ctrl+C on a terminal-launched
	// windowed session should shut down through the normal teardown too.
	std::signal(SIGINT, &_on_terminate_signal);
	std::signal(SIGTERM, &_on_terminate_signal);

	// update
	double accumulator = 0.0;
	while (keep_running && !g_quit_requested) {
		keep_running = _main_window.update();

		auto new_time = Clock::now();

		double frame_time = std::chrono::duration_cast<std::chrono::duration<double>>(new_time - current_time).count();
		current_time = new_time;

		accumulator += frame_time;
		_current_dt = simulation_time;
		while (accumulator >= simulation_time) {
			accumulator -= simulation_time;
			_world_sim.fixed_update(simulation_time);
		}

		_current_dt = frame_time;
		_world_sim.update(frame_time);

		// Tell the renderer to render here
		_rendering_server.update(frame_time);
	}

	// Join the render thread while every Engine member is still alive. Member
	// destruction order (engine.h) otherwise tears down _world_sim and
	// _main_window before the jthread destructor gets to join.
	_rendering_server.stop();

	return true;
}

double Engine::get_current_delta_time() const {
	return _current_dt;
}

bool Engine::is_editor() {
	return LaunchSettings::get().editor_mode.Get();
}

bool Engine::is_headless() {
	// Cached rather than re-read like is_editor(): this is queried from Window's
	// constructor and from the render path, and the flag is a string. Safe to
	// call before Engine::_instance is set -- it only touches LaunchSettings,
	// which Main constructs first (feather_main.cpp).
	static const bool headless = [] {
		const std::string& mode = LaunchSettings::get().windowed.Get();
		if (mode == "headless")
			return true;

		fassert(mode == "windowed",
				std::format("Unknown window mode '{}' (expected 'windowed' or 'headless')", mode));
		return false;
	}();

	return headless;
}
} //namespace feather
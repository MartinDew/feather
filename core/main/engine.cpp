#include "engine.h"
#include "launch_settings.h"
#include "world/components/light.h"
#include "world/rendering_world_module.h"

#include <framework/assert.h>
#include <resources/resource_loader.h>

#include <chrono>
#include <csignal>

#include <rendering/rendering_server.h>
#include <resources/mesh.h>

namespace feather {

namespace {

// Headless has no window which in turn can't deliver quit events.
// this is the only exit condition. sig_atomic_t is the only type a signal
// handler may portably touch.
volatile std::sig_atomic_t quit_requested = 0;

void _on_terminate_signal(int) {
	quit_requested = 1;
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

#if BETA
inline void _setup_demo_scene(WorldSim& _world_sim) {
	// test script
	auto w = *_world_sim.get_world();
	Transform t1 { { 0, -1, -3 }, Quaternion::create_from_yaw_pitch_roll(1.f, 0, 0), Vector3::one };
	Transform t2 { { -2, -1, -3 }, Quaternion::create_from_yaw_pitch_roll(1.f, 0, 0), Vector3::one };
	Transform t3 { { 2, -1, -3 }, Quaternion::create_from_yaw_pitch_roll(1.f, 0, 0), Vector3::one };
	Transform t4 { { 0, 2, -3 }, Quaternion::create_from_yaw_pitch_roll(1.f, 0, 0), Vector3::one };

	auto material = std::make_shared<PBRMaterial>();
	material->set_base_color_factor({ .7f, .7f, .0f });

	struct Move {};
	auto s = _world_sim.create_scene("Ni");
	_world_sim.set_active_scene(s);

	auto _ = _world_sim.create_entity(s, "Box1")
					 .emplace<Transform>(t1)
					 .emplace<MeshInstance>(std::make_shared<BoxMesh>())
					 .emplace<MaterialInstance>(material)
					 .add<Move>();

	_ = _world_sim.create_entity(s, "Box2")
				.emplace<Transform>(t2)
				.emplace<MeshInstance>(std::make_shared<BoxMesh>())
				.emplace<MaterialInstance>(material)
				.add<Move>();

	w.entity(s, "Box3")
			.emplace<Transform>(t3)
			.emplace<MeshInstance>(std::make_shared<BoxMesh>())
			.emplace<MaterialInstance>(material)
			.add<Move>();

	w.entity("BoxChild").emplace<Transform>(t4).emplace<MeshInstance>(std::make_shared<BoxMesh>()).child_of(_);

	_world_sim.create_entity(s, "Floor")
			.emplace<Transform>(
					Vector3 { 0, -2, 0 },
					Quaternion::create_from_yaw_pitch_roll({ 0, 0, 0 }),
					Vector3 { 200, 0.1f, 200 }
			)
			.emplace<MeshInstance>(std::make_shared<BoxMesh>());

	auto dir = Vector3 { -0.5f, -1.0f, -1.f };
	Light l { .type = LightType::Directional,
			  .position = Vector3::zero,
			  .direction = dir,
			  .color = Color(1.0f, 1.0f, 1.0f, 1.0f),
			  .intensity = 10.0f,
			  .cast_shadows = true };
	w.entity(s, "Directional").emplace<Light>(std::move(l));

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
	ResourceLoader::get()->index_project();

#if BETA
	if (LaunchSettings::get().dump_db.Get()) {
		ClassDB::get()->print_db();
		return true;
	}
#endif

	_world_sim.init();

#if BETA
	if (LaunchSettings::get().demo_mode.Get()) {
		_setup_demo_scene(_world_sim);
	}
#endif

	// Installed in every mode: Ctrl+C on a windowed session should also shut
	// down through normal teardown, not just headless.
	std::signal(SIGINT, &_on_terminate_signal);
	std::signal(SIGTERM, &_on_terminate_signal);

	// update
	double accumulator = 0.0;
	while (keep_running && !quit_requested) {
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

	_rendering_server.stop();

	ResourceLoader::get()->unload();

	return true;
}

double Engine::get_current_delta_time() const {
	return _current_dt;
}

bool Engine::is_editor() {
	if constexpr (!EDITOR_BUILD)
		return false;
	else
		return LaunchSettings::get().editor_mode.Get();
}

bool Engine::is_headless() {
	static const bool headless = [] {
		const std::string& mode = LaunchSettings::get().windowed.Get();
		if (mode == "headless")
			return true;

		fassert(mode == "windowed", std::format("Unknown window mode '{}' (expected 'windowed' or 'headless')", mode));
		return false;
	}();

	return headless;
}

} //namespace feather

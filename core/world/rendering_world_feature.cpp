#include "rendering_world_feature.h"

#include "components/scene.h"
#include <main/world_sim.h>
#include <rendering/rendering_server.h>
#include <resources/mesh.h>
#include <resources/resource_loader.h>
#include <world/components/light.h>

namespace feather {

void RenderingWorldFeature::_load_module(WorldSim* sim) {
	sim->get_world()->import <Type>();
}

inline void _begin_render_scene(const flecs::iter& it) {
	auto* rs = RenderingServer::get();
	rs->begin_scene_frame();
	rs->set_camera_projection(Projection::create_perspective_fov(90.0f, 16.0f / 9.0f, 0.1f, 1000.0f));
	rs->set_camera_transform({});
}

inline void _update_meshes(Entity e, Transform transform, MeshInstance& mesh, MaterialInstance* mat) {
	RenderingServer::get()->add_entity({ transform, mesh.mesh->get_mesh_data(), mat ? mat->material : nullptr });
}

RenderingWorldFeature::RenderingWorldFeature(World world) {
	std::println("importing module {} ", get_class_static());
	world.module<Type>();

	world.component<MeshInstance>("MeshInstance");
	world.component<MaterialInstance>("MaterialInstance");
	world.component<Light>("Light");

	world.system("Begin Render Scene").kind(flecs::PreStore).run(&_begin_render_scene);

	world.system<Transform, MeshInstance, MaterialInstance*>("Fill Render Scene")
			.with<ActiveScene>()
			.up()
			.kind(flecs::PreStore)
			.multi_threaded(false)
			.each(_update_meshes);

	world.system<const Light>("Fill lights")
			.kind(flecs::PreStore)
			.with<ActiveScene>()
			.up()
			.each([](Entity e, const Light& light) {
				RenderingServer::get()->add_light(light);
			});

	world.system("Commit Render Scene").kind(flecs::OnStore).run([](const flecs::iter&) {
		RenderingServer::get()->commit_scene_frame();
	});
}

} //namespace feather

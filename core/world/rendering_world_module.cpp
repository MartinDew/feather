#include "rendering_world_module.h"

#include "components/scene.h"
#include <main/world_sim.h>
#include <rendering/rendering_server.h>
#include <resources/mesh.h>
#include <resources/resource_loader.h>
#include <world/components/light.h>

namespace feather {

void RenderingWorldModule::_begin_render_scene(const Ecs::iter& it) {
	auto* rs = RenderingServer::get();
	rs->begin_scene_frame();
	rs->set_camera_projection(Projection::create_perspective_fov(90.0f, 16.0f / 9.0f, 0.1f, 1000.0f));
	rs->set_camera_transform({});
}

void RenderingWorldModule::_update_meshes(Entity e, Transform transform, MeshInstance& mesh, MaterialInstance* mat) {
	RenderingServer::get()->add_entity({ transform, mesh.mesh->get_mesh_data(), mat ? mat->material : nullptr });
}

RenderingWorldModule::RenderingWorldModule(World& world) {
	std::println("importing module {} ", get_class_static());
	system<>(world, "Begin Render Scene").kind(flecs::PreStore).run(&_begin_render_scene);

	system<Transform, MeshInstance, MaterialInstance*>(world, "Fill Render Scene")
			.with<ActiveScene>()
			.up()
			.kind(flecs::PreStore)
			.multi_threaded(false)
			.each(_update_meshes);

	system<const Light>(world, "Fill lights")
			.kind(flecs::PreStore)
			.with<ActiveScene>()
			.up()
			.each([](Entity e, const Light& light) { RenderingServer::get()->add_light(light); });

	system<>(world, "Commit Render Scene").kind(flecs::OnStore).run([](const flecs::iter&) {
		RenderingServer::get()->commit_scene_frame();
	});
}

} //namespace feather

#include "asset/navigation_bake.h"
#include "gameplay/navigation_mesh.h"
#include "gameplay/navigation_system.h"
#include "scene/scene_serializer.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/systems/physics_system.h"

namespace Comet::NavigationBake {
    Result<Input, Error> capture(Scene& scene, const ComponentRegistry& components,
        const EntityUuid uuid, PhysicsService::MeshSource meshes) {
        using Captured = Result<Input, Error>;
        const auto entity = scene.find_entity(uuid);
        if(scene.component_count<NavigationRegionComponent>() != 1 || !entity
            || !entity.has_component<NavigationRegionComponent>())
            return Captured::failure(
                {"Navigation baking requires the scene's single Navigation Region"});
        const auto settings = entity.get_component<NavigationRegionComponent>();
        const Math::Vec3 center(scene.get_world_matrix(entity)[3]);
        auto content = SceneSerializer(components).clone(scene);
        if(!content)
            return Captured::failure({content.error()});
        PhysicsService physics(std::move(meshes));
        SceneRuntime runtime;
        if(auto services = runtime.set_services({.physics = &physics}); !services)
            return Captured::failure(services.error());
        if(auto added = runtime.add_system(std::make_unique<PhysicsSystem>(physics)); !added)
            return Captured::failure(added.error());
        if(auto started = runtime.start(*content.value()); !started)
            return Captured::failure(started.error());
        auto geometry = physics.static_triangles(
            center - settings.half_extents, center + settings.half_extents);
        if(!geometry)
            return Captured::failure(geometry.error());
        auto links = Gameplay::capture_navigation_links(scene);
        if(!links)
            return Captured::failure({links.error()});
        const auto fingerprint = Gameplay::NavigationMesh::input_fingerprint(
            geometry.value(), center, settings, links.value());
        if(!fingerprint)
            return Captured::failure({fingerprint.error()});
        if(auto stopped = runtime.stop(); !stopped)
            return Captured::failure(stopped.error());
        return Captured::success({std::move(geometry).value(), center, settings,
            fingerprint.value(), std::move(links).value()});
    }

    Result<NavigationData> bake(const Input& input) {
        Gameplay::NavigationMesh mesh;
        if(auto built = mesh.build(input.triangles, input.center, input.settings, input.links);
            !built)
            return Result<NavigationData>::failure(built.error());
        return mesh.product(input.fingerprint);
    }
}

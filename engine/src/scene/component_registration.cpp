#include "scene/component_registration.h"
#include "scene/component_changes.h"
#include "scene/script_component.h"
#include "scene/prefab.h"

namespace Comet {
    void ComponentRegistration::core(ComponentRegistry& registry) {
        add(registry, describe_component<ScriptComponent>("script", "Script")
                          .field(COMET_FIELD(ScriptComponent, asset), "Script",
                              {.asset_type = AssetType::Script})
                          .field(COMET_FIELD(ScriptComponent, parameters), "Parameters")
                          .build());

        // 名称已由 .scene 单独保存，避免重复序列化。
        add(registry, describe_component<NameComponent>("name", "Name", false)
                          .field(COMET_FIELD(NameComponent, name), "Name")
                          .build());

        add(registry, describe_component<TransformComponent>("transform", "Transform")
                          .field(COMET_FIELD(TransformComponent, translation), "Translation")
                          .field(COMET_FIELD(TransformComponent, rotation), "Rotation",
                              {.numeric = {.speed = 1.0f}},
                              [](Math::Vec3& rotation) { rotation = Math::wrap_degrees(rotation); })
                          .field(COMET_FIELD(TransformComponent, scale), "Scale")
                          .build());
        registry.add_scene_references(
            [](Scene& scene, AssetReferenceScope scope, auto& references) {
                if(scope != AssetReferenceScope::All)
                    return;
                scene.each<const PrefabInstanceComponent>([&](Entity, const auto& instance) {
                    references.push_back({instance.asset, instance.source_type, false});
                });
            });
    }
    void ComponentChangeAccess::transform(Scene& scene, Entity entity, bool structure) {
        scene.mark_transform_dirty(entity.m_handle);
        scene.mark_physics_dirty(entity.m_handle);
        if(structure) {
            scene.reset_transform_interpolation(entity.m_handle);
            scene.m_render_structure_changed = true;
            scene.m_physics_structure_changed = true;
        }
    }

}

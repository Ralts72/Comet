#pragma once

#include "asset/content_fwd.h"

#include "asset/reference.h"
#include "common/error.h"
#include "reflection/type.h"
#include "common/export.h"
#include "scene/scene.h"

#include <any>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Comet {
    enum class AssetReferenceScope { All, Runtime, Resident };
    struct ComponentDescriptor: TypeDescriptor {
        uint64_t type_id = 0; // 进程内类型标识，不写入场景文件。
        bool serializable = true;
        std::function<bool(const void*, AssetReferenceScope)> include_references;
        std::function<bool(const Entity&)> has_component_callback;
        std::function<void(Entity&)> add_component_callback;
        std::function<void(Entity&)> remove_component_callback;
        std::function<bool(Entity&, const PropertyDescriptor&, const PropertyValue&,
            PropertyDescriptor::WriteMode)>
            assign_property_callback;
        std::function<const void*(const Entity&)> const_component_accessor;
        std::function<std::any(const Entity&)> capture_component_callback;
        std::function<bool(Entity&, const std::any&)> restore_component_callback;

        [[nodiscard]] COMET_API std::any capture_component(const Entity& entity) const;
        [[nodiscard]] COMET_API bool restore_component(
            Entity& entity, const std::any& snapshot) const;

        [[nodiscard]] bool has_component(const Entity& entity) const {
            return has_component_callback && has_component_callback(entity);
        }

        [[nodiscard]] bool add_component(Entity& entity) const {
            if(!entity || has_component(entity) || !add_component_callback) {
                return false;
            }
            add_component_callback(entity);
            return has_component(entity);
        }

        [[nodiscard]] bool remove_component(Entity& entity) const {
            if(!entity || !has_component(entity) || !remove_component_callback) {
                return false;
            }
            remove_component_callback(entity);
            return !has_component(entity);
        }

        [[nodiscard]] COMET_API bool assign_property(Entity entity, std::string_view property_id,
            const PropertyValue& value,
            PropertyDescriptor::WriteMode mode = PropertyDescriptor::WriteMode::Edit) const;

        [[nodiscard]] const void* get_component(const Entity& entity) const {
            if(!has_component(entity) || !const_component_accessor) {
                return nullptr;
            }
            return const_component_accessor(entity);
        }
    };

    // 组件类型与属性元信息；组件实例由 Scene 持有。
    class COMET_API ComponentRegistry {
    public:
        using ReferenceScope = AssetReferenceScope;
        using SceneReferences =
            std::function<void(Scene&, ReferenceScope, std::vector<AssetReference>&)>;
        void add_scene_references(SceneReferences collect) {
            m_scene_references.push_back(std::move(collect));
        }
        void set_reference_filter(
            std::string_view id, std::function<bool(const void*, ReferenceScope)> filter);

        [[nodiscard]] bool register_component(ComponentDescriptor descriptor);

        [[nodiscard]] const ComponentDescriptor* find_component(
            std::string_view component_id) const;
        [[nodiscard]] bool covers_entity(const Entity& entity) const;
        [[nodiscard]] std::vector<AssetReference> collect_asset_references(
            Scene& scene, ReferenceScope scope = ReferenceScope::All) const;

        [[nodiscard]] const std::vector<ComponentDescriptor>& components() const {
            return m_components;
        }

    private:
        std::vector<ComponentDescriptor> m_components;
        std::vector<SceneReferences> m_scene_references;
    };

    [[nodiscard]] COMET_API ComponentRegistry create_scene_component_registry();
}

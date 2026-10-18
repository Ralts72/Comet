#pragma once

#include "common/json.h"
#include "asset/metadata.h"
#include "reflection/type.h"
#include "scene/entity_id.h"
#include "scene/scene_settings.h"
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Comet {
    class ComponentRegistry;
    struct ComponentDescriptor;
}

// 文件编码的私有中间值；不承载活动 Scene、导入器或资源管理器。
namespace Comet::SceneFormat {
    struct PropertyRecord {
        const PropertyDescriptor* descriptor = nullptr;
        PropertyValue value;
    };

    struct ComponentRecord {
        const ComponentDescriptor* descriptor = nullptr;
        std::vector<PropertyRecord> properties;
    };

    struct PrefabRecord;
    struct EntityRecord {
        EntityUuid uuid;
        std::optional<EntityUuid> parent;
        std::string name;
        std::vector<ComponentRecord> components;
        std::shared_ptr<PrefabRecord> prefab;
    };

    struct InstanceChange {
        EntityRecord base, value;
        bool inherit_prefab = false;
    };
    struct PrefabRecord {
        AssetHandle asset;
        std::unordered_map<EntityUuid, EntityUuid> entities;
        std::vector<EntityRecord> base;
        AssetType source_type = AssetType::Prefab;
        bool compact = false;
        std::vector<InstanceChange> changes;
        std::unordered_set<EntityUuid> removed;
        std::vector<EntityRecord> added;
    };

    using ChildrenIndex = std::unordered_map<EntityUuid, std::vector<std::size_t>>;
    Result<VolumetricFogSettings> read_volumetric_fog(Json::Node, const Json::Context&);
    void write_volumetric_fog(Json::Writer&, const VolumetricFogSettings&);
    Result<PropertyValue> copy_property_value(
        const PropertyDescriptor&, const void*, const Json::Context&, std::string_view);
    std::string entity_location(std::size_t);
    Result<void> read_entity_tree(Json::Node, std::optional<EntityUuid>, const std::string&,
        const Json::Context&, const ComponentRegistry&, std::vector<EntityRecord>&,
        std::unordered_set<EntityUuid>&, std::size_t depth, std::size_t prefab_depth = 0);
    Result<void> validate_records(
        const std::vector<EntityRecord>&, const Json::Context&, std::size_t prefab_depth = 0);
    Result<void> write_scene_tree(Json::Writer&, const EntityRecord&,
        const std::vector<EntityRecord>&, const ChildrenIndex&, std::size_t depth, bool referenced,
        const std::unordered_set<EntityUuid>* excluded = nullptr);
    EntityRecord remap_record(EntityRecord, const std::unordered_map<EntityUuid, EntityUuid>&);
}

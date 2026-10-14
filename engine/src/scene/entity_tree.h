#pragma once

#include "scene/component_registry.h"
#include <span>
#include <unordered_map>

namespace Comet {
    struct PrefabInstanceComponent;
}

namespace Comet::EntityTrees {
    struct EntitySnapshot {
        struct Component {
            std::string id;
            std::any value;
        };
        EntityUuid uuid;
        EntityUuid parent;
        std::string name;
        std::vector<Component> components;
        std::shared_ptr<const PrefabInstanceComponent> prefab;
    };
    using Snapshot = std::vector<EntitySnapshot>;
    using ReferenceRemap = std::unordered_map<EntityUuid, EntityUuid>;

    [[nodiscard]] COMET_API std::optional<Snapshot> capture(
        Scene& scene, const ComponentRegistry& registry, Entity root);
    // 合并重叠子树；返回顺序保持选择顺序，父子同时选择时只保留父树。
    [[nodiscard]] COMET_API std::optional<Snapshot> capture(
        Scene& scene, const ComponentRegistry& registry, std::span<const EntityUuid> entities);
    [[nodiscard]] COMET_API std::vector<EntityUuid> roots(const Snapshot& snapshots);
    [[nodiscard]] COMET_API std::optional<std::vector<EntityUuid>> roots(
        Scene& scene, std::span<const EntityUuid> entities);
    // 恢复原身份用于撤销；remap 只转换组件内的实体引用。
    [[nodiscard]] COMET_API bool restore(Scene& scene, const ComponentRegistry& registry,
        const Snapshot& snapshots, const ReferenceRemap& remap = {});
    // 为每次复制或实例化分配新身份，子树外的引用保持原值。
    // parent 为 nullopt 时保留各根原父级；显式 UUID（含空值）统一重设父级。
    [[nodiscard]] COMET_API std::optional<ReferenceRemap> make_instance_ids(Snapshot& snapshots,
        Scene& scene, std::optional<EntityUuid> parent = EntityUuid{},
        EntityUuid reserved_root = {});
    [[nodiscard]] COMET_API bool has_only_internal_references(
        Scene& scene, const ComponentRegistry& registry, const Snapshot& snapshots);
}

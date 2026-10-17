#pragma once

#include "asset/content_fwd.h"

#include "scene/entity_tree.h"
#include <filesystem>
#include <functional>

namespace Comet {
    struct ModelData;
    struct PrefabSourceUpdate;
    // 可重复实例化的子树内容；不持有源 Scene 或运行态。
    class COMET_API Prefab final {
    public:
        [[nodiscard]] static Result<Prefab> capture(
            Scene& scene, const ComponentRegistry& registry, Entity root);
        [[nodiscard]] static Result<Prefab> from_model(const ModelData& model, AssetHandle source,
            const ComponentRegistry& registry, const std::string& name);
        [[nodiscard]] static Result<Prefab> deserialize(std::string_view contents,
            const ComponentRegistry& registry, std::string_view source = "<prefab>");
        [[nodiscard]] static Result<Prefab> load(
            const std::filesystem::path& path, const ComponentRegistry& registry);
        using SourceLoader = std::function<Result<Prefab>(AssetHandle)>;
        // 在独立候选中刷新内层来源，保留覆盖；循环／冲突不发布部分结果。
        [[nodiscard]] Result<Prefab> resolve_nested(
            const ComponentRegistry& registry, AssetHandle source, const SourceLoader& load) const;
        [[nodiscard]] Result<std::string> serialize(const ComponentRegistry& registry) const;
        [[nodiscard]] Result<void> save(
            const std::filesystem::path& path, const ComponentRegistry& registry) const;
        // source 为空时生成纯运行内容；提供来源时保留各层编辑关联。
        [[nodiscard]] Result<Entity> instantiate(Scene& scene, const ComponentRegistry& registry,
            std::optional<Math::Vec3> root_position = std::nullopt, EntityUuid parent = {},
            AssetHandle source = {}, AssetType source_type = AssetType::Prefab,
            EntityUuid reserved_root = {}) const;
        enum class UpdateMode { KeepOverrides, RevertOverrides };
        // 生成完整更新候选；源身份复用，冲突时不修改场景。
        [[nodiscard]] Result<EntityTrees::Snapshot> update_candidate(Scene& scene,
            const ComponentRegistry& registry, Entity instance_root,
            UpdateMode mode = UpdateMode::KeepOverrides) const;
        [[nodiscard]] Result<PrefabSourceUpdate> prepare_source_update(
            Scene& scene, const ComponentRegistry& registry, Entity instance_root) const;
        [[nodiscard]] EntityUuid root_uuid() const { return m_entities.front().uuid; }
        [[nodiscard]] std::size_t entity_count() const { return m_entities.size(); }

    private:
        friend class SceneSerializer;
        static EntityUuid member_uuid(EntityUuid instance, EntityUuid source);
        explicit Prefab(EntityTrees::Snapshot entities) : m_entities(std::move(entities)) {}
        EntityTrees::Snapshot m_entities;
    };
    struct PrefabSourceUpdate {
        Prefab source;
        EntityTrees::Snapshot instance;
    };

    // 仅实例根保存来源和对应关系。基线是不可变内容，用于区分源修改与实例覆盖。
    struct COMET_API PrefabInstanceComponent {
        AssetHandle asset;
        std::shared_ptr<const Prefab> base;
        EntityTrees::ReferenceRemap entities;
        AssetType source_type = AssetType::Prefab;
    };
}

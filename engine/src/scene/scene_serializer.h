#pragma once

#include "common/export.h"
#include "common/result.h"
#include "asset/handle.h"
#include "asset/content_fwd.h"
#include <functional>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace Comet {
    enum class AssetType : std::uint8_t;
    class ComponentRegistry;
    class Scene;

    class COMET_API SceneSerializer final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 3;
        static constexpr std::size_t MAX_HIERARCHY_DEPTH = 128;
        static constexpr std::size_t MAX_PREFAB_DEPTH = 16;

        using Sources =
            std::function<Result<std::shared_ptr<const Prefab>>(AssetHandle, AssetType)>;
        // 项目宿主提供来源读取；无来源时保存独立内容，供子树资产和运行产品使用。
        explicit SceneSerializer(const ComponentRegistry& component_registry, Sources sources = {});

        [[nodiscard]] Result<std::string> serialize(const Scene& scene) const;
        // 子树资产与场景共用实体格式，不携带关卡环境或后处理。
        [[nodiscard]] Result<std::string> serialize_entities(const Scene& scene) const;

        [[nodiscard]] Result<std::unique_ptr<Scene>> deserialize(
            std::string_view contents, std::string_view source = "<memory>") const;

        [[nodiscard]] Result<std::unique_ptr<Scene>> clone(const Scene& scene) const;

        [[nodiscard]] Result<void> save(const Scene& scene, const std::string& path) const;

        [[nodiscard]] Result<std::unique_ptr<Scene>> load(const std::string& path) const;

    private:
        struct ContentSnapshot;
        [[nodiscard]] Result<std::string> serialize_content(
            const ContentSnapshot& content, bool include_scene_settings) const;
        [[nodiscard]] Result<ContentSnapshot> capture_content(
            const Scene& scene, std::size_t prefab_depth = 0) const;
        [[nodiscard]] Result<std::unique_ptr<Scene>> restore_content(const ContentSnapshot& content,
            std::string_view source, std::size_t prefab_depth = 0) const;

        [[nodiscard]] Result<void> expand_instances(
            ContentSnapshot&, std::string_view source) const;
        const ComponentRegistry& m_component_registry;
        Sources m_sources;
    };
}

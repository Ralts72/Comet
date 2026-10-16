#pragma once

#include "asset/database.h"
#include "asset/data/import_input_snapshot.h"
#include "common/result.h"
#include "render/scene/render_scene.h"
#include "scene/entity_uuid.h"

#include <map>
#include <optional>
#include <string>

namespace Comet {
    class Scene;
}

namespace CometEditor::LightingBakeInput {
    enum class Purpose { Reflection, Irradiance };
    using SourceCache = std::map<std::filesystem::path, Comet::ImportInputSnapshot>;

    struct Snapshot {
        Comet::RenderScene scene;
        std::string fingerprint;
        std::vector<Comet::AssetHandle> dependencies;
    };

    [[nodiscard]] Comet::Result<Snapshot> describe(Comet::Scene& scene,
        const Comet::AssetDatabase& assets, Purpose purpose, uint64_t shader_fingerprint = 0,
        SourceCache* source_cache = nullptr);

    struct StatusKey {
        Comet::EntityUuid entity;
        uint64_t lifetime;
        uint64_t scene_revision;
        uint64_t database_generation;
        uint64_t asset_revision;
        uint64_t program_version;
        bool operator==(const StatusKey&) const = default;
    };

    // 仅状态展示复用文件指纹；显式开始和结果接纳始终重新读取源字节。
    class StatusCache {
    public:
        [[nodiscard]] const std::string* find(const StatusKey& key) const;
        [[nodiscard]] SourceCache& sources(uint64_t database_generation);
        void store(StatusKey key, std::string text);
        void clear();

    private:
        std::optional<std::pair<StatusKey, std::string>> m_status;
        uint64_t m_source_generation = 0;
        SourceCache m_sources;
    };
}

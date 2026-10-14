#pragma once

#include "asset/handle.h"
#include "common/parameters.h"
#include "common/result.h"
#include "gameplay/game_session.h"
#include <filesystem>
#include <optional>

namespace Comet {
    struct Checkpoint {
        AssetHandle scene;
        std::string marker;
        ParameterMap values;
    };

    // 单槽进度文件；只保存项目声明的值，不保存 ECS 或运行时对象。
    class COMET_API CheckpointStore final {
    public:
        CheckpointStore(Uuid project, std::filesystem::path path)
            : m_project(project), m_path(std::move(path)) {}
        [[nodiscard]] static Result<std::filesystem::path> default_path(Uuid project);
        [[nodiscard]] Result<void> save(const Checkpoint& checkpoint) const;
        [[nodiscard]] Result<std::optional<Checkpoint>> load() const;
        // 宿主提供场景身份查询，存档层不依赖资产数据库；store 须活到会话结束。
        [[nodiscard]] GameSession::Persistence persistence(
            std::function<AssetHandle(std::string_view)> find_scene,
            std::function<std::optional<std::string>(AssetHandle)> scene_path) const;

    private:
        Uuid m_project;
        std::filesystem::path m_path;
    };
}

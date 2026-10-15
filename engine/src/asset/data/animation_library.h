#pragma once
#include "animation/animation_data.h"
#include "asset/handle.h"
#include <filesystem>
#include <map>

namespace Comet {
    // 独立动作目录；源网格仅提供骨架与片段，运行时不创建其 GPU 网格。
    struct COMET_API AnimationLibrary {
        struct Entry {
            std::string name;
            AssetHandle mesh;
            std::string clip;
            bool operator==(const Entry&) const = default;
        };
        std::vector<Entry> entries;
        std::map<AssetHandle, std::shared_ptr<const AnimationData>> sources;
        [[nodiscard]] Result<void> validate() const;
        [[nodiscard]] Result<AnimationData> bind(const AnimationData& target) const;
        [[nodiscard]] static Result<AnimationLibrary> deserialize(
            std::string_view contents, std::string_view source = {});
        [[nodiscard]] static Result<AnimationLibrary> load(const std::filesystem::path& path);
        [[nodiscard]] Result<std::string> serialize() const;
        [[nodiscard]] Result<void> save(const std::filesystem::path& path) const;
    };
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Comet {
    struct ImportInputFingerprint {
        std::filesystem::path relative_path;
        std::uint64_t size = 0;
        std::uint64_t hash = 0;

        bool operator==(const ImportInputFingerprint&) const noexcept = default;
    };

    struct ImportInputSnapshot {
        std::vector<ImportInputFingerprint> files;

        // 源文件在首项，其余依赖按规范化的项目相对路径排序且不重复。
        [[nodiscard]] bool valid() const {
            if(files.empty())
                return false;
            std::string previous_dependency;
            for(std::size_t index = 0; index < files.size(); ++index) {
                const auto& path = files[index].relative_path;
                if(path.empty() || path.is_absolute() || path != path.lexically_normal())
                    return false;
                for(const auto& part : path)
                    if(part == "..")
                        return false;
                if(index == 0)
                    continue;
                auto dependency = path.generic_string();
                if(path == files.front().relative_path
                    || (index > 1 && dependency <= previous_dependency))
                    return false;
                previous_dependency = std::move(dependency);
            }
            return true;
        }

        bool operator==(const ImportInputSnapshot&) const noexcept = default;
    };
}

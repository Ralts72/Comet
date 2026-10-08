#pragma once

#include "common/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Comet {
    namespace ScriptSource {
        inline constexpr std::size_t MAX_SOURCE_BYTES = 1024 * 1024;
        inline constexpr std::size_t MAX_GROUP_BYTES = 8 * MAX_SOURCE_BYTES;
        inline constexpr std::size_t MAX_GROUP_FILES = 256;
        inline constexpr std::size_t MAX_GROUP_ROOTS = 128;
        bool is_module_path(const std::filesystem::path& path);
        bool safe_source_path(const std::filesystem::path& path);
        Result<std::filesystem::path> resolve_source(
            const std::filesystem::path& root, const std::filesystem::path& relative);
        Result<std::string> read_source(const std::filesystem::path& path);
    }

    struct ScriptSources {
        struct File {
            std::filesystem::path resolved;
            std::string source;
            std::string name;
        };
        struct Unreadable {
            std::filesystem::path resolved;
            std::filesystem::file_type type;
            std::optional<std::filesystem::file_time_type> write_time;
            std::optional<std::uintmax_t> size;
            bool read_failed = true;

            bool operator==(const Unreadable&) const = default;
        };
        std::filesystem::path root;
        std::map<std::filesystem::path, File> files;
        std::map<std::filesystem::path, Unreadable> unreadable;
        std::size_t bytes = 0;

        Result<const File*> capture(const std::filesystem::path& path,
            std::vector<std::filesystem::path>* dependencies = nullptr);
        bool inputs_are_current() const;

    private:
        static Unreadable inspect_unreadable(const std::filesystem::path& path);
    };
}

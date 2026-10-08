#include "asset/data/script_sources.h"

#include <algorithm>
#include <array>
#include <fstream>

namespace Comet {
    namespace ScriptSource {
        bool is_module_path(const std::filesystem::path& path) {
            auto name = path.filename().string();
            std::ranges::transform(name, name.begin(), [](const unsigned char value) {
                if(value >= 'A' && value <= 'Z')
                    return static_cast<char>(value + 'a' - 'A');
                return static_cast<char>(value);
            });
            return name.ends_with(".module.lua");
        }

        bool safe_source_path(const std::filesystem::path& path) {
            if(path.empty() || path.is_absolute() || path.has_root_name())
                return false;
            const auto text = path.generic_string();
            if(text.find('\0') != std::string::npos || text.find('\\') != std::string::npos)
                return false;
            for(const auto& part : path)
                if(part == "..")
                    return false;
            return path.lexically_normal() != ".";
        }

        Result<std::filesystem::path> resolve_source(
            const std::filesystem::path& root, const std::filesystem::path& relative) {
            if(!safe_source_path(relative))
                return Result<std::filesystem::path>::failure("Invalid project script path");
            std::error_code error;
            auto resolved = std::filesystem::weakly_canonical(root / relative, error);
            if(error)
                return Result<std::filesystem::path>::failure(
                    "Cannot resolve project script: " + relative.generic_string());
            const auto inside = resolved.lexically_relative(root);
            if(!safe_source_path(inside))
                return Result<std::filesystem::path>::failure(
                    "Script source is outside project assets: " + relative.generic_string());
            if(resolved != (root / relative).lexically_normal())
                return Result<std::filesystem::path>::failure(
                    "Script source symlink aliases are not supported: "
                    + relative.generic_string());
            return Result<std::filesystem::path>::success(std::move(resolved));
        }

        Result<std::string> read_source(const std::filesystem::path& path) {
            std::error_code error;
            if(!std::filesystem::is_regular_file(path, error) || error)
                return Result<std::string>::failure("Cannot read script: " + path.string());
            std::ifstream input(path, std::ios::binary);
            if(!input)
                return Result<std::string>::failure("Cannot read script: " + path.string());
            std::string source;
            std::array<char, 8192> buffer;
            while(input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto count = static_cast<std::size_t>(input.gcount());
                if(count > MAX_SOURCE_BYTES - source.size())
                    return Result<std::string>::failure(
                        "Script exceeds 1 MiB limit: " + path.string());
                source.append(buffer.data(), count);
            }
            if(!input.eof())
                return Result<std::string>::failure("Cannot read script: " + path.string());
            return Result<std::string>::success(std::move(source));
        }
    }

    using namespace ScriptSource;

    ScriptSources::Unreadable ScriptSources::inspect_unreadable(const std::filesystem::path& path) {
        std::error_code error;
        Unreadable result{path, std::filesystem::status(path, error).type(), {}, {}};
        error.clear();
        const auto time = std::filesystem::last_write_time(path, error);
        if(!error)
            result.write_time = time;
        error.clear();
        const auto size = std::filesystem::file_size(path, error);
        if(!error)
            result.size = size;
        return result;
    }

    Result<const ScriptSources::File*> ScriptSources::capture(
        const std::filesystem::path& path, std::vector<std::filesystem::path>* dependencies) {
        const auto existing = files.find(path);
        if(existing != files.end()) {
            if(dependencies && std::ranges::find(*dependencies, path) == dependencies->end())
                dependencies->push_back(path);
            return Result<const File*>::success(&existing->second);
        }
        auto resolved = resolve_source(root, path);
        if(!resolved)
            return Result<const File*>::failure(resolved.error());
        if(dependencies && std::ranges::find(*dependencies, path) == dependencies->end())
            dependencies->push_back(path);
        if(files.size() >= MAX_GROUP_FILES)
            return Result<const File*>::failure("Script group exceeds 256 source files");
        const auto observation = inspect_unreadable(resolved.value());
        auto source = read_source(resolved.value());
        if(!source) {
            unreadable.try_emplace(path, observation);
            return Result<const File*>::failure(source.error());
        }
        if(source.value().size() > MAX_GROUP_BYTES - bytes) {
            auto rejected = observation;
            rejected.read_failed = false;
            unreadable.try_emplace(path, std::move(rejected));
            return Result<const File*>::failure("Script group exceeds 8 MiB source limit");
        }
        bytes += source.value().size();
        const auto inserted =
            files.emplace(path, File{std::move(resolved).value(), std::move(source).value(),
                                    "@" + path.generic_string()});
        return Result<const File*>::success(&inserted.first->second);
    }

    bool ScriptSources::inputs_are_current() const {
        for(const auto& [path, file] : files) {
            const auto resolved = resolve_source(root, path);
            if(!resolved || resolved.value() != file.resolved)
                return false;
            const auto source = read_source(resolved.value());
            if(!source || source.value() != file.source)
                return false;
        }
        for(const auto& [path, observation] : unreadable) {
            const auto resolved = resolve_source(root, path);
            if(!resolved)
                return false;
            auto current = inspect_unreadable(resolved.value());
            current.read_failed = observation.read_failed;
            if(current != observation)
                return false;
            // 同样状态的不可读文件若已可读，也必须重试原候选。
            if(observation.read_failed && observation.size && *observation.size <= MAX_SOURCE_BYTES
                && observation.type == std::filesystem::file_type::regular
                && read_source(resolved.value()))
                return false;
        }
        return true;
    }
}

#include "project/recent_projects.h"

#include "common/file_io.h"
#include "common/json.h"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

namespace CometEditor {
    namespace {
        constexpr std::uint32_t FORMAT_VERSION = 1;
        constexpr std::size_t MAX_RECENT_PROJECTS = 10;
    }

    Comet::Result<std::filesystem::path> RecentProjects::default_storage_path() {
        using Result = Comet::Result<std::filesystem::path>;
#ifdef _WIN32
        const char* base = std::getenv("APPDATA");
        if(base && *base)
            return Result::success(std::filesystem::path(base) / "Comet/recent-projects.json");
        return Result::failure("APPDATA is not available for editor state");
#elif defined(__APPLE__)
        const char* base = std::getenv("HOME");
        if(base && *base)
            return Result::success(std::filesystem::path(base)
                                   / "Library/Application Support/Comet/recent-projects.json");
        return Result::failure("HOME is not available for editor state");
#else
        if(const char* state = std::getenv("XDG_STATE_HOME"); state && *state) {
            const std::filesystem::path directory(state);
            if(directory.is_absolute())
                return Result::success(directory / "comet/recent-projects.json");
        }
        const char* base = std::getenv("HOME");
        if(base && *base)
            return Result::success(
                std::filesystem::path(base) / ".local/state/comet/recent-projects.json");
        return Result::failure("HOME is not available for editor state");
#endif
    }

    Comet::Result<RecentProjects> RecentProjects::load(std::filesystem::path file) {
        using Result = Comet::Result<RecentProjects>;
        if(file.empty())
            return Result::failure("Recent projects path cannot be empty");
        RecentProjects recent(std::move(file));
        std::error_code error;
        const bool exists = std::filesystem::exists(recent.m_file, error);
        if(error)
            return Result::failure("Cannot inspect recent projects: " + error.message());
        if(!exists)
            return Result::success(std::move(recent));

        auto contents = Comet::read_text_file(recent.m_file);
        if(!contents)
            return Result::failure(contents.error());
        const Comet::Json::Context context("recent projects", recent.m_file.string());
        simdjson::dom::parser parser;
        auto parsed = context.parse(parser, contents.value());
        if(!parsed)
            return Result::failure(parsed.error());
        const auto root = parsed.value();
        if(auto valid = context.validate_keys(root, {"version", "projects"}); !valid)
            return Result::failure(valid.error());
        auto version = context.read_field<std::uint32_t>(root, "version", "an unsigned integer");
        if(!version)
            return Result::failure(version.error());
        if(version.value() != FORMAT_VERSION)
            return Result::failure(context.error("version", "unsupported version"));
        auto child = context.required_child(root, "projects");
        if(!child)
            return Result::failure(child.error());
        auto projects = context.array(child.value(), "projects");
        if(!projects)
            return Result::failure(projects.error());
        for(const auto entry : projects.value()) {
            auto text = context.read_scalar<std::string>(entry, "projects[]", "a path string");
            if(!text)
                return Result::failure(text.error());
            std::filesystem::path path(std::move(text).value());
            if(!path.is_absolute())
                return Result::failure(context.error("projects[]", "expected an absolute path"));
            path = path.lexically_normal();
            if(std::ranges::find(recent.m_entries, path) == recent.m_entries.end())
                recent.m_entries.push_back(std::move(path));
            if(recent.m_entries.size() == MAX_RECENT_PROJECTS)
                break;
        }
        return Result::success(std::move(recent));
    }

    Comet::Result<void> RecentProjects::record(const std::filesystem::path& root) {
        using Result = Comet::Result<void>;
        std::error_code error;
        auto canonical = std::filesystem::canonical(root, error);
        if(error || !std::filesystem::is_directory(canonical, error))
            return Result::failure("Cannot record project directory: "
                                   + (error ? error.message() : root.string()));
        if(!m_entries.empty() && m_entries.front() == canonical)
            return Result::success();
        std::vector<std::filesystem::path> candidate;
        candidate.reserve(MAX_RECENT_PROJECTS);
        candidate.push_back(std::move(canonical));
        for(const auto& entry : m_entries) {
            if(entry != candidate.front())
                candidate.push_back(entry);
            if(candidate.size() == MAX_RECENT_PROJECTS)
                break;
        }

        Comet::Json::Writer writer;
        writer.begin_object();
        writer.field("version", std::uint64_t(FORMAT_VERSION));
        writer.key("projects");
        writer.begin_array();
        for(const auto& entry : candidate)
            writer.value(entry.generic_string());
        writer.end_array();
        writer.end_object();
        auto contents = std::move(writer).finish();
        if(!contents)
            return Result::failure(contents.error());
        if(auto saved = Comet::write_text_file_atomic(m_file, contents.value()); !saved)
            return saved;
        m_entries = std::move(candidate);
        return Result::success();
    }
}

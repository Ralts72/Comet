#include "project/recent_projects.h"
#include "project/editor_paths.h"

#include "common/file_io.h"
#include "common/json.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

namespace CometEditor {
    namespace {
        constexpr std::uint32_t FORMAT_VERSION = 1;
        constexpr std::size_t MAX_RECENT_PROJECTS = 10;

        Comet::Result<std::vector<std::filesystem::path>> read_recent_projects(
            Comet::Json::Node root, const Comet::Json::Context& context) {
            using Result = Comet::Result<std::vector<std::filesystem::path>>;
            if(auto valid = context.validate_keys(root, {"version", "projects"}); !valid)
                return Result::failure(valid.error());
            auto version =
                context.read_field<std::uint32_t>(root, "version", "an unsigned integer");
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
            std::vector<std::filesystem::path> entries;
            entries.reserve(std::min(projects.value().size(), MAX_RECENT_PROJECTS));
            for(const auto entry : projects.value()) {
                auto text = context.read_scalar<std::string>(entry, "projects[]", "a path string");
                if(!text)
                    return Result::failure(text.error());
                std::filesystem::path path(std::move(text).value());
                if(!path.is_absolute())
                    return Result::failure(
                        context.error("projects[]", "expected an absolute path"));
                path = path.lexically_normal();
                if(std::ranges::find(entries, path) == entries.end())
                    entries.push_back(std::move(path));
                if(entries.size() == MAX_RECENT_PROJECTS)
                    break;
            }
            return Result::success(std::move(entries));
        }
    }

    Comet::Result<std::filesystem::path> RecentProjects::default_storage_path() {
        auto directory = editor_user_state_directory();
        if(!directory)
            return directory;
        return Comet::Result<std::filesystem::path>::success(
            directory.value() / "recent-projects.json");
    }

    Comet::Result<RecentProjects> RecentProjects::load(std::filesystem::path file) {
        using Result = Comet::Result<RecentProjects>;
        if(file.empty())
            return Result::failure("Recent projects path cannot be empty");
        auto loaded = Comet::Json::load_optional<std::vector<std::filesystem::path>>(
            "recent projects", file, read_recent_projects);
        if(!loaded)
            return Result::failure(loaded.error());
        RecentProjects recent(std::move(file));
        if(loaded.value())
            recent.m_entries = std::move(*loaded.value());
        return Result::success(std::move(recent));
    }

    Comet::Result<void> RecentProjects::record(const std::filesystem::path& root) {
        using Result = Comet::Result<void>;
        std::error_code error;
        auto canonical = std::filesystem::canonical(root, error);
        if(error || !std::filesystem::is_directory(canonical, error))
            return Result::failure(
                "Cannot record project directory: " + (error ? error.message() : root.string()));
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

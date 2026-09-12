#include "core/project.h"

#include "common/file_io.h"
#include "common/json.h"

#include <stdexcept>
#include <utility>

namespace Comet {
    Project::Project(ProjectPaths paths) : m_paths(std::move(paths)) {}

    Project Project::load(const std::filesystem::path& path) {
        if(path.empty())
            throw std::runtime_error("Project path cannot be empty");
        const auto manifest = std::filesystem::canonical(
            std::filesystem::is_directory(path) ? path / "project.json" : path);
        const std::string source = manifest.string();
        const Json::Context context("project", source);
        simdjson::dom::parser parser;
        const auto data = context.parse(parser, read_text_file(manifest));
        context.validate_keys(data, {"version", "name", "startup_scene"});
        const auto version = context.read_scalar<std::uint32_t>(
            context.required_child(data, "version"), "version", "an unsigned integer");
        if(version != FORMAT_VERSION)
            throw std::runtime_error(context.error(
                "version", "unsupported version " + std::to_string(version)
                               + "; expected " + std::to_string(FORMAT_VERSION)));

        Project project{ProjectPaths(manifest.parent_path())};
        project.m_name = context.read_scalar<std::string>(
            context.required_child(data, "name"), "name", "a non-empty string");
        if(project.m_name.find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::runtime_error(context.error("name", "name cannot be empty"));
        if(!std::filesystem::is_directory(project.paths().assets()))
            throw std::runtime_error(
                context.error("<root>", "assets directory does not exist"));

        Json::Node scene;
        if(!data["startup_scene"].get(scene)) {
            const std::filesystem::path relative =
                context.read_scalar<std::string>(scene, "startup_scene", "a string");
            if(!relative.empty()) {
                if(relative.is_absolute() || relative.extension() != ".scene")
                    throw std::runtime_error(context.error(
                        "startup_scene", "expected an assets-relative .scene path"));
                static_cast<void>(project.paths().resolve_asset_path(relative));
                project.m_startup_scene = relative.lexically_normal();
            }
        }
        return project;
    }
}

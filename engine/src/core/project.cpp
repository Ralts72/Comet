#include "core/project.h"

#include "common/file_io.h"
#include "common/json.h"

#include <system_error>
#include <utility>

namespace Comet {
    Project::Project(ProjectPaths paths) : m_paths(std::move(paths)) {}

    Result<Project> Project::load(const std::filesystem::path& path) {
        if(path.empty())
            return Result<Project>::failure("Project path cannot be empty");
        std::error_code error;
        const bool directory = std::filesystem::is_directory(path, error);
        if(error)
            return Result<Project>::failure(
                "Cannot inspect project path '" + path.string() + "': " + error.message());
        const auto manifest =
            std::filesystem::canonical(directory ? path / "project.json" : path, error);
        if(error)
            return Result<Project>::failure(
                "Cannot resolve project manifest '" + path.string() + "': " + error.message());
        const std::string source = manifest.string();
        const Json::Context context("project", source);
        simdjson::dom::parser parser;
        auto contents = read_text_file(manifest);
        if(!contents)
            return Result<Project>::failure(contents.error());
        try {
            const auto data = context.parse(parser, contents.value());
            context.validate_keys(data, {"version", "name", "startup_scene"});
            const auto version = context.read_scalar<std::uint32_t>(
                context.required_child(data, "version"), "version", "an unsigned integer");
            if(version != FORMAT_VERSION)
                return Result<Project>::failure(
                    context.error("version", "unsupported version " + std::to_string(version)
                                                 + "; expected " + std::to_string(FORMAT_VERSION)));

            Project project{ProjectPaths(manifest.parent_path())};
            project.m_name = context.read_scalar<std::string>(
                context.required_child(data, "name"), "name", "a non-empty string");
            if(project.m_name.find_first_not_of(" \t\r\n") == std::string::npos)
                return Result<Project>::failure(context.error("name", "name cannot be empty"));
            const bool has_assets = std::filesystem::is_directory(project.paths().assets(), error);
            if(error || !has_assets)
                return Result<Project>::failure(context.error(
                    "<root>", error ? error.message() : "assets directory does not exist"));

            Json::Node scene;
            if(!data["startup_scene"].get(scene)) {
                const std::filesystem::path relative =
                    context.read_scalar<std::string>(scene, "startup_scene", "a string");
                if(!relative.empty()) {
                    if(relative.is_absolute() || relative.extension() != ".scene")
                        return Result<Project>::failure(context.error(
                            "startup_scene", "expected an assets-relative .scene path"));
                    const auto resolved = project.paths().resolve_asset_path(relative);
                    if(!resolved)
                        return Result<Project>::failure(
                            context.error("startup_scene", resolved.error()));
                    project.m_startup_scene = relative.lexically_normal();
                }
            }
            return Result<Project>::success(std::move(project));
        } catch(const Json::Error& error) {
            return Result<Project>::failure(error.what());
        }
    }
}

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
        const auto parsed = context.parse(parser, contents.value());
        if(!parsed)
            return Result<Project>::failure(parsed.error());
        const auto data = parsed.value();
        if(auto valid = context.validate_keys(data, {"version", "name", "startup_scene"}); !valid)
            return Result<Project>::failure(valid.error());
        const auto version =
            context.read_field<std::uint32_t>(data, "version", "an unsigned integer");
        if(!version)
            return Result<Project>::failure(version.error());
        if(version.value() != FORMAT_VERSION)
            return Result<Project>::failure(
                context.error("version", "unsupported version " + std::to_string(version.value())
                                             + "; expected " + std::to_string(FORMAT_VERSION)));

        Project project{ProjectPaths(manifest.parent_path())};
        auto name = context.read_field<std::string>(data, "name", "a non-empty string");
        if(!name)
            return Result<Project>::failure(name.error());
        project.m_name = std::move(name).value();
        if(project.m_name.find_first_not_of(" \t\r\n") == std::string::npos)
            return Result<Project>::failure(context.error("name", "name cannot be empty"));
        const bool has_assets = std::filesystem::is_directory(project.paths().assets(), error);
        if(error || !has_assets)
            return Result<Project>::failure(context.error(
                "<root>", error ? error.message() : "assets directory does not exist"));

        Json::Node scene;
        if(!data["startup_scene"].get(scene)) {
            const auto scene_path =
                context.read_scalar<std::string>(scene, "startup_scene", "a string");
            if(!scene_path)
                return Result<Project>::failure(scene_path.error());
            const std::filesystem::path relative(scene_path.value());
            if(!relative.empty()) {
                if(relative.is_absolute() || relative.extension() != ".scene")
                    return Result<Project>::failure(
                        context.error("startup_scene", "expected an assets-relative .scene path"));
                const auto resolved = project.paths().resolve_asset_path(relative);
                if(!resolved)
                    return Result<Project>::failure(
                        context.error("startup_scene", resolved.error()));
                project.m_startup_scene = relative.lexically_normal();
            }
        }
        return Result<Project>::success(std::move(project));
    }
}

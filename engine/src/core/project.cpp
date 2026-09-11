#include "core/project.h"

#include "common/file_io.h"

#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <yaml-cpp/yaml.h>

namespace Comet {
    Project::Project(ProjectPaths paths) : m_paths(std::move(paths)) {}

    Project Project::load(const std::filesystem::path& path) {
        if(path.empty())
            throw std::runtime_error("Project path cannot be empty");
        const auto manifest = std::filesystem::canonical(
            std::filesystem::is_directory(path) ? path / "project.yaml" : path);
        try {
            const auto data = YAML::Load(read_text_file(manifest));
            if(!data.IsMap())
                throw std::runtime_error("expected a mapping");
            std::unordered_set<std::string> keys;
            for(const auto& entry : data) {
                const auto key = entry.first.as<std::string>();
                if(key != "version" && key != "name" && key != "startup_scene")
                    throw std::runtime_error("unknown field '" + key + "'");
                if(!keys.insert(key).second)
                    throw std::runtime_error("duplicate field '" + key + "'");
            }
            if(data["version"].as<int>() != 1)
                throw std::runtime_error("unsupported project version");

            Project project{ProjectPaths(manifest.parent_path())};
            if(!data["name"].IsScalar())
                throw std::runtime_error("name must be a nonempty string");
            project.m_name = data["name"].as<std::string>();
            if(project.m_name.find_first_not_of(" \t\r\n") == std::string::npos)
                throw std::runtime_error("name cannot be empty");
            if(!std::filesystem::is_directory(project.paths().assets()))
                throw std::runtime_error("assets directory does not exist");

            if(const auto scene = data["startup_scene"]) {
                const std::filesystem::path relative = scene.as<std::string>();
                if(!relative.empty()) {
                    if(relative.is_absolute() || relative.extension() != ".scene")
                        throw std::runtime_error(
                            "startup_scene must be an assets-relative .scene path");
                    static_cast<void>(project.paths().resolve_asset_path(relative));
                    project.m_startup_scene = relative.lexically_normal();
                }
            }
            return project;
        } catch(const std::exception& error) {
            throw std::runtime_error(
                "Invalid project '" + manifest.string() + "': " + error.what());
        }
    }
}

#include "project/project_creation.h"

#include "common/file_io.h"
#include "common/json.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"

#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

namespace CometEditor {
    Comet::Result<std::filesystem::path> create_project(const std::filesystem::path& root) {
        using Created = Comet::Result<std::filesystem::path>;
        if(root.empty() || root.filename().empty()
            || root.filename().string().find_first_not_of(" \t\r\n") == std::string::npos)
            return Created::failure("Project directory must have a name");

        std::error_code error;
        const bool created = std::filesystem::create_directory(root, error);
        if(error)
            return Created::failure("Cannot create project directory: " + error.message());
        if(!created)
            return Created::failure("Project directory already exists");

        const auto scene_directory = root / "assets" / "scenes";
        const auto scene_file = scene_directory / "main.scene";
        const auto manifest = root / "project.json";
        const auto cleanup = [&] {
            std::error_code ignored;
            std::filesystem::remove(manifest, ignored);
            std::filesystem::remove(scene_file, ignored);
            std::filesystem::remove(scene_directory, ignored);
            std::filesystem::remove(root / "assets", ignored);
            std::filesystem::remove(root, ignored);
        };
        std::filesystem::create_directories(scene_directory, error);
        if(error) {
            cleanup();
            return Created::failure("Cannot create project assets: " + error.message());
        }

        const auto components = Comet::create_scene_component_registry();
        Comet::Scene scene;
        scene.create_entity("MainCamera").add_component<Comet::CameraComponent>().primary = true;
        if(auto saved = Comet::SceneSerializer(components).save(scene, scene_file.string());
            !saved) {
            cleanup();
            return Created::failure(saved.error());
        }

        Comet::Json::Writer writer;
        writer.begin_object();
        writer.field("version", std::uint64_t(Comet::Project::FORMAT_VERSION));
        writer.field("name", root.filename().string());
        writer.field("startup_scene", "scenes/main.scene");
        writer.end_object();
        auto contents = std::move(writer).finish();
        if(!contents) {
            cleanup();
            return Created::failure(contents.error());
        }
        if(auto saved = Comet::write_text_file_atomic(manifest, contents.value()); !saved) {
            cleanup();
            return Created::failure(saved.error());
        }
        auto project = Comet::Project::load(root);
        if(!project) {
            cleanup();
            return Created::failure(project.error());
        }
        return Created::success(project.value().paths().root());
    }
}

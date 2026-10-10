#include "asset/project_prepare.h"
#include "asset/shader_program_import.h"
#include "asset/database.h"
#include "asset/import/import_service.h"
#include "asset/serialization/material_serializer.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "ui/resource_dependencies.h"

#include <unordered_set>

namespace Comet {
    namespace {
        Result<void> prepare_asset(const AssetDatabase& database, const AssetRecord& record,
            const AssetImportLimits limits) {
            const auto& paths = database.paths();
            const ImportService imports(paths, limits);
            switch(record.type) {
                case AssetType::Environment: {
                    auto prepared = imports.prepare_environment(record, limits.async.working_bytes);
                    if(!prepared)
                        return Result<void>::failure(prepared.error());
                    break;
                }
                case AssetType::Mesh: {
                    auto prepared =
                        imports.prepare_mesh(record, database.get_revision(record.handle),
                            MeshImportMode::IfNeeded, limits.mesh_working_bytes);
                    if(!prepared.result)
                        return Result<void>::failure(prepared.result.error());
                    if(!prepared.reused_artifact) {
                        if(!import_inputs_are_current(
                               paths.assets(), prepared.result.value().source_inputs))
                            return Result<void>::failure("Mesh inputs changed during preparation");
                        return prepared.result.value().publish_atomic(
                            imports.mesh_artifact_path(record.handle));
                    }
                    break;
                }
                case AssetType::ShaderProgram: {
                    auto request = ShaderProgramImport::resolve(database, record.handle);
                    if(!request)
                        return Result<void>::failure(request.error());
                    auto prepared = ShaderProgramImport::prepare(paths, request.value());
                    if(!prepared)
                        return Result<void>::failure(prepared.error().message);
                    if(!prepared.value().from_cache) {
                        if(!import_inputs_are_current(
                               paths.assets(), prepared.value().artifact.inputs))
                            return Result<void>::failure(
                                "Shader inputs changed during preparation");
                        return prepared.value().artifact.publish_atomic(
                            imports.shader_program_artifact_path(record.handle));
                    }
                    break;
                }
                case AssetType::Material: {
                    auto material = MaterialSerializer{}.load(paths.assets() / record.path);
                    if(!material)
                        return Result<void>::failure(material.error());
                    break;
                }
                default:
                    break;
            }
            return Result<void>::success();
        }
    }

    Result<void> prepare_project(const Project& project, const AssetImportLimits limits) {
        if(project.ui()) {
            const auto resources =
                Ui::collect_resource_dependencies(project.paths().assets(), *project.ui());
            if(!resources)
                return Result<void>::failure(resources.error());
        }
        const auto components = create_scene_component_registry();
        auto path = project.paths().resolve_asset_path(project.startup_scene());
        if(!path)
            return Result<void>::failure(path.error());
        auto scene = SceneSerializer(components).load(path.value().string());
        if(!scene)
            return Result<void>::failure(scene.error());
        AssetDatabase database(project.paths());
        const auto scan = database.scan();
        if(!scan.snapshot_updated)
            return Result<void>::failure("Cannot scan project assets");

        auto pending = components.collect_asset_references(
            *scene.value(), ComponentRegistry::ReferenceScope::Runtime);
        std::unordered_set<AssetHandle> prepared;
        while(!pending.empty()) {
            const auto reference = pending.back();
            pending.pop_back();
            const auto* record = database.find(reference.handle);
            if(!record) {
                if(!reference.required)
                    continue;
                return Result<void>::failure(
                    "Required asset is missing: " + std::to_string(reference.handle.value()));
            }
            if(record->type != reference.type)
                return Result<void>::failure(
                    "Asset type mismatch: " + record->path.generic_string());
            if(!prepared.insert(record->handle).second)
                continue;
            if(auto result = prepare_asset(database, *record, limits); !result)
                return Result<void>::failure(record->path.generic_string() + ": " + result.error());
            for(const auto dependency : record->dependencies) {
                const auto* input = database.find(dependency);
                if(!input)
                    return Result<void>::failure(
                        "Missing dependency of " + record->path.generic_string());
                pending.push_back({dependency, input->type});
            }
        }
        return Result<void>::success();
    }
}

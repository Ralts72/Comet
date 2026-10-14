#include "asset/default_assets.h"
#include "asset/project_prepare.h"
#include "asset/shader_program_import.h"
#include "asset/database.h"
#include "asset/import/import_service.h"
#include "asset/import/mesh_importer.h"
#include "asset/product_index.h"
#include "asset/script.h"
#include "asset/serialization/material_serializer.h"
#include "common/scope_exit.h"
#include "core/project.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "scene/prefab.h"
#include "ui/resource_dependencies.h"

#include <unordered_set>
#include <map>
#include <set>

namespace Comet {
    namespace {
        Result<std::vector<AssetReference>> collect_runtime_references(
            Scene& scene, const ComponentRegistry& components, const AssetDatabase& database) {
            std::map<AssetHandle, std::shared_ptr<Script>> scripts;
            const auto references = components.collect_runtime_references(
                scene,
                [&](AssetHandle handle) -> Result<std::shared_ptr<Script>, Error> {
                    using Loaded = Result<std::shared_ptr<Script>, Error>;
                    if(const auto found = scripts.find(handle); found != scripts.end())
                        return Loaded::success(found->second);
                    const auto* record = database.find(handle);
                    if(!record || record->type != AssetType::Script)
                        return Loaded::failure({"Script dependency is missing or mistyped"});
                    const std::array entry{record->path};
                    auto loaded = Script::load_group(database.paths().assets(), entry);
                    if(!loaded)
                        return Loaded::failure({loaded.error().message});
                    auto script = loaded.value().front();
                    scripts.emplace(handle, script);
                    return Loaded::success(std::move(script));
                },
                [&](AssetHandle handle) -> Result<std::shared_ptr<Prefab>, Error> {
                    using Loaded = Result<std::shared_ptr<Prefab>, Error>;
                    const auto* record = database.find(handle);
                    if(!record || record->type != AssetType::Prefab)
                        return Loaded::failure({"Prefab dependency is missing or mistyped"});
                    auto prefab =
                        Prefab::load(database.paths().assets() / record->path, components);
                    if(!prefab)
                        return Loaded::failure({prefab.error()});
                    return Loaded::success(std::make_shared<Prefab>(std::move(prefab).value()));
                });
            if(!references)
                return Result<std::vector<AssetReference>>::failure(references.error().message);
            return Result<std::vector<AssetReference>>::success(references.value());
        }

        Result<void> prepare_asset(
            AssetDatabase& database, const AssetRecord record, const AssetImportLimits limits) {
            const auto& paths = database.paths();
            const ImportService imports(paths, limits);
            switch(record.type) {
                case AssetType::Model: {
                    auto prepared = imports.prepare_model(record, limits.mesh_working_bytes);
                    if(!prepared)
                        return Result<void>::failure(prepared.error());
                    const auto scan = database.scan();
                    if(!scan.snapshot_updated)
                        return Result<void>::failure("Cannot index prepared model subresources");
                    std::vector<std::filesystem::path> dependencies;
                    for(size_t index = 1; index < prepared.value().source_inputs.files.size();
                        ++index)
                        dependencies.push_back(
                            prepared.value().source_inputs.files[index].relative_path);
                    return database.update_import_dependencies(
                        record.handle, std::move(dependencies));
                }
                case AssetType::Texture: {
                    const auto* settings =
                        std::get_if<TextureImportSettings>(&record.import_settings);
                    if(!settings)
                        return Result<void>::failure("Texture import settings are missing");
                    auto prepared =
                        imports.prepare_texture(record, *settings, limits.texture_working_bytes);
                    if(!prepared)
                        return Result<void>::failure(prepared.error());
                    break;
                }
                case AssetType::Environment: {
                    auto prepared = imports.prepare_environment(record, limits.async.working_bytes);
                    if(!prepared)
                        return Result<void>::failure(prepared.error());
                    break;
                }
                case AssetType::Mesh: {
                    auto prepared =
                        imports.prepare_mesh(record, database.get_revision(record.handle),
                            AssetImportMode::IfNeeded, limits.mesh_working_bytes);
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
        if(database.is_read_only())
            return Result<void>::success();

        auto references = collect_runtime_references(*scene.value(), components, database);
        if(!references)
            return Result<void>::failure(references.error());
        auto pending = std::move(references).value();
        std::unordered_set<AssetHandle> prepared;
        while(!pending.empty()) {
            const auto reference = pending.back();
            pending.pop_back();
            if(const auto* asset = find_default_asset(reference.handle);
                asset && asset->type == reference.type)
                continue;
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
            if(record->source_asset && prepared.insert(record->source_asset).second) {
                const auto* model = database.find(record->source_asset);
                if(!model || model->type != AssetType::Model)
                    return Result<void>::failure("Model subresource has no source model");
                if(auto result = prepare_asset(database, *model, limits); !result)
                    return result;
                record = database.find(reference.handle);
                if(!record || record->type != reference.type)
                    return Result<void>::failure(
                        "Referenced model subresource was removed during import: "
                        + std::to_string(reference.handle.value()));
            }
            if(!prepared.insert(record->handle).second)
                continue;
            if(!record->source_asset) {
                const auto source_path = record->path;
                if(auto result = prepare_asset(database, *record, limits); !result)
                    return Result<void>::failure(
                        source_path.generic_string() + ": " + result.error());
                record = database.find(reference.handle);
                if(!record)
                    return Result<void>::failure("Asset disappeared during preparation");
            }
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

    Result<void> export_project_assets(const Project& project,
        const std::filesystem::path& destination, std::span<const std::filesystem::path> scenes,
        const AssetImportLimits limits) {
        const auto& paths = project.paths();
        std::filesystem::path staging;
        ScopeExit cleanup([&] {
            std::error_code ignored;
            if(!staging.empty())
                std::filesystem::remove_all(staging, ignored);
        });
        try {
            if(destination.empty())
                return Result<void>::failure("Export destination is empty");
            const auto target = std::filesystem::weakly_canonical(destination);
            if(std::filesystem::exists(target))
                return Result<void>::failure(
                    "Export destination already exists: " + target.string());
            for(const auto& root : {paths.assets(), paths.local_data()}) {
                const auto relative =
                    target.lexically_relative(std::filesystem::weakly_canonical(root));
                if(!relative.empty() && *relative.begin() != "..")
                    return Result<void>::failure(
                        "Export destination is inside project assets or cache");
            }
            AssetDatabase database(paths);
            const auto scan = database.scan();
            if(!scan.succeeded() || database.is_read_only())
                return Result<void>::failure("Export requires a valid source project asset index");
            staging = target.parent_path() / (".comet-export-" + Uuid::generate().to_string());
            std::filesystem::create_directories(staging / "assets");
            const ProjectPaths output(staging);
            const ImportService imports(paths, limits);
            const auto components = create_scene_component_registry();
            const SceneSerializer serializer(components);
            ProductIndex index;
            std::vector<AssetReference> pending;
            std::unordered_map<AssetHandle, AssetType> exported;
            std::unordered_map<AssetHandle, ModelArtifact> models;
            std::map<std::filesystem::path, std::filesystem::path> copied;
            const auto copy = [&](const std::filesystem::path& source,
                                  const std::filesystem::path& relative) -> Result<void> {
                auto resolved = output.resolve_asset_path(relative);
                if(!resolved)
                    return Result<void>::failure(resolved.error());
                const auto [found, inserted] = copied.emplace(relative, source);
                if(!inserted) {
                    if(found->second == source)
                        return Result<void>::success();
                    return Result<void>::failure(
                        "Conflicting export file: " + relative.generic_string());
                }
                std::filesystem::create_directories(resolved.value().parent_path());
                std::filesystem::copy_file(source, resolved.value());
                return Result<void>::success();
            };
            const auto append_references = [&](Scene& scene,
                                               AssetProduct& product) -> Result<void> {
                auto references = collect_runtime_references(scene, components, database);
                if(!references)
                    return Result<void>::failure(references.error());
                for(const auto& reference : references.value()) {
                    if(!reference.handle || reference.handle == product.handle)
                        continue;
                    if(!reference.required && !database.find(reference.handle)
                        && !find_default_asset(reference.handle))
                        continue;
                    pending.push_back(reference);
                    product.dependencies.push_back(reference.handle);
                }
                std::ranges::sort(product.dependencies);
                const auto duplicates = std::ranges::unique(product.dependencies);
                product.dependencies.erase(duplicates.begin(), duplicates.end());
                return Result<void>::success();
            };

            std::set<std::filesystem::path> selected(scenes.begin(), scenes.end());
            selected.insert(project.startup_scene());
            for(const auto& relative : selected) {
                auto path = paths.resolve_asset_path(relative);
                if(!path)
                    return Result<void>::failure(path.error());
                const auto* record = database.find(path.value().lexically_relative(paths.assets()));
                if(!record || record->type != AssetType::Scene)
                    return Result<void>::failure(
                        "Export scene is not indexed: " + relative.generic_string());
                if(!exported.emplace(record->handle, record->type).second)
                    continue;
                auto scene = serializer.load(path.value().string());
                if(!scene)
                    return Result<void>::failure(scene.error());
                AssetProduct product{record->handle, AssetType::Scene, record->path};
                if(auto collected = append_references(*scene.value(), product); !collected)
                    return collected;
                // 来源、基线与覆盖仅供编辑器更新实例；运行实体和稳定引用保持原样。
                for(auto entity : scene.value()->get_entities())
                    if(entity.has_component<PrefabInstanceComponent>())
                        entity.remove_component<PrefabInstanceComponent>();
                if(auto saved =
                        serializer.save(*scene.value(), (output.assets() / product.path).string());
                    !saved)
                    return saved;
                index.assets.push_back(std::move(product));
            }
            while(!pending.empty()) {
                const auto reference = pending.back();
                pending.pop_back();
                if(const auto done = exported.find(reference.handle); done != exported.end()) {
                    if(done->second != reference.type)
                        return Result<void>::failure(
                            "Export reference has conflicting asset types");
                    continue;
                }
                if(const auto* builtin = find_default_asset(reference.handle)) {
                    if(builtin->type != reference.type)
                        return Result<void>::failure("Built-in asset type mismatch");
                    exported.emplace(reference.handle, reference.type);
                    if(builtin->type == AssetType::Material)
                        continue;
                    const auto source = default_character_source();
                    auto inputs = capture_import_inputs(source.parent_path(), source, {});
                    if(!inputs)
                        return Result<void>::failure(inputs.error());
                    auto mesh = MeshImporter{}.import(source, limits.mesh_working_bytes, limits);
                    if(!mesh)
                        return Result<void>::failure(mesh.error());
                    if(!import_inputs_are_current(source.parent_path(), inputs.value()))
                        return Result<void>::failure("Built-in character changed during export");
                    AssetProduct product{reference.handle, AssetType::Mesh,
                        ".products/" + std::to_string(reference.handle.value()) + ".mesh"};
                    MeshArtifact artifact{reference.handle, MeshImporter::VERSION,
                        std::move(inputs).value(), std::move(mesh).value()};
                    if(auto saved = artifact.publish_atomic(output.assets() / product.path); !saved)
                        return saved;
                    index.assets.push_back(std::move(product));
                    continue;
                }
                const auto* found = database.find(reference.handle);
                if(!found || found->type != reference.type)
                    return Result<void>::failure("Missing or mistyped export asset: "
                                                 + std::to_string(reference.handle.value()));
                if(found->source_asset && !models.contains(found->source_asset)) {
                    const auto model_handle = found->source_asset;
                    const auto* model = database.find(model_handle);
                    if(!model || model->type != AssetType::Model)
                        return Result<void>::failure("Model source is not indexed");
                    if(auto prepared = prepare_asset(database, *model, limits); !prepared)
                        return prepared;
                    auto artifact = ModelArtifact::load(
                        imports.model_artifact_path(model_handle), model_handle);
                    if(!artifact)
                        return Result<void>::failure("Prepared model artifact is unavailable");
                    models.emplace(model_handle, std::move(*artifact));
                    found = database.find(reference.handle);
                    if(!found || found->type != reference.type)
                        return Result<void>::failure(
                            "Referenced model child was removed during export");
                }
                const AssetRecord record = *found;
                AssetProduct product{record.handle, record.type, record.path};
                auto dependencies = record.dependencies;
                auto source = paths.assets() / record.path;
                if(record.source_asset) {
                    const auto& model = models.at(record.source_asset);
                    source = model.product_path(
                        imports.model_artifact_path(record.source_asset), record.handle);
                    if(record.type == AssetType::Texture)
                        product.sampler = model.texture_samplers.at(record.handle);
                } else if(auto prepared = prepare_asset(database, record, limits); !prepared)
                    return Result<void>::failure(
                        record.path.generic_string() + ": " + prepared.error());
                switch(record.type) {
                    case AssetType::Mesh:
                        if(!record.source_asset)
                            source = imports.mesh_artifact_path(record.handle);
                        break;
                    case AssetType::Texture:
                        if(!record.source_asset)
                            source = imports.texture_artifact_path(record.handle);
                        break;
                    case AssetType::Environment:
                        source = imports.environment_artifact_path(record.handle);
                        break;
                    case AssetType::ShaderProgram:
                        source = imports.shader_program_artifact_path(record.handle);
                        break;
                    case AssetType::Script: {
                        const std::array entry{record.path};
                        auto script = Script::load_group(paths.assets(), entry);
                        if(!script)
                            return Result<void>::failure(script.error().message);
                        break;
                    }
                    case AssetType::Prefab: {
                        auto prefab = Prefab::load(source, components);
                        if(!prefab)
                            return Result<void>::failure(prefab.error());
                        Scene scene;
                        if(auto instance = prefab.value().instantiate(scene, components); !instance)
                            return Result<void>::failure(instance.error());
                        if(auto collected = append_references(scene, product); !collected)
                            return collected;
                        break;
                    }
                    case AssetType::Material: {
                        auto material = MaterialSerializer{}.load(source);
                        if(!material)
                            return Result<void>::failure(material.error());
                        dependencies = get_asset_dependencies(material.value());
                        break;
                    }
                    case AssetType::Audio:
                        break;
                    default:
                        return Result<void>::failure("Unsupported runtime product type: "
                                                     + std::string(to_string(record.type)));
                }
                if(record.source_asset || record.type == AssetType::Mesh
                    || record.type == AssetType::Texture || record.type == AssetType::Environment
                    || record.type == AssetType::ShaderProgram)
                    product.path = ".products/" + std::to_string(record.handle.value()) + "."
                                   + std::string(to_string(record.type));
                if(auto written = copy(source, product.path); !written)
                    return written;
                for(const auto dependency : dependencies) {
                    if(dependency == record.source_asset)
                        continue;
                    const auto* input = database.find(dependency);
                    if(const auto* builtin = find_default_asset(dependency); !input && builtin) {
                        product.dependencies.push_back(dependency);
                        pending.push_back({dependency, builtin->type});
                        continue;
                    }
                    if(!input)
                        return Result<void>::failure(
                            "Missing dependency of " + record.path.generic_string());
                    if(input->type == AssetType::Shader)
                        continue;
                    product.dependencies.push_back(dependency);
                    pending.push_back({dependency, input->type});
                }
                std::ranges::sort(product.dependencies);
                const auto duplicates = std::ranges::unique(product.dependencies);
                product.dependencies.erase(duplicates.begin(), duplicates.end());
                exported.emplace(record.handle, record.type);
                index.assets.push_back(std::move(product));
            }
            if(project.ui()) {
                auto resources = Ui::collect_resource_dependencies(paths.assets(), *project.ui());
                if(!resources)
                    return Result<void>::failure(resources.error());
                for(const auto& relative : resources.value())
                    if(auto written = copy(paths.assets() / relative, relative); !written)
                        return written;
            }
            // require 可以在运行回调中发生，项目模块库整体交付，不依赖执行某条玩法分支。
            for(const auto& entry : std::filesystem::recursive_directory_iterator(paths.assets())) {
                if(!entry.is_regular_file() || !is_lua_module_source(entry.path()))
                    continue;
                auto path = paths.resolve_asset_path(entry.path());
                if(!path)
                    return Result<void>::failure(path.error());
                if(auto written =
                        copy(path.value(), entry.path().lexically_relative(paths.assets()));
                    !written)
                    return written;
            }
            std::filesystem::copy_file(paths.root() / "project.json", staging / "project.json");
            std::ranges::sort(index.assets, {}, &AssetProduct::handle);
            if(auto saved = index.save(staging / ProductIndex::FILENAME); !saved)
                return saved;
            std::filesystem::rename(staging, target);
            cleanup.release();
            return Result<void>::success();
        } catch(const std::filesystem::filesystem_error& error) {
            return Result<void>::failure(
                "Cannot export project assets: " + std::string(error.what()));
        }
    }
}

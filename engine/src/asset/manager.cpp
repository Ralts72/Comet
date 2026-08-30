#include "asset/manager.h"

#include "asset/cache/mesh_import_cache.h"
#include "asset/import/mesh_importer.h"
#include "asset/import/texture_importer.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "common/file_io.h"
#include "core/task_scheduler.h"
#include "diagnostics/logger.h"
#include "render/material.h"
#include "render/resource/mesh.h"
#include "render/resource/resource_factory.h"
#include "render/resource/texture.h"

#include <chrono>
#include <deque>
#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Comet {
    namespace {
        struct MeshImportCandidate {
            AssetHandle handle;
            AssetRevision revision = INVALID_ASSET_REVISION;
            std::filesystem::path relative_path;
            std::filesystem::path asset_root;
            std::filesystem::path source_path;
            std::filesystem::path cache_path;
            MeshData data;
            std::vector<std::filesystem::path> source_dependencies;
            std::string error;
            bool cache_hit = false;
            bool cache_update_required = false;
        };

        MeshImportCandidate import_mesh_candidate(
            const std::filesystem::path& asset_root,
            const std::filesystem::path& cache_root,
            const AssetHandle handle,
            const AssetRevision revision,
            const std::filesystem::path& relative_path) {
            MeshImportCandidate candidate{
                .handle = handle,
                .revision = revision,
                .relative_path = relative_path,
                .asset_root = asset_root,
                .source_path = asset_root / relative_path,
                .cache_path = cache_root / "imported" / "mesh"
                    / (std::to_string(handle.value()) + ".bin")
            };

            try {
                if(auto cached = MeshImportCache::load_if_current(
                       candidate.cache_path,
                       candidate.asset_root,
                       candidate.source_path,
                       MeshImporter::OUTPUT_VERSION)) {
                    candidate.data = std::move(*cached);
                    candidate.cache_hit = true;
                } else {
                    MeshImportResult imported =
                        MeshImporter{}.import_with_dependencies(
                            candidate.source_path);
                    candidate.data = std::move(imported.data);
                    candidate.source_dependencies =
                        std::move(imported.source_dependencies);
                    candidate.cache_update_required = true;
                }
            } catch(const std::exception& exception) {
                candidate.error = exception.what();
            } catch(...) {
                candidate.error = "Unknown mesh import failure";
            }
            return candidate;
        }

        void update_mesh_import_cache(const MeshImportCandidate& candidate) {
            MeshImportCache::store(
                candidate.cache_path,
                candidate.asset_root,
                candidate.source_path,
                candidate.source_dependencies,
                MeshImporter::OUTPUT_VERSION,
                candidate.data);
        }
    }

    struct AssetManager::AsyncState {
        struct ScheduledMeshTask {
            AssetHandle handle;
            AssetRevision revision = INVALID_ASSET_REVISION;
            std::future<void> completion;
        };

        std::mutex completed_mutex;
        std::deque<MeshImportCandidate> completed_meshes;
        std::unordered_map<AssetHandle, AssetRevision> pending_meshes;
        std::vector<ScheduledMeshTask> scheduled_tasks;
    };

    AssetManager::AssetManager(
        ProjectPaths paths,
        AssetRegistry& registry,
        RenderResourceFactory& resource_factory,
        TaskScheduler& task_scheduler)
        : m_paths(std::move(paths)),
          m_database(m_paths),
          m_registry(registry),
          m_resource_factory(resource_factory),
          m_task_scheduler(task_scheduler),
          m_async_state(std::make_shared<AsyncState>()) {}

    AssetManager::~AssetManager() {
        for(AsyncState::ScheduledMeshTask& task:
            m_async_state->scheduled_tasks) {
            task.completion.wait();
        }
    }

    AssetScanReport AssetManager::scan() {
        AssetScanReport report = m_database.scan();
        if(!report.snapshot_updated) {
            return report;
        }

        std::unordered_set<AssetHandle> invalidated(
            report.removed_assets.begin(),
            report.removed_assets.end());
        std::queue<AssetHandle> pending_invalidations;
        for(const AssetHandle handle: report.removed_assets) {
            pending_invalidations.push(handle);
        }
        while(!pending_invalidations.empty()) {
            const AssetHandle dependency = pending_invalidations.front();
            pending_invalidations.pop();
            for(const AssetHandle dependent:
                m_database.get_dependents(dependency)) {
                if(invalidated.insert(dependent).second) {
                    pending_invalidations.push(dependent);
                }
            }
        }
        for(const AssetHandle handle: invalidated) {
            static_cast<void>(m_registry.unregister_asset(handle));
        }

        for(const AssetHandle handle: report.modified_assets) {
            if(invalidated.contains(handle) || !m_registry.contains(handle)) {
                continue;
            }

            const AssetRecord* record = m_database.find(handle);
            if(!record) {
                continue;
            }
            switch(record->type) {
                case AssetType::Texture:
                    if(!refresh_loaded_texture(*record)) {
                        LOG_ERROR(
                            "Failed to refresh modified texture asset handle {}",
                            handle.value());
                    }
                    break;
                case AssetType::Material:
                    if(!reload_material(handle)) {
                        LOG_ERROR(
                            "Failed to refresh modified material asset handle {}",
                            handle.value());
                    }
                    break;
                case AssetType::Mesh:
                    if(!schedule_loaded_mesh_refresh(*record)) {
                        LOG_ERROR(
                            "Failed to schedule refresh for modified mesh asset handle {}",
                            handle.value());
                    }
                    break;
                default:
                    static_cast<void>(m_registry.unregister_asset(handle));
                    LOG_WARN(
                        "Unloaded modified asset handle {} because runtime reload is not implemented for type '{}'",
                        handle.value(),
                        to_string(record->type));
                    break;
            }
        }
        return report;
    }

    void AssetManager::process_completions() {
        std::deque<MeshImportCandidate> completed_meshes;
        {
            const std::lock_guard lock(m_async_state->completed_mutex);
            completed_meshes.swap(m_async_state->completed_meshes);
        }

        for(MeshImportCandidate& candidate: completed_meshes) {
            const auto pending =
                m_async_state->pending_meshes.find(candidate.handle);
            if(pending != m_async_state->pending_meshes.end()
               && pending->second == candidate.revision) {
                m_async_state->pending_meshes.erase(pending);
            }

            if(!m_database.is_current(
                   candidate.handle,
                   candidate.revision)) {
                LOG_DEBUG(
                    "Discarded stale background mesh import for asset handle {} (revision {})",
                    candidate.handle.value(),
                    candidate.revision);
                continue;
            }
            if(!candidate.error.empty()) {
                LOG_ERROR(
                    "Failed to import modified mesh asset '{}' (handle {}): {}",
                    candidate.relative_path.generic_string(),
                    candidate.handle.value(),
                    candidate.error);
                continue;
            }

            if(candidate.cache_update_required) {
                try {
                    update_mesh_import_cache(candidate);
                } catch(const std::exception& exception) {
                    LOG_WARN(
                        "Imported mesh '{}', but could not update its import cache: {}",
                        candidate.relative_path.generic_string(),
                        exception.what());
                }
            } else if(candidate.cache_hit) {
                LOG_DEBUG(
                    "Loaded mesh import cache '{}' (handle {})",
                    candidate.relative_path.generic_string(),
                    candidate.handle.value());
            }

            if(!m_database.is_current(
                   candidate.handle,
                   candidate.revision)) {
                LOG_DEBUG(
                    "Discarded stale background mesh import for asset handle {} (revision {})",
                    candidate.handle.value(),
                    candidate.revision);
                continue;
            }

            std::shared_ptr<Mesh> mesh;
            try {
                mesh = m_resource_factory.create_mesh(candidate.data);
            } catch(const std::exception& exception) {
                LOG_ERROR(
                    "Failed to create refreshed runtime mesh for asset handle {}: {}",
                    candidate.handle.value(),
                    exception.what());
                continue;
            }
            if(!mesh) {
                LOG_ERROR(
                    "Failed to create refreshed runtime mesh for asset handle {}",
                    candidate.handle.value());
                continue;
            }
            if(!m_database.is_current(
                   candidate.handle,
                   candidate.revision)) {
                LOG_DEBUG(
                    "Discarded stale runtime mesh candidate for asset handle {} (revision {})",
                    candidate.handle.value(),
                    candidate.revision);
                continue;
            }
            if(!m_registry.replace_asset(candidate.handle, mesh)) {
                LOG_ERROR(
                    "Failed to publish refreshed runtime mesh for asset handle {}",
                    candidate.handle.value());
                continue;
            }

            LOG_INFO(
                "Reloaded mesh asset '{}' (handle {})",
                candidate.relative_path.generic_string(),
                candidate.handle.value());
        }

        auto& tasks = m_async_state->scheduled_tasks;
        for(auto task = tasks.begin(); task != tasks.end();) {
            if(task->completion.wait_for(std::chrono::seconds(0))
               != std::future_status::ready) {
                ++task;
                continue;
            }

            try {
                task->completion.get();
            } catch(const std::exception& exception) {
                const auto pending =
                    m_async_state->pending_meshes.find(task->handle);
                if(pending != m_async_state->pending_meshes.end()
                   && pending->second == task->revision) {
                    m_async_state->pending_meshes.erase(pending);
                }
                LOG_ERROR(
                    "Background mesh task failed for asset handle {}: {}",
                    task->handle.value(),
                    exception.what());
            }
            task = tasks.erase(task);
        }
    }

    std::shared_ptr<Mesh> AssetManager::load_mesh(
        const AssetHandle handle) {
        if(!handle) {
            LOG_ERROR("Cannot load a mesh with an invalid asset handle");
            return nullptr;
        }

        if(const auto mesh = m_registry.resolve<Mesh>(handle)) {
            return mesh;
        }
        if(m_registry.contains(handle)) {
            LOG_ERROR(
                "Asset handle {} is already registered with another runtime type",
                handle.value());
            return nullptr;
        }

        const AssetRecord* record = m_database.find(handle);
        if(!record) {
            LOG_ERROR("Mesh asset handle {} is not indexed", handle.value());
            return nullptr;
        }
        if(record->type != AssetType::Mesh) {
            LOG_ERROR(
                "Asset handle {} has type '{}', expected 'mesh'",
                handle.value(),
                to_string(record->type));
            return nullptr;
        }
        const AssetRevision revision = m_database.get_revision(handle);

        auto mesh = create_runtime_mesh(*record);
        if(!mesh) {
            return nullptr;
        }
        if(!m_database.is_current(handle, revision)) {
            LOG_DEBUG(
                "Discarded stale runtime mesh candidate for asset handle {} (revision {})",
                handle.value(),
                revision);
            return nullptr;
        }
        if(!m_registry.register_asset(handle, mesh)) {
            LOG_ERROR(
                "Failed to register runtime mesh for asset handle {}",
                handle.value());
            return nullptr;
        }
        return mesh;
    }

    std::shared_ptr<Texture> AssetManager::load_texture(
        const AssetHandle handle) {
        if(!handle) {
            LOG_ERROR("Cannot load a texture with an invalid asset handle");
            return nullptr;
        }

        if(const auto texture = m_registry.resolve<Texture>(handle)) {
            return texture;
        }
        if(m_registry.contains(handle)) {
            LOG_ERROR(
                "Asset handle {} is already registered with another runtime type",
                handle.value());
            return nullptr;
        }

        const AssetRecord* record = m_database.find(handle);
        if(!record) {
            LOG_ERROR("Texture asset handle {} is not indexed", handle.value());
            return nullptr;
        }
        if(record->type != AssetType::Texture) {
            LOG_ERROR(
                "Asset handle {} has type '{}', expected 'texture'",
                handle.value(),
                to_string(record->type));
            return nullptr;
        }

        const auto* settings = std::get_if<TextureImportSettings>(
            &record->import_settings);
        if(!settings) {
            LOG_ERROR(
                "Texture asset handle {} has incompatible import settings",
                handle.value());
            return nullptr;
        }

        auto texture = create_runtime_texture(*record, *settings);
        if(!texture) {
            return nullptr;
        }
        if(!m_registry.register_asset(handle, texture)) {
            LOG_ERROR(
                "Failed to register runtime texture for asset handle {}",
                handle.value());
            return nullptr;
        }
        return texture;
    }

    std::shared_ptr<Texture> AssetManager::reimport_texture(
        const AssetHandle handle,
        TextureImportSettings import_settings) {
        if(!handle) {
            LOG_ERROR("Cannot reimport a texture with an invalid asset handle");
            return nullptr;
        }

        const AssetRecord* record = m_database.find(handle);
        if(!record) {
            LOG_ERROR("Texture asset handle {} is not indexed", handle.value());
            return nullptr;
        }
        if(record->type != AssetType::Texture) {
            LOG_ERROR(
                "Asset handle {} has type '{}', expected 'texture'",
                handle.value(),
                to_string(record->type));
            return nullptr;
        }

        const auto previous_texture = m_registry.resolve<Texture>(handle);
        if(m_registry.contains(handle) && !previous_texture) {
            LOG_ERROR(
                "Asset handle {} is already registered with another runtime type",
                handle.value());
            return nullptr;
        }

        std::vector<AssetHandle> dependent_materials;
        for(const AssetHandle dependent: m_database.get_dependents(handle)) {
            const AssetRecord* dependent_record = m_database.find(dependent);
            if(dependent_record
               && dependent_record->type == AssetType::Material
               && m_registry.resolve<Material>(dependent)) {
                dependent_materials.push_back(dependent);
            }
        }

        auto texture = create_runtime_texture(*record, import_settings);
        if(!texture) {
            return nullptr;
        }

        try {
            m_database.update_import_settings(handle, import_settings);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        const bool published = previous_texture
            ? m_registry.replace_asset(handle, texture)
            : m_registry.register_asset(handle, texture);
        if(!published) {
            LOG_ERROR(
                "Failed to publish reimported texture for asset handle {}",
                handle.value());
            return nullptr;
        }

        for(const AssetHandle material_handle: dependent_materials) {
            if(!reload_material(material_handle)) {
                LOG_ERROR(
                    "Texture handle {} was reimported, but dependent material handle {} could not be refreshed",
                    handle.value(),
                    material_handle.value());
            }
        }
        LOG_INFO(
            "Reimported texture asset '{}' (handle {}, color_space={}, flip_y={})",
            record->path.generic_string(),
            handle.value(),
            to_string(import_settings.color_space),
            import_settings.flip_y);
        return texture;
    }

    std::shared_ptr<Material> AssetManager::load_material(
        const AssetHandle handle) {
        if(!handle) {
            LOG_ERROR("Cannot load a material with an invalid asset handle");
            return nullptr;
        }

        if(const auto material = m_registry.resolve<Material>(handle)) {
            return material;
        }
        if(m_registry.contains(handle)) {
            LOG_ERROR(
                "Asset handle {} is already registered with another runtime type",
                handle.value());
            return nullptr;
        }

        const AssetRecord* record = m_database.find(handle);
        if(!record) {
            LOG_ERROR("Material asset handle {} is not indexed", handle.value());
            return nullptr;
        }
        if(record->type != AssetType::Material) {
            LOG_ERROR(
                "Asset handle {} has type '{}', expected 'material'",
                handle.value(),
                to_string(record->type));
            return nullptr;
        }

        auto material = create_runtime_material(*record);
        if(!material) {
            return nullptr;
        }
        if(!m_registry.register_asset(handle, material)) {
            LOG_ERROR(
                "Failed to register runtime material for asset handle {}",
                handle.value());
            return nullptr;
        }
        return material;
    }

    std::shared_ptr<Material> AssetManager::reload_material(
        const AssetHandle handle) {
        if(!handle) {
            LOG_ERROR("Cannot reload a material with an invalid asset handle");
            return nullptr;
        }

        const AssetRecord* record = m_database.find(handle);
        if(!record) {
            LOG_ERROR("Material asset handle {} is not indexed", handle.value());
            return nullptr;
        }
        if(record->type != AssetType::Material) {
            LOG_ERROR(
                "Asset handle {} has type '{}', expected 'material'",
                handle.value(),
                to_string(record->type));
            return nullptr;
        }

        const bool has_runtime_asset = m_registry.contains(handle);
        if(has_runtime_asset && !m_registry.resolve<Material>(handle)) {
            LOG_ERROR(
                "Asset handle {} is already registered with another runtime type",
                handle.value());
            return nullptr;
        }

        MaterialData data;
        try {
            data = MaterialSerializer{}.load(m_paths.assets() / record->path);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        auto material = create_runtime_material(*record, data);
        if(!material) {
            return nullptr;
        }

        try {
            m_database.update_dependencies(
                handle,
                get_asset_dependencies(data));
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        const bool published = has_runtime_asset
            ? m_registry.replace_asset(handle, material)
            : m_registry.register_asset(handle, material);
        if(!published) {
            LOG_ERROR(
                "Failed to publish reloaded material for asset handle {}",
                handle.value());
            return nullptr;
        }
        LOG_INFO(
            "Reloaded material asset '{}' (handle {})",
            record->path.generic_string(),
            handle.value());
        return material;
    }

    std::shared_ptr<Material> AssetManager::update_material(
        const AssetHandle handle,
        const MaterialData& data) {
        if(!handle) {
            LOG_ERROR("Cannot update a material with an invalid asset handle");
            return nullptr;
        }

        const AssetRecord* record = m_database.find(handle);
        if(!record) {
            LOG_ERROR("Material asset handle {} is not indexed", handle.value());
            return nullptr;
        }
        if(record->type != AssetType::Material) {
            LOG_ERROR(
                "Asset handle {} has type '{}', expected 'material'",
                handle.value(),
                to_string(record->type));
            return nullptr;
        }

        const bool has_runtime_asset = m_registry.contains(handle);
        if(has_runtime_asset && !m_registry.resolve<Material>(handle)) {
            LOG_ERROR(
                "Asset handle {} is already registered with another runtime type",
                handle.value());
            return nullptr;
        }

        const MaterialSerializer serializer;
        std::string serialized_data;
        try {
            serialized_data = serializer.serialize(data);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        auto material = create_runtime_material(*record, data);
        if(!material) {
            return nullptr;
        }

        try {
            write_text_file_atomic(
                m_paths.assets() / record->path,
                serialized_data);
            m_database.update_dependencies(
                handle,
                get_asset_dependencies(data));
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        const bool published = has_runtime_asset
            ? m_registry.replace_asset(handle, material)
            : m_registry.register_asset(handle, material);
        if(!published) {
            LOG_ERROR(
                "Failed to publish updated material for asset handle {}",
                handle.value());
            return nullptr;
        }
        LOG_INFO(
            "Updated material asset '{}' (handle {})",
            record->path.generic_string(),
            handle.value());
        return material;
    }

    std::shared_ptr<Mesh> AssetManager::create_runtime_mesh(
        const AssetRecord& record) {
        const AssetRevision revision =
            m_database.get_revision(record.handle);
        const MeshImportCandidate candidate = import_mesh_candidate(
            m_paths.assets(),
            m_paths.cache(),
            record.handle,
            revision,
            record.path);
        if(!candidate.error.empty()) {
            LOG_ERROR("{}", candidate.error);
            return nullptr;
        }
        if(!m_database.is_current(record.handle, revision)) {
            return nullptr;
        }
        if(candidate.cache_update_required) {
            try {
                update_mesh_import_cache(candidate);
            } catch(const std::exception& exception) {
                LOG_WARN(
                    "Imported mesh '{}', but could not update its import cache: {}",
                    record.path.generic_string(),
                    exception.what());
            }
        } else if(candidate.cache_hit) {
            LOG_DEBUG(
                "Loaded mesh import cache '{}' (handle {})",
                record.path.generic_string(),
                record.handle.value());
        }
        if(!m_database.is_current(record.handle, revision)) {
            return nullptr;
        }
        return m_resource_factory.create_mesh(candidate.data);
    }

    bool AssetManager::schedule_loaded_mesh_refresh(
        const AssetRecord& record) {
        const AssetHandle handle = record.handle;
        const AssetRevision revision = m_database.get_revision(handle);
        const auto previous_mesh = m_registry.resolve<Mesh>(handle);
        if(!previous_mesh) {
            return !m_registry.contains(handle);
        }

        const auto pending = m_async_state->pending_meshes.find(handle);
        if(pending != m_async_state->pending_meshes.end()
           && pending->second == revision) {
            return true;
        }

        m_async_state->pending_meshes[handle] = revision;
        bool task_slot_created = false;
        try {
            m_async_state->scheduled_tasks.push_back({
                .handle = handle,
                .revision = revision,
                .completion = {}
            });
            task_slot_created = true;
            m_async_state->scheduled_tasks.back().completion =
                m_task_scheduler.submit([
                    state = m_async_state,
                    asset_root = m_paths.assets(),
                    cache_root = m_paths.cache(),
                    handle,
                    revision,
                    relative_path = record.path
                ] {
                    MeshImportCandidate candidate = import_mesh_candidate(
                        asset_root,
                        cache_root,
                        handle,
                        revision,
                        relative_path);
                    const std::lock_guard lock(state->completed_mutex);
                    state->completed_meshes.push_back(std::move(candidate));
                });
        } catch(const std::exception& exception) {
            if(task_slot_created) {
                m_async_state->scheduled_tasks.pop_back();
            }
            const auto current_pending =
                m_async_state->pending_meshes.find(handle);
            if(current_pending != m_async_state->pending_meshes.end()
               && current_pending->second == revision) {
                m_async_state->pending_meshes.erase(current_pending);
            }
            LOG_ERROR(
                "Failed to schedule mesh refresh for asset handle {}: {}",
                handle.value(),
                exception.what());
            return false;
        }
        return true;
    }

    std::shared_ptr<Texture> AssetManager::create_runtime_texture(
        const AssetRecord& record,
        const TextureImportSettings& import_settings) {
        TextureData data;
        try {
            data = TextureImporter{}.import(
                m_paths.assets() / record.path,
                import_settings);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }
        return m_resource_factory.create_texture(data);
    }

    bool AssetManager::refresh_loaded_texture(const AssetRecord& record) {
        const auto previous_texture = m_registry.resolve<Texture>(record.handle);
        if(!previous_texture) {
            return !m_registry.contains(record.handle);
        }

        const auto* settings = std::get_if<TextureImportSettings>(
            &record.import_settings);
        if(!settings) {
            LOG_ERROR(
                "Texture asset handle {} has incompatible import settings",
                record.handle.value());
            return false;
        }

        auto texture = create_runtime_texture(record, *settings);
        if(!texture
           || !m_registry.replace_asset(record.handle, texture)) {
            return false;
        }

        for(const AssetHandle dependent:
            m_database.get_dependents(record.handle)) {
            const AssetRecord* dependent_record = m_database.find(dependent);
            if(dependent_record
               && dependent_record->type == AssetType::Material
               && m_registry.resolve<Material>(dependent)
               && !reload_material(dependent)) {
                LOG_ERROR(
                    "Texture handle {} was refreshed, but dependent material handle {} could not be refreshed",
                    record.handle.value(),
                    dependent.value());
            }
        }
        LOG_INFO(
            "Reloaded texture asset '{}' (handle {})",
            record.path.generic_string(),
            record.handle.value());
        return true;
    }

    std::shared_ptr<Material> AssetManager::create_runtime_material(
        const AssetRecord& record) {
        MaterialData data;
        try {
            data = MaterialSerializer{}.load(m_paths.assets() / record.path);
        } catch(const std::exception& exception) {
            LOG_ERROR("{}", exception.what());
            return nullptr;
        }

        return create_runtime_material(record, data);
    }

    std::shared_ptr<Material> AssetManager::create_runtime_material(
        const AssetRecord& record,
        const MaterialData& data) {
        std::map<std::string, std::shared_ptr<Texture>> textures;
        for(const auto& [property_name, texture_handle]: data.texture_properties) {
            auto texture = load_texture(texture_handle);
            if(!texture) {
                LOG_ERROR(
                    "Failed to resolve texture handle {} for material '{}' property '{}'",
                    texture_handle.value(),
                    record.path.generic_string(),
                    property_name);
                return nullptr;
            }
            textures.emplace(property_name, std::move(texture));
        }

        auto material = std::make_shared<Material>(
            record.path.stem().string(),
            data.template_name);
        for(const auto& [property_name, texture]: textures) {
            material->set_texture_property(property_name, texture);
        }
        return material;
    }
}

#include "asset/runtime/prefab_loader.h"
#include "asset/database.h"
#include "asset/import/import_service.h"

namespace Comet {
    Result<Prefab> read_prefab_content(const AssetDatabase& database, const AssetHandle source,
        const ComponentRegistry& registry) {
        const auto read = [&](AssetHandle handle) -> Result<Prefab> {
            const auto* record = database.find(handle);
            if(!record || record->type != AssetType::Prefab)
                return Result<Prefab>::failure("Nested Prefab source is missing or mistyped");
            auto path = database.paths().resolve_asset_path(record->path);
            if(!path)
                return Result<Prefab>::failure(path.error());
            return Prefab::load(path.value(), registry);
        };
        auto content = read(source);
        if(!content)
            return content;
        return content.value().resolve_nested(registry, source, read);
    }

    SceneSerializer::Sources project_scene_sources(
        AssetDatabase& database, const ComponentRegistry& registry, AssetImportLimits limits) {
        return [&database, &registry, limits](
                   AssetHandle handle, AssetType type) -> Result<std::shared_ptr<const Prefab>> {
            using Loaded = Result<std::shared_ptr<const Prefab>>;
            if(type == AssetType::Prefab) {
                auto content = read_prefab_content(database, handle, registry);
                if(!content)
                    return Loaded::failure(content.error());
                return Loaded::success(std::make_shared<const Prefab>(std::move(content).value()));
            }
            const auto* record = database.find(handle);
            if(type != AssetType::Model || !record || record->type != type
                || database.is_read_only())
                return Loaded::failure("Scene model source is missing or unavailable");
            const auto name = record->path.stem().string();
            auto model = ImportService(database.paths(), limits)
                             .prepare_model(*record, limits.mesh_working_bytes);
            if(!model)
                return Loaded::failure(model.error());
            if(!database.scan().succeeded())
                return Loaded::failure("Cannot index scene model products");
            auto prefab = Prefab::from_model(model.value().model, handle, registry, name);
            if(!prefab)
                return Loaded::failure(prefab.error());
            return Loaded::success(std::make_shared<const Prefab>(std::move(prefab).value()));
        };
    }

}

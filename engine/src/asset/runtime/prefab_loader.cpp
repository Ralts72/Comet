#include "asset/runtime/prefab_loader.h"
#include "asset/database.h"

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

}

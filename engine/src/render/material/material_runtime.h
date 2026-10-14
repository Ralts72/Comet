#pragma once

#include "asset/handle.h"
#include "asset/data/material_data.h"
#include "asset/registry.h"
#include "common/export.h"
#include "common/result.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Comet {
    class Material;
    class Texture;
    class MaterialLayout;
    struct MaterialOverrides;

    struct MaterialInstanceKey {
        AssetHandle material_handle;
        uint64_t instance_id = 0;

        auto operator<=>(const MaterialInstanceKey&) const = default;
    };

    struct PreparedMaterial {
        struct TextureBinding {
            uint32_t binding;
            std::shared_ptr<Texture> texture; // 空值使用布局指定的默认纹理。

            bool operator==(const TextureBinding&) const = default;
        };

        std::shared_ptr<const MaterialLayout> layout;
        std::vector<TextureBinding> textures;
        std::vector<std::byte> parameters;
        MaterialSurface surface;
    };

    // 仅所属线程可访问；源对象跟踪版本，准备结果不可变。
    class COMET_API MaterialRuntimeCache {
    public:
        [[nodiscard]] Result<std::shared_ptr<const PreparedMaterial>> prepare(AssetHandle handle,
            const std::shared_ptr<const Material>& material,
            const std::shared_ptr<const MaterialLayout>& layout,
            const std::shared_ptr<const MaterialOverrides>& overrides = {});

        void collect_unused();
        // 标记仍被场景引用的已有缓存，不为屏外材质创建新资源。
        void mark_used(MaterialInstanceKey key);
        void erase(AssetHandle handle);
        void erase(MaterialInstanceKey key) { m_entries.erase(key); }
        void remove_assets(const AssetRegistry::Removals& removals);
        Result<std::shared_ptr<const PreparedMaterial>> rebind(
            MaterialInstanceKey key, const std::shared_ptr<const MaterialLayout>& layout);
        Result<std::shared_ptr<const PreparedMaterial>> rebind(
            AssetHandle handle, const std::shared_ptr<const MaterialLayout>& layout) {
            return rebind(MaterialInstanceKey{handle}, layout);
        }
        void swap(MaterialRuntimeCache& other) noexcept;
        void merge(MaterialRuntimeCache&& candidates);

    private:
        struct InputKey {
            std::shared_ptr<const Material> source;
            std::shared_ptr<const MaterialLayout> layout;
            std::shared_ptr<const MaterialOverrides> overrides;
            uint64_t material_revision = 0;

            [[nodiscard]] bool matches(const std::shared_ptr<const Material>& material,
                const std::shared_ptr<const MaterialLayout>& requested_layout,
                const std::shared_ptr<const MaterialOverrides>& requested_overrides,
                uint64_t revision) const {
                return source == material && layout == requested_layout
                       && overrides == requested_overrides && material_revision == revision;
            }
        };
        struct Entry {
            InputKey input;
            std::shared_ptr<const PreparedMaterial> prepared;
            std::string error;
            bool used = false;
        };
        std::map<MaterialInstanceKey, Entry> m_entries;
    };
}

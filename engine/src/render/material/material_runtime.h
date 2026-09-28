#pragma once

#include "asset/handle.h"
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
            std::shared_ptr<Texture> texture; // 空值表示可选槽位使用白色默认纹理。

            bool operator==(const TextureBinding&) const = default;
        };

        std::shared_ptr<const MaterialLayout> layout;
        std::vector<TextureBinding> textures;
        std::vector<std::byte> parameters;
    };

    // 仅所属线程可访问；源对象跟踪版本，准备结果不可变。
    class COMET_API MaterialRuntimeCache {
    public:
        [[nodiscard]] Result<std::shared_ptr<const PreparedMaterial>> prepare(AssetHandle handle,
            const std::shared_ptr<const Material>& material,
            const std::shared_ptr<const MaterialLayout>& layout,
            const std::shared_ptr<const MaterialOverrides>& overrides = {});

        void collect_unused();
        void erase(AssetHandle handle);
        void erase(MaterialInstanceKey key) { m_entries.erase(key); }
        Result<std::shared_ptr<const PreparedMaterial>> rebind(
            MaterialInstanceKey key, const std::shared_ptr<const MaterialLayout>& layout);
        Result<std::shared_ptr<const PreparedMaterial>> rebind(
            AssetHandle handle, const std::shared_ptr<const MaterialLayout>& layout) {
            return rebind(MaterialInstanceKey{handle}, layout);
        }
        void swap(MaterialRuntimeCache& other) noexcept;
        void merge(MaterialRuntimeCache&& candidates);

    private:
        struct Entry {
            std::shared_ptr<const Material> source;
            std::shared_ptr<const MaterialLayout> layout;
            std::shared_ptr<const MaterialOverrides> overrides;
            uint64_t material_revision = 0;
            std::shared_ptr<const PreparedMaterial> prepared;
            std::string error;
            bool used = false;
        };
        std::map<MaterialInstanceKey, Entry> m_entries;
    };
}

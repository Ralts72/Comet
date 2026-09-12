#pragma once

#include "asset/handle.h"
#include "common/export.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Comet {
    class Material;
    class Texture;
    class MaterialLayout;

    struct PreparedMaterial {
        struct TextureBinding {
            uint32_t binding;
            std::shared_ptr<Texture> texture;

            bool operator==(const TextureBinding&) const = default;
        };

        std::shared_ptr<const MaterialLayout> layout;
        std::vector<TextureBinding> textures;
        std::vector<std::byte> parameters;
    };

    // Owner-thread only; sources track revisions, prepared results are immutable.
    class COMET_API MaterialRuntimeCache {
    public:
        [[nodiscard]] std::shared_ptr<const PreparedMaterial> prepare(AssetHandle handle,
            const std::shared_ptr<const Material>& material,
            const std::shared_ptr<const MaterialLayout>& layout);

        void collect_unused();

    private:
        struct Entry {
            std::shared_ptr<const Material> source;
            std::shared_ptr<const MaterialLayout> layout;
            uint64_t material_revision = 0;
            std::shared_ptr<const PreparedMaterial> prepared;
            bool used = false;
        };
        std::unordered_map<AssetHandle, Entry> m_entries;
    };
}

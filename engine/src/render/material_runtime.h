#pragma once

#include "asset/handle.h"
#include "common/export.h"

#include <cstdint>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Comet {
    class Material;
    class Texture;

    class COMET_API MaterialLayout {
    public:
        struct TextureProperty {
            std::string name;
            uint32_t binding;
        };
        struct ScalarProperty {
            std::string name;
            uint32_t offset;
            float default_value;
        };
        struct VectorProperty {
            std::string name;
            uint32_t offset;
            std::array<float, 4> default_value;
        };

        MaterialLayout(std::string name, uint64_t revision,
            std::vector<TextureProperty> textures, uint32_t parameter_size = 0,
            std::vector<ScalarProperty> scalars = {},
            std::vector<VectorProperty> vectors = {});
        MaterialLayout(const MaterialLayout&) = default;
        MaterialLayout& operator=(const MaterialLayout&) = delete;

        [[nodiscard]] const std::string& get_name() const { return m_name; }
        [[nodiscard]] uint64_t get_revision() const { return m_revision; }
        [[nodiscard]] const std::vector<TextureProperty>& get_textures() const {
            return m_textures;
        }
        [[nodiscard]] uint32_t get_parameter_size() const { return m_parameter_size; }
        [[nodiscard]] const std::vector<ScalarProperty>& get_scalars() const {
            return m_scalars;
        }
        [[nodiscard]] const std::vector<VectorProperty>& get_vectors() const {
            return m_vectors;
        }

    private:
        std::string m_name;
        uint64_t m_revision;
        std::vector<TextureProperty> m_textures;
        uint32_t m_parameter_size;
        std::vector<ScalarProperty> m_scalars;
        std::vector<VectorProperty> m_vectors;
    };

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

    // 仅 owner 线程访问；缓存值是不可变快照，不持有可变材质的引用。
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

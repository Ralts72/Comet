#pragma once

#include "asset/handle.h"
#include "common/export.h"

#include <cstdint>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Comet {
    class Material;
    class Texture;
    class ShaderInterface;

    class COMET_API MaterialLayout {
    public:
        struct TextureProperty {
            std::string name;
            uint32_t binding;
            std::string display_name;
            std::string shader_name;
        };
        struct ScalarProperty {
            std::string name;
            uint32_t offset;
            float default_value;
            float min_value = 0;
            float max_value = 0;
            float step = 0.01f;
            std::string display_name;
            std::string shader_name;
        };
        struct VectorProperty {
            enum class Semantic { Vector, Color };
            std::string name;
            uint32_t offset;
            std::array<float, 4> default_value;
            Semantic semantic = Semantic::Vector;
            std::string display_name;
            std::string shader_name;
        };

        [[nodiscard]] static std::shared_ptr<const MaterialLayout> find_builtin(
            std::string_view name);
        [[nodiscard]] static std::shared_ptr<const MaterialLayout> reflect(
            const std::shared_ptr<const MaterialLayout>& metadata,
            const ShaderInterface& shader);

        MaterialLayout(std::string name, uint64_t revision,
            std::vector<TextureProperty> textures, uint32_t parameter_size = 0,
            std::vector<ScalarProperty> scalars = {},
            std::vector<VectorProperty> vectors = {}, uint32_t parameter_binding = 0);
        MaterialLayout(const MaterialLayout&) = default;
        MaterialLayout& operator=(const MaterialLayout&) = delete;

        [[nodiscard]] const std::string& get_name() const { return m_name; }
        [[nodiscard]] uint64_t get_revision() const { return m_revision; }
        [[nodiscard]] const std::vector<TextureProperty>& get_textures() const {
            return m_textures;
        }
        [[nodiscard]] uint32_t get_parameter_size() const { return m_parameter_size; }
        [[nodiscard]] uint32_t get_parameter_binding() const {
            return m_parameter_binding;
        }
        [[nodiscard]] const std::vector<ScalarProperty>& get_scalars() const {
            return m_scalars;
        }
        [[nodiscard]] const std::vector<VectorProperty>& get_vectors() const {
            return m_vectors;
        }

        void validate(const ShaderInterface& shader, uint32_t material_set = 1) const;

    private:
        std::string m_name;
        uint64_t m_revision;
        std::vector<TextureProperty> m_textures;
        uint32_t m_parameter_size;
        uint32_t m_parameter_binding;
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

    // 仅 owner 线程访问；输出不可变快照，源引用仅用于 revision 检查和重绑定。
    class COMET_API MaterialRuntimeCache {
    public:
        [[nodiscard]] std::shared_ptr<const PreparedMaterial> prepare(AssetHandle handle,
            const std::shared_ptr<const Material>& material,
            const std::shared_ptr<const MaterialLayout>& layout);

        void collect_unused();
        [[nodiscard]] std::shared_ptr<const PreparedMaterial> rebind(
            AssetHandle handle, const std::shared_ptr<const MaterialLayout>& layout);
        void swap(MaterialRuntimeCache& other) noexcept;

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

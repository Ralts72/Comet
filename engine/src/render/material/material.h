#pragma once

#include "common/export.h"
#include "asset/handle.h"
#include "core/math_utils.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace Comet {
    class Texture;

    class COMET_API Material {
    public:
        Material(std::string name, std::string template_name,
            AssetHandle shader_program = INVALID_ASSET_HANDLE);
        Material(const Material&) = delete;
        Material& operator=(const Material&) = delete;

        [[nodiscard]] const std::string& get_name() const { return m_name; }
        [[nodiscard]] uint64_t get_revision() const { return m_revision; }
        [[nodiscard]] const std::string& get_template_name() const { return m_template_name; }
        [[nodiscard]] AssetHandle get_shader_program() const { return m_shader_program; }

        void set_texture_property(const std::string& name, std::shared_ptr<Texture> texture);
        // 非有限值被拒绝；相同值成功但不改变版本。
        [[nodiscard]] bool set_scalar_property(const std::string& name, float value);
        [[nodiscard]] bool set_vector_property(const std::string& name, Math::Vec4 value);
        [[nodiscard]] std::optional<float> get_scalar_property(const std::string& name) const;
        [[nodiscard]] std::optional<Math::Vec4> get_vector_property(const std::string& name) const;

        [[nodiscard]] std::shared_ptr<Texture> get_texture_property(const std::string& name) const;

    private:
        std::string m_name;
        std::string m_template_name;
        AssetHandle m_shader_program;
        uint64_t m_revision = 1;
        std::map<std::string, std::shared_ptr<Texture>> m_texture_properties;
        std::map<std::string, float> m_scalar_properties;
        std::map<std::string, Math::Vec4> m_vector_properties;
    };
}

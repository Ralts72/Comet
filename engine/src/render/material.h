#pragma once

#include "common/export.h"

#include <map>
#include <array>
#include <optional>
#include <cstdint>
#include <memory>
#include <string>

namespace Comet {
    class Texture;

    class COMET_API Material {
    public:
        Material(std::string name, std::string template_name);
        Material(const Material&) = delete;
        Material& operator=(const Material&) = delete;

        [[nodiscard]] const std::string& get_name() const { return m_name; }
        [[nodiscard]] uint64_t get_revision() const { return m_revision; }
        [[nodiscard]] const std::string& get_template_name() const {
            return m_template_name;
        }

        void set_texture_property(
            const std::string& name, std::shared_ptr<Texture> texture);
        void set_scalar_property(const std::string& name, float value);
        void set_vector_property(const std::string& name, std::array<float, 4> value);
        [[nodiscard]] std::optional<float> get_scalar_property(
            const std::string& name) const;
        [[nodiscard]] std::optional<std::array<float, 4>> get_vector_property(
            const std::string& name) const;

        [[nodiscard]] std::shared_ptr<Texture> get_texture_property(
            const std::string& name) const;
        [[nodiscard]] const std::map<std::string, std::shared_ptr<Texture>>&
        get_texture_properties() const {
            return m_texture_properties;
        }

    private:
        std::string m_name;
        std::string m_template_name;
        uint64_t m_revision = 1;
        std::map<std::string, std::shared_ptr<Texture>> m_texture_properties;
        std::map<std::string, float> m_scalar_properties;
        std::map<std::string, std::array<float, 4>> m_vector_properties;
    };
}

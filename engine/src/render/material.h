#pragma once

#include "common/export.h"

#include <map>
#include <memory>
#include <string>

namespace Comet {
    class Texture;

    class COMET_API Material {
    public:
        Material(std::string name, std::string template_name);

        [[nodiscard]] const std::string& get_name() const { return m_name; }
        [[nodiscard]] const std::string& get_template_name() const {
            return m_template_name;
        }

        void set_texture_property(
            const std::string& name, std::shared_ptr<Texture> texture);

        [[nodiscard]] std::shared_ptr<Texture> get_texture_property(
            const std::string& name) const;
        [[nodiscard]] const std::map<std::string, std::shared_ptr<Texture>>&
        get_texture_properties() const {
            return m_texture_properties;
        }

    private:
        std::string m_name;
        std::string m_template_name;
        std::map<std::string, std::shared_ptr<Texture>> m_texture_properties;
    };
}

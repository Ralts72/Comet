#include "render/material.h"

#include <utility>

namespace Comet {
    Material::Material(std::string name, std::string template_name)
        : m_name(std::move(name)),
          m_template_name(std::move(template_name)) {}

    void Material::set_texture_property(
        const std::string& name,
        std::shared_ptr<Texture> texture) {
        m_texture_properties[name] = std::move(texture);
    }

    std::shared_ptr<Texture> Material::get_texture_property(
        const std::string& name) const {
        const auto property = m_texture_properties.find(name);
        return property == m_texture_properties.end()
            ? nullptr
            : property->second;
    }
}

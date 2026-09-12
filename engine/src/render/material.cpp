#include "render/material.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace Comet {
    Material::Material(std::string name, std::string template_name)
        : m_name(std::move(name)), m_template_name(std::move(template_name)) {}

    void Material::set_texture_property(
        const std::string& name, std::shared_ptr<Texture> texture) {
        const auto found = m_texture_properties.find(name);
        if(found != m_texture_properties.end() && found->second == texture) {
            return;
        }
        m_texture_properties[name] = std::move(texture);
        ++m_revision;
    }

    std::shared_ptr<Texture> Material::get_texture_property(
        const std::string& name) const {
        const auto property = m_texture_properties.find(name);
        return property == m_texture_properties.end() ? nullptr : property->second;
    }

    void Material::set_scalar_property(const std::string& name, const float value) {
        if(!std::isfinite(value)) {
            throw std::invalid_argument("Material scalar must be finite");
        }
        const auto found = m_scalar_properties.find(name);
        if(found != m_scalar_properties.end() && found->second == value)
            return;
        m_scalar_properties[name] = value;
        ++m_revision;
    }

    void Material::set_vector_property(const std::string& name, const Math::Vec4 value) {
        if(!Math::is_finite(value)) {
            throw std::invalid_argument("Material vector must be finite");
        }
        const auto found = m_vector_properties.find(name);
        if(found != m_vector_properties.end() && found->second == value)
            return;
        m_vector_properties[name] = value;
        ++m_revision;
    }

    std::optional<float> Material::get_scalar_property(const std::string& name) const {
        const auto found = m_scalar_properties.find(name);
        if(found == m_scalar_properties.end())
            return std::nullopt;
        return found->second;
    }

    std::optional<Math::Vec4> Material::get_vector_property(
        const std::string& name) const {
        const auto found = m_vector_properties.find(name);
        if(found == m_vector_properties.end())
            return std::nullopt;
        return found->second;
    }
}

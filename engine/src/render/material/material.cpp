#include "render/material/material.h"

#include <cmath>
#include <utility>

namespace Comet {
    Material::Material(
        std::string name, std::string template_name, const AssetHandle shader_program)
        : m_name(std::move(name)), m_template_name(std::move(template_name)),
          m_shader_program(shader_program) {}

    std::shared_ptr<const Material> Material::snapshot() const {
        if(auto copy = m_snapshot.lock())
            return copy;
        auto copy = std::make_shared<Material>(m_name, m_template_name, m_shader_program);
        copy->m_revision = m_revision;
        copy->m_texture_properties = m_texture_properties;
        copy->m_scalar_properties = m_scalar_properties;
        copy->m_vector_properties = m_vector_properties;
        copy->m_snapshot = copy;
        m_snapshot = copy;
        return copy;
    }

    void Material::set_texture_property(const std::string& name, std::shared_ptr<Texture> texture) {
        const auto found = m_texture_properties.find(name);
        if(found != m_texture_properties.end() && found->second == texture) {
            return;
        }
        m_texture_properties[name] = std::move(texture);
        ++m_revision;
        m_snapshot.reset();
    }

    std::shared_ptr<Texture> Material::get_texture_property(const std::string& name) const {
        const auto property = m_texture_properties.find(name);
        return property == m_texture_properties.end() ? nullptr : property->second;
    }

    bool Material::set_scalar_property(const std::string& name, const float value) {
        if(!std::isfinite(value)) {
            return false;
        }
        const auto found = m_scalar_properties.find(name);
        if(found != m_scalar_properties.end() && found->second == value)
            return true;
        m_scalar_properties[name] = value;
        ++m_revision;
        m_snapshot.reset();
        return true;
    }

    bool Material::set_vector_property(const std::string& name, const Math::Vec4 value) {
        if(!Math::is_finite(value)) {
            return false;
        }
        const auto found = m_vector_properties.find(name);
        if(found != m_vector_properties.end() && found->second == value)
            return true;
        m_vector_properties[name] = value;
        ++m_revision;
        m_snapshot.reset();
        return true;
    }

    std::optional<float> Material::get_scalar_property(const std::string& name) const {
        const auto found = m_scalar_properties.find(name);
        if(found == m_scalar_properties.end())
            return std::nullopt;
        return found->second;
    }

    std::optional<Math::Vec4> Material::get_vector_property(const std::string& name) const {
        const auto found = m_vector_properties.find(name);
        if(found == m_vector_properties.end())
            return std::nullopt;
        return found->second;
    }
}

#include "material.h"
#include "common/logger.h"

namespace Comet {

    void MaterialConfig::set_blend_op(const BlendOp color_op, const BlendOp alpha_op) {
        m_color_blend_op = color_op;
        m_alpha_blend_op = alpha_op;
    }

    void MaterialConfig::set_blend_factors(const BlendFactor src_color, const BlendFactor dst_color,
                                          const BlendFactor src_alpha, const BlendFactor dst_alpha) {
        m_src_color_blend_factor = src_color;
        m_dst_color_blend_factor = dst_color;
        m_src_alpha_blend_factor = src_alpha;
        m_dst_alpha_blend_factor = dst_alpha;
    }

    Material::Material(const std::string& name, const MaterialConfig& config)
        : m_name(name), m_config(config) {}

    void Material::set_property(const std::string& name, const float value) {
        m_properties[name] = MaterialProperty{MaterialPropertyType::Float, name, value};
    }

    void Material::set_property(const std::string& name, const Math::Vec2& value) {
        m_properties[name] = MaterialProperty{MaterialPropertyType::Vec2, name, value};
    }

    void Material::set_property(const std::string& name, const Math::Vec3& value) {
        m_properties[name] = MaterialProperty{MaterialPropertyType::Vec3, name, value};
    }

    void Material::set_property(const std::string& name, const Math::Vec4& value) {
        m_properties[name] = MaterialProperty{MaterialPropertyType::Vec4, name, value};
    }

    void Material::set_property(const std::string& name, int value) {
        m_properties[name] = MaterialProperty{MaterialPropertyType::Int, name, value};
    }

    void Material::set_property_sampler(const std::string& name, const std::shared_ptr<Sampler>& sampler) {
        m_properties[name] = MaterialProperty{MaterialPropertyType::Sampler, name, sampler};
    }

    bool Material::has_property(const std::string& name) const {
        return m_properties.contains(name);
    }

    const MaterialProperty& Material::get_property(const std::string& name) const {
        if (m_properties.contains(name)) {
            return m_properties.at(name);
        }
        LOG_FATAL("Material property '{}' not found in material '{}'", name, m_name);
    }

    // ====================== MaterialInstance ======================

    MaterialInstance::MaterialInstance(std::shared_ptr<Material> material)
        : m_material(std::move(material)) {}

    void MaterialInstance::set_property(const std::string& name, float value) {
        m_property_overrides[name] = MaterialProperty{MaterialPropertyType::Float, name, value};
    }

    void MaterialInstance::set_property(const std::string& name, const Math::Vec2& value) {
        m_property_overrides[name] = MaterialProperty{MaterialPropertyType::Vec2, name, value};
    }

    void MaterialInstance::set_property(const std::string& name, const Math::Vec3& value) {
        m_property_overrides[name] = MaterialProperty{MaterialPropertyType::Vec3, name, value};
    }

    void MaterialInstance::set_property(const std::string& name, const Math::Vec4& value) {
        m_property_overrides[name] = MaterialProperty{MaterialPropertyType::Vec4, name, value};
    }

    void MaterialInstance::set_property(const std::string& name, int value) {
        m_property_overrides[name] = MaterialProperty{MaterialPropertyType::Int, name, value};
    }

    void MaterialInstance::set_property_sampler(const std::string& name, const std::shared_ptr<Sampler>& sampler) {
        m_property_overrides[name] = MaterialProperty{MaterialPropertyType::Sampler, name, sampler};
    }

    bool MaterialInstance::has_property_override(const std::string& name) const {
        return m_property_overrides.contains(name);
    }

    const MaterialProperty& MaterialInstance::get_property_or_default(const std::string& name) const {
        if (m_property_overrides.contains(name)) {
            return m_property_overrides.at(name);
        }
        return m_material->get_property(name);
    }

    MaterialManager::MaterialManager(Device* device, ShaderManager* shader_manager, SamplerManager* sampler_manager) {
        // Parameters kept for future extensibility
    }

    std::shared_ptr<Material> MaterialManager::create_material(const std::string& name, const MaterialConfig& config) {
        if (m_materials.contains(name)) {
            LOG_WARN("Material '{}' already exists, returning existing material", name);
            return m_materials.at(name);
        }
        auto material = std::make_shared<Material>(name, config);
        m_materials[name] = material;
        return material;
    }

    std::shared_ptr<Material> MaterialManager::get_material(const std::string& name) const {
        if (m_materials.contains(name)) {
            return m_materials.at(name);
        }
        LOG_ERROR("Material '{}' not found", name);
        return nullptr;
    }

    std::shared_ptr<MaterialInstance> MaterialManager::create_instance(const std::string& material_name) const {
        auto material = get_material(material_name);
        return std::make_shared<MaterialInstance>(material);
    }

    void MaterialManager::clean_up() {
        m_materials.clear();
    }
}

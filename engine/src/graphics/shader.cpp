#include "shader.h"
#include "device.h"
#include "core/logger/logger.h"

namespace Comet {
    Shader::Shader(Device* device, const std::string& name, const std::vector<unsigned char>& spv_data, const ShaderLayout& layout)
    : m_device(device), m_name(name), m_spv_data(spv_data), m_layout(layout) {
        vk::ShaderModuleCreateInfo create_info{};
        create_info.codeSize = spv_data.size();
        create_info.pCode = reinterpret_cast<const uint32_t*>(spv_data.data());
        m_shader_module = m_device->get().createShaderModule(create_info);
        LOG_INFO("Vulkan shader module '{}' created successfully", name);
    }

    Shader::~Shader() {
        m_device->get().destroyShaderModule(m_shader_module);
    }

    std::shared_ptr<Shader> ShaderManager::load_shader(const std::string& name, const std::vector<unsigned char>& spv_data, const ShaderLayout& layout) {
        if(m_shaders.contains(name)) {
            LOG_DEBUG("shader {} already exists, skipping load", name);
            return m_shaders[name];
        }
        const auto shader = std::make_shared<Shader>(m_device, name, spv_data, layout);
        m_shaders[name] = shader;
        LOG_INFO("Shader '{}' loaded and cached successfully", name);
        return shader;
    }

    std::shared_ptr<Shader> ShaderManager::get_shader(const std::string& name) const {
        const auto it = m_shaders.find(name);
        if(it != m_shaders.end()) {
            return it->second;
        }
        LOG_WARN("no shader found for name: {}", name);
        return nullptr;
    }

    void ShaderManager::clean_up() {
        m_shaders.clear();
    }
}

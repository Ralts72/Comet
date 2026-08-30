#include "graphics/pipeline/shader.h"

#include "graphics/device.h"
#include "diagnostics/logger.h"

namespace Comet {
    Shader::Shader(Device& device, const std::string& name,
                   std::span<const std::uint32_t> spirv_words)
        : m_device(device) {
        if(spirv_words.empty()) {
            LOG_FATAL("Shader '{}' has empty SPIR-V bytecode", name);
        }
        vk::ShaderModuleCreateInfo create_info{};
        create_info.codeSize = spirv_words.size_bytes();
        create_info.pCode = spirv_words.data();
        m_shader_module = m_device.get().createShaderModule(create_info);
        LOG_INFO("Vulkan shader module '{}' created successfully", name);
    }

    Shader::~Shader() {
        m_device.get().destroyShaderModule(m_shader_module);
    }

    std::shared_ptr<Shader> ShaderManager::load_shader(
        const std::string& name,
        std::span<const std::uint32_t> spirv_words) {
        if(const auto it = m_shaders.find(name); it != m_shaders.end()) {
            LOG_DEBUG("shader {} already exists, skipping load", name);
            return it->second;
        }
        const auto shader = std::make_shared<Shader>(m_device, name, spirv_words);
        m_shaders[name] = shader;
        LOG_INFO("Shader '{}' loaded and cached successfully", name);
        return shader;
    }

}

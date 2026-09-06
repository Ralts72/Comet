#include "graphics/pipeline/shader.h"

#include "graphics/device.h"
#include "diagnostics/logger.h"

#include <stdexcept>
#include <algorithm>
#include <utility>

namespace Comet {
    void ShaderLayout::validate(const ShaderInterface& shader) const {
        for(const auto& required : shader.get_bindings()) {
            if(required.set >= descriptor_set_layouts.size()
                || !descriptor_set_layouts[required.set]) {
                throw std::invalid_argument("Shader descriptor set "
                                            + std::to_string(required.set)
                                            + " is missing from pipeline layout");
            }
        }
        for(uint32_t set = 0; set < descriptor_set_layouts.size(); ++set) {
            if(!descriptor_set_layouts[set])
                throw std::invalid_argument("Pipeline layout contains a null set layout");
            shader.validate_set(set, descriptor_set_layouts[set]->get_bindings());
        }
        std::vector<vk::PushConstantRange> ranges;
        for(const auto& range : push_constants) {
            if(!range)
                throw std::invalid_argument("Pipeline layout contains a null push range");
            ranges.push_back(range->get());
        }
        shader.validate_push_constants(ranges);
    }

    Shader::Shader(Device& device, const std::string& name,
        std::span<const std::uint32_t> spirv_words, std::string entry_point)
        : m_device(device), m_interface(spirv_words, std::move(entry_point)),
          m_code(spirv_words.begin(), spirv_words.end()) {
        vk::ShaderModuleCreateInfo create_info{};
        create_info.codeSize = spirv_words.size_bytes();
        create_info.pCode = spirv_words.data();
        m_shader_module = m_device.get().createShaderModule(create_info);
        LOG_INFO("Vulkan shader module '{}' created successfully", name);
    }

    Shader::~Shader() {
        m_device.get().destroyShaderModule(m_shader_module);
    }

    std::shared_ptr<Shader> ShaderManager::get_shader(const std::string& name) const {
        const auto found = m_shaders.find(name);
        if(found == m_shaders.end())
            return nullptr;
        return found->second;
    }

    std::shared_ptr<Shader> ShaderManager::load_shader_if_missing(
        const std::string& name, std::span<const uint32_t> words) {
        if(auto shader = get_shader(name))
            return shader;
        return load_shader(name, words);
    }

    ShaderManager::Snapshot ShaderManager::prepare_update(
        const Bytecodes& bytecodes) const {
        auto candidate = m_shaders;
        for(const auto& [name, source] : bytecodes) {
            auto old = get_shader(name);
            if(!old)
                throw std::invalid_argument("Cannot reload unknown Shader: " + name);
            if(old->get_code() == source.words
                && old->get_interface().get_entry_point() == source.entry_point)
                continue;
            auto shader = std::make_shared<Shader>(
                m_device, name, source.words, source.entry_point);
            if(!old->get_interface().has_same_layout(shader->get_interface()))
                throw std::invalid_argument(
                    "Shader layout change requires a layout rebuild: " + name);
            candidate.at(name) = std::move(shader);
        }
        return candidate;
    }

    void ShaderManager::publish_update(Snapshot& candidate) noexcept {
        m_shaders.swap(candidate);
    }

    std::shared_ptr<Shader> ShaderManager::load_shader(const std::string& name,
        std::span<const std::uint32_t> spirv_words, std::string entry_point) {
        if(const auto it = m_shaders.find(name); it != m_shaders.end()) {
            if(it->second->get_interface().get_entry_point() == entry_point
                && std::ranges::equal(it->second->get_code(), spirv_words)) {
                return it->second;
            }
        }
        const auto shader =
            std::make_shared<Shader>(m_device, name, spirv_words, std::move(entry_point));
        m_shaders[name] = shader;
        LOG_INFO("Shader '{}' loaded and cached successfully", name);
        return shader;
    }

}

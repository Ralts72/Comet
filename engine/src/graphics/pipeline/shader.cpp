#include "graphics/pipeline/shader.h"

#include "graphics/device.h"
#include "graphics/convert.h"
#include "diagnostics/logger.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Comet {
    namespace {
        bool compatible_descriptor_type(
            DescriptorType shader, vk::DescriptorType layout) {
            return Graphics::description_type_to_vk(shader) == layout
                   || (shader == DescriptorType::UniformBuffer
                       && layout == vk::DescriptorType::eUniformBufferDynamic)
                   || (shader == DescriptorType::StorageBuffer
                       && layout == vk::DescriptorType::eStorageBufferDynamic);
        }
    }

    void ShaderLayout::validate(const ShaderInterface& shader) const {
        for(const auto& set : descriptor_set_layouts) {
            if(!set)
                throw std::invalid_argument("Pipeline layout contains a null set layout");
        }
        for(const auto& range : push_constants) {
            if(!range)
                throw std::invalid_argument("Pipeline layout contains a null push range");
        }
        for(const auto& required : shader.get_bindings()) {
            if(required.set >= descriptor_set_layouts.size()) {
                throw std::invalid_argument("Shader descriptor set "
                                            + std::to_string(required.set)
                                            + " is missing from pipeline layout");
            }
            const auto& bindings = descriptor_set_layouts[required.set]->get_bindings();
            const auto found = std::ranges::find(
                bindings, required.binding, &vk::DescriptorSetLayoutBinding::binding);
            const std::string label = "Shader '" + shader.get_entry_point() + "' set "
                                      + std::to_string(required.set) + " binding "
                                      + std::to_string(required.binding);
            if(found == bindings.end())
                throw std::invalid_argument(label + " is missing from layout");
            if(!compatible_descriptor_type(required.type, found->descriptorType))
                throw std::invalid_argument(label + " descriptor type mismatch");
            if(found->descriptorCount < required.count)
                throw std::invalid_argument(label + " descriptor count is too small");
            const auto stages = Graphics::shader_stage_to_vk(required.stages);
            if((found->stageFlags & stages) != stages)
                throw std::invalid_argument(label + " is not visible to shader stage");
        }
        for(const auto& required : shader.get_push_constants()) {
            const auto stages = Graphics::shader_stage_to_vk(required.stages);
            const bool covered =
                std::ranges::any_of(push_constants, [&](const auto& item) {
                    const auto range = item->get();
                    return (range.stageFlags & stages) == stages
                           && range.offset <= required.offset
                           && uint64_t(range.offset) + range.size
                                  >= uint64_t(required.offset) + required.size;
                });
            if(!covered) {
                throw std::invalid_argument("Shader '" + shader.get_entry_point()
                                            + "' push constant range is not covered");
            }
        }
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
        return shader;
    }

}

#include "graphics/pipeline/shader.h"

#include "graphics/device.h"
#include "graphics/convert.h"
#include "diagnostics/logger.h"

#include <algorithm>
#include <utility>

namespace Comet {
    namespace {
        bool compatible_descriptor_type(DescriptorType shader, vk::DescriptorType layout) {
            return Graphics::description_type_to_vk(shader) == layout
                   || (shader == DescriptorType::UniformBuffer
                       && layout == vk::DescriptorType::eUniformBufferDynamic)
                   || (shader == DescriptorType::StorageBuffer
                       && layout == vk::DescriptorType::eStorageBufferDynamic);
        }
    }

    Result<void> ShaderLayout::validate(const ShaderInterface& shader) const {
        for(const auto& set : descriptor_set_layouts) {
            if(!set)
                return Result<void>::failure("Pipeline layout contains a null set layout");
        }
        for(const auto& range : push_constants) {
            if(!range)
                return Result<void>::failure("Pipeline layout contains a null push range");
        }
        for(const auto& required : shader.get_bindings()) {
            if(required.set >= descriptor_set_layouts.size()) {
                return Result<void>::failure("Shader descriptor set " + std::to_string(required.set)
                                             + " is missing from pipeline layout");
            }
            const auto& bindings = descriptor_set_layouts[required.set]->get_bindings();
            const auto found = std::ranges::find(
                bindings, required.binding, &vk::DescriptorSetLayoutBinding::binding);
            const std::string label = "Shader '" + shader.get_entry_point() + "' set "
                                      + std::to_string(required.set) + " binding "
                                      + std::to_string(required.binding);
            if(found == bindings.end())
                return Result<void>::failure(label + " is missing from layout");
            if(!compatible_descriptor_type(required.type, found->descriptorType))
                return Result<void>::failure(label + " descriptor type mismatch");
            if(found->descriptorCount < required.count)
                return Result<void>::failure(label + " descriptor count is too small");
            const auto stages = Graphics::shader_stage_to_vk(required.stages);
            if((found->stageFlags & stages) != stages)
                return Result<void>::failure(label + " is not visible to shader stage");
        }
        for(const auto& required : shader.get_push_constants()) {
            const auto stages = Graphics::shader_stage_to_vk(required.stages);
            const bool covered = std::ranges::any_of(push_constants, [&](const auto& item) {
                const auto range = item->get();
                return (range.stageFlags & stages) == stages && range.offset <= required.offset
                       && uint64_t(range.offset) + range.size
                              >= uint64_t(required.offset) + required.size;
            });
            if(!covered) {
                return Result<void>::failure(
                    "Shader '" + shader.get_entry_point() + "' push constant range is not covered");
            }
        }
        return Result<void>::success();
    }

    Shader::Shader(Device& device, const std::string& name,
        std::span<const std::uint32_t> spirv_words, ShaderInterface interface)
        : m_device(device), m_interface(std::move(interface)),
          m_code(spirv_words.begin(), spirv_words.end()) {
        vk::ShaderModuleCreateInfo create_info{};
        create_info.codeSize = spirv_words.size_bytes();
        create_info.pCode = spirv_words.data();
        m_shader_module = m_device.get().createShaderModule(create_info);
        LOG_INFO("Vulkan shader module '{}' created successfully", name);
    }

    Result<std::shared_ptr<Shader>> Shader::create(Device& device, const std::string& name,
        std::span<const uint32_t> words, std::string entry_point) {
        auto interface = ShaderInterface::reflect(words, std::move(entry_point));
        if(!interface)
            return Result<std::shared_ptr<Shader>>::failure(interface.error());
        return Result<std::shared_ptr<Shader>>::success(
            std::shared_ptr<Shader>(new Shader(device, name, words, std::move(interface).value())));
    }

    Shader::~Shader() {
        m_device.get().destroyShaderModule(m_shader_module);
    }

    Result<std::shared_ptr<Shader>> ShaderManager::load_shader(const std::string& name,
        std::span<const std::uint32_t> spirv_words, std::string entry_point) {
        if(const auto it = m_shaders.find(name); it != m_shaders.end()) {
            if(it->second->get_interface().get_entry_point() == entry_point
                && std::ranges::equal(it->second->get_code(), spirv_words)) {
                return Result<std::shared_ptr<Shader>>::success(it->second);
            }
        }
        auto shader = Shader::create(m_device, name, spirv_words, std::move(entry_point));
        if(shader)
            m_shaders[name] = shader.value();
        return shader;
    }

}

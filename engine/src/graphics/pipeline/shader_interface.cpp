#include "graphics/pipeline/shader_interface.h"

#include <spirv_reflect.h>

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Comet {
    namespace {
        void require_success(const SpvReflectResult result) {
            if(result != SPV_REFLECT_RESULT_SUCCESS) {
                throw std::invalid_argument(
                    "SPIR-V reflection failed: " + std::to_string(result));
            }
        }

        void validate_word_ranges(std::span<const uint32_t> words) {
            if(words.size() < 5 || words[0] != SpvMagicNumber)
                throw std::invalid_argument("Invalid SPIR-V header");
            // 先拒绝截断指令；反射库不是字节码校验器，Debug 中会断言越界。
            for(size_t offset = 5; offset < words.size();) {
                const auto count = words[offset] >> 16;
                if(count == 0 || count > words.size() - offset)
                    throw std::invalid_argument("Invalid SPIR-V instruction word range");
                offset += count;
            }
        }

        vk::Format member_format(const SpvReflectBlockVariable& member) {
            if(member.member_count || member.array.dims_count
                || member.numeric.matrix.column_count || member.numeric.scalar.width != 32
                || !member.type_description) {
                return vk::Format::eUndefined;
            }
            const auto components = std::max(1u, member.numeric.vector.component_count);
            if(components > 4)
                return vk::Format::eUndefined;
            const auto type = member.type_description->type_flags;
            if(type & SPV_REFLECT_TYPE_FLAG_FLOAT) {
                constexpr std::array formats{vk::Format::eR32Sfloat,
                    vk::Format::eR32G32Sfloat, vk::Format::eR32G32B32Sfloat,
                    vk::Format::eR32G32B32A32Sfloat};
                return formats[components - 1];
            }
            if(type & SPV_REFLECT_TYPE_FLAG_INT) {
                constexpr std::array signed_formats{vk::Format::eR32Sint,
                    vk::Format::eR32G32Sint, vk::Format::eR32G32B32Sint,
                    vk::Format::eR32G32B32A32Sint};
                constexpr std::array unsigned_formats{vk::Format::eR32Uint,
                    vk::Format::eR32G32Uint, vk::Format::eR32G32B32Uint,
                    vk::Format::eR32G32B32A32Uint};
                if(member.numeric.scalar.signedness)
                    return signed_formats[components - 1];
                return unsigned_formats[components - 1];
            }
            return vk::Format::eUndefined;
        }

        bool compatible_descriptor_type(
            vk::DescriptorType shader, vk::DescriptorType layout) {
            return shader == layout
                   || (shader == vk::DescriptorType::eUniformBuffer
                       && layout == vk::DescriptorType::eUniformBufferDynamic)
                   || (shader == vk::DescriptorType::eStorageBuffer
                       && layout == vk::DescriptorType::eStorageBufferDynamic);
        }
    }

    ShaderInterface::ShaderInterface(
        std::span<const uint32_t> spirv_words, std::string entry_point)
        : m_entry_point(std::move(entry_point)) {
        if(spirv_words.size() < 5 || m_entry_point.empty()
            || m_entry_point.find('\0') != std::string::npos) {
            throw std::invalid_argument("Shader requires SPIR-V and a valid entry point");
        }
        validate_word_ranges(spirv_words);
        spv_reflect::ShaderModule module(spirv_words.size_bytes(), spirv_words.data());
        require_success(module.GetResult());
        const auto* entry =
            spvReflectGetEntryPoint(&module.GetShaderModule(), m_entry_point.c_str());
        if(!entry) {
            throw std::invalid_argument("SPIR-V entry point not found: " + m_entry_point);
        }
        m_stage = static_cast<vk::ShaderStageFlagBits>(entry->shader_stage);
        uint32_t count = 0;
        require_success(module.EnumerateEntryPointDescriptorBindings(
            m_entry_point.c_str(), &count, nullptr));
        std::vector<SpvReflectDescriptorBinding*> bindings(count);
        require_success(module.EnumerateEntryPointDescriptorBindings(
            m_entry_point.c_str(), &count, bindings.data()));
        for(const auto* source : bindings) {
            if(source->count == 0) {
                throw std::invalid_argument(
                    "Runtime descriptor arrays are not supported");
            }
            DescriptorBinding binding{source->set, source->binding,
                static_cast<vk::DescriptorType>(source->descriptor_type), source->count,
                m_stage, source->block.padded_size, {}};
            for(uint32_t index = 0; index < source->block.member_count; ++index) {
                const auto& member = source->block.members[index];
                std::string name;
                if(member.name)
                    name = member.name;
                binding.members.push_back(
                    {std::move(name), member.offset, member.size, member_format(member)});
            }
            m_bindings.push_back(std::move(binding));
        }
        std::ranges::sort(m_bindings, {},
            [](const auto& binding) { return std::pair(binding.set, binding.binding); });
        require_success(module.EnumerateEntryPointPushConstantBlocks(
            m_entry_point.c_str(), &count, nullptr));
        std::vector<SpvReflectBlockVariable*> blocks(count);
        require_success(module.EnumerateEntryPointPushConstantBlocks(
            m_entry_point.c_str(), &count, blocks.data()));
        for(const auto* block : blocks) {
            // 块 size 包含起始 offset 和尾部 padding；Vulkan 范围只需覆盖成员字节。
            uint64_t end = block->offset;
            for(uint32_t index = 0; index < block->member_count; ++index) {
                const auto& member = block->members[index];
                end = std::max(end, uint64_t(member.offset) + member.size);
            }
            if(end <= block->offset || end > std::numeric_limits<uint32_t>::max())
                throw std::invalid_argument("Invalid SPIR-V push constant block");
            m_push_constants.emplace_back(
                m_stage, block->offset, static_cast<uint32_t>(end) - block->offset);
        }
    }

    void ShaderInterface::validate_set(const uint32_t set,
        std::span<const vk::DescriptorSetLayoutBinding> bindings) const {
        for(const auto& required : m_bindings) {
            if(required.set != set)
                continue;
            const auto found = std::ranges::find(
                bindings, required.binding, &vk::DescriptorSetLayoutBinding::binding);
            const std::string label = "Shader '" + m_entry_point + "' set "
                                      + std::to_string(set) + " binding "
                                      + std::to_string(required.binding);
            if(found == bindings.end())
                throw std::invalid_argument(label + " is missing from layout");
            if(!compatible_descriptor_type(required.type, found->descriptorType))
                throw std::invalid_argument(label + " descriptor type mismatch");
            if(found->descriptorCount < required.count)
                throw std::invalid_argument(label + " descriptor count is too small");
            if((found->stageFlags & required.stages) != required.stages)
                throw std::invalid_argument(label + " is not visible to shader stage");
        }
    }

    void ShaderInterface::validate_push_constants(
        std::span<const vk::PushConstantRange> ranges) const {
        for(const auto& required : m_push_constants) {
            const bool covered = std::ranges::any_of(ranges, [&](const auto& range) {
                return (range.stageFlags & required.stageFlags) == required.stageFlags
                       && range.offset <= required.offset
                       && uint64_t(range.offset) + range.size
                              >= uint64_t(required.offset) + required.size;
            });
            if(!covered) {
                throw std::invalid_argument(
                    "Shader '" + m_entry_point + "' push constant range is not covered");
            }
        }
    }
}

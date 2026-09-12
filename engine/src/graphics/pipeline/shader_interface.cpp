#include "graphics/pipeline/shader_interface.h"

#include <spirv_reflect.h>

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>
#include <cstring>
#include <unordered_set>

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
            // Reject truncated instructions before reflection; this is not a full validator.
            for(size_t offset = 5; offset < words.size();) {
                const auto count = words[offset] >> 16;
                if(count == 0 || count > words.size() - offset)
                    throw std::invalid_argument("Invalid SPIR-V instruction word range");
                offset += count;
            }
        }

        void validate_fixed_array_lengths(std::span<const uint32_t> words) {
            std::unordered_set<uint32_t> constants;
            std::vector<uint32_t> lengths;
            for(size_t offset = 5; offset < words.size();) {
                const auto count = words[offset] >> 16;
                const auto op = static_cast<SpvOp>(words[offset] & 0xffff);
                if(op == SpvOpConstant || op == SpvOpTypeArray) {
                    if(count < 4)
                        throw std::invalid_argument("Invalid SPIR-V constant or array");
                    if(op == SpvOpConstant)
                        constants.insert(words[offset + 2]);
                    else
                        lengths.push_back(words[offset + 3]);
                }
                offset += count;
            }
            for(const auto length : lengths) {
                if(!constants.contains(length))
                    throw std::invalid_argument(
                        "Specialization-dependent array lengths are not supported; use a compile-time define variant");
            }
        }

        ShaderInterface::ConstantValue constant_default(
            const SpvReflectSpecializationConstant& constant) {
            const auto* type = constant.type_description;
            if(!type || !constant.default_value || constant.default_value_size != 4)
                throw std::invalid_argument(
                    "Only bool and 32-bit specialization constants are supported");
            uint32_t bits;
            std::memcpy(&bits, constant.default_value, sizeof(bits));
            if(type->op == SpvOpTypeBool)
                return ShaderInterface::ConstantValue(bits != 0);
            if(type->traits.numeric.scalar.width != 32)
                throw std::invalid_argument(
                    "Only 32-bit numeric specialization constants are supported");
            if(type->op == SpvOpTypeFloat)
                return ShaderInterface::ConstantValue(std::bit_cast<float>(bits));
            if(type->op == SpvOpTypeInt) {
                if(type->traits.numeric.scalar.signedness)
                    return ShaderInterface::ConstantValue(std::bit_cast<int32_t>(bits));
                return ShaderInterface::ConstantValue(bits);
            }
            throw std::invalid_argument("Unsupported specialization constant type");
        }

        Format member_format(const SpvReflectBlockVariable& member) {
            if(member.member_count || member.array.dims_count
                || member.numeric.matrix.column_count || member.numeric.scalar.width != 32
                || !member.type_description) {
                return Format::UNDEFINED;
            }
            const auto components = std::max(1u, member.numeric.vector.component_count);
            if(components > 4)
                return Format::UNDEFINED;
            const auto type = member.type_description->type_flags;
            if(type & SPV_REFLECT_TYPE_FLAG_FLOAT) {
                constexpr std::array formats{Format::R32_SFLOAT, Format::R32G32_SFLOAT,
                    Format::R32G32B32_SFLOAT, Format::R32G32B32A32_SFLOAT};
                return formats[components - 1];
            }
            if(type & SPV_REFLECT_TYPE_FLAG_INT) {
                constexpr std::array signed_formats{Format::R32_SINT, Format::R32G32_SINT,
                    Format::R32G32B32_SINT, Format::R32G32B32A32_SINT};
                constexpr std::array unsigned_formats{Format::R32_UINT,
                    Format::R32G32_UINT, Format::R32G32B32_UINT,
                    Format::R32G32B32A32_UINT};
                if(member.numeric.scalar.signedness)
                    return signed_formats[components - 1];
                return unsigned_formats[components - 1];
            }
            return Format::UNDEFINED;
        }

        ShaderStage shader_stage(SpvReflectShaderStageFlagBits value) {
            switch(value) {
                case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:
                    return ShaderStage::Vertex;
                case SPV_REFLECT_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
                    return ShaderStage::TessellationControl;
                case SPV_REFLECT_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
                    return ShaderStage::TessellationEvaluation;
                case SPV_REFLECT_SHADER_STAGE_GEOMETRY_BIT:
                    return ShaderStage::Geometry;
                case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:
                    return ShaderStage::Fragment;
                case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:
                    return ShaderStage::Compute;
                default:
                    throw std::invalid_argument("Unsupported SPIR-V shader_stage");
            }
        }

        DescriptorType descriptor_type(SpvReflectDescriptorType value) {
            switch(value) {
                case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
                    return DescriptorType::Sampler;
                case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    return DescriptorType::CombinedImageSampler;
                case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    return DescriptorType::SampledImage;
                case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    return DescriptorType::StorageImage;
                case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                    return DescriptorType::UniformTexelBuffer;
                case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                    return DescriptorType::StorageTexelBuffer;
                case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    return DescriptorType::UniformBuffer;
                case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    return DescriptorType::StorageBuffer;
                case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                    return DescriptorType::UniformBufferDynamic;
                case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                    return DescriptorType::StoragesBufferDynamic;
                case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                    return DescriptorType::InputAttachment;
                default:
                    throw std::invalid_argument("Unsupported SPIR-V descriptor_type");
            }
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
        validate_fixed_array_lengths(spirv_words);
        spv_reflect::ShaderModule module(spirv_words.size_bytes(), spirv_words.data());
        require_success(module.GetResult());
        const auto* entry =
            spvReflectGetEntryPoint(&module.GetShaderModule(), m_entry_point.c_str());
        if(!entry) {
            throw std::invalid_argument("SPIR-V entry point not found: " + m_entry_point);
        }
        m_stage = shader_stage(entry->shader_stage);
        uint32_t count = 0;
        require_success(module.EnumerateSpecializationConstants(&count, nullptr));
        std::vector<SpvReflectSpecializationConstant*> constants(count);
        require_success(
            module.EnumerateSpecializationConstants(&count, constants.data()));
        for(const auto* constant : constants) {
            std::string name;
            if(constant->name)
                name = constant->name;
            m_specialization_constants.push_back(
                {constant->constant_id, std::move(name), constant_default(*constant)});
        }
        std::ranges::sort(m_specialization_constants, {}, &SpecializationConstant::id);
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
                descriptor_type(source->descriptor_type), source->count,
                Flags<ShaderStage>(m_stage), source->block.padded_size, {}};
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
            // Block size includes initial offset/padding; the range covers member bytes.
            uint64_t end = block->offset;
            for(uint32_t index = 0; index < block->member_count; ++index) {
                const auto& member = block->members[index];
                end = std::max(end, uint64_t(member.offset) + member.size);
            }
            if(end <= block->offset || end > std::numeric_limits<uint32_t>::max())
                throw std::invalid_argument("Invalid SPIR-V push constant block");
            m_push_constants.emplace_back(Flags<ShaderStage>(m_stage), block->offset,
                static_cast<uint32_t>(end) - block->offset);
        }
    }

    void ShaderInterface::canonicalize_specialization(Specialization& values) const {
        for(const auto& [id, value] : values) {
            bool found = false;
            for(const auto& constant : m_specialization_constants) {
                if(constant.id != id)
                    continue;
                found = true;
                if(constant.default_value.get_type() != value.get_type())
                    throw std::invalid_argument(
                        "Specialization constant " + std::to_string(id)
                        + " type mismatch in Shader '" + m_entry_point + "'");
            }
            if(!found)
                throw std::invalid_argument("Unknown specialization constant "
                    + std::to_string(id) + " in Shader '" + m_entry_point + "'");
        }
        std::erase_if(values, [&](const auto& entry) {
            return std::ranges::all_of(
                m_specialization_constants, [&](const auto& constant) {
                    return constant.id != entry.first || constant.default_value == entry.second;
                });
        });
    }
}

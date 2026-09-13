#include "graphics/pipeline/shader_interface.h"

#include <spirv_reflect.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>
#include <cstring>
#include <unordered_set>

namespace Comet {
    namespace {
        Result<void> validate_word_ranges(std::span<const uint32_t> words) {
            if(words.size() < 5 || words[0] != SpvMagicNumber)
                return Result<void>::failure("Invalid SPIR-V header");
            // 反射前拒绝截断指令，不代替完整字节码校验。
            for(size_t offset = 5; offset < words.size();) {
                const auto count = words[offset] >> 16;
                if(count == 0 || count > words.size() - offset)
                    return Result<void>::failure("Invalid SPIR-V instruction word range");
                offset += count;
            }
            return Result<void>::success();
        }

        Result<void> validate_fixed_array_lengths(std::span<const uint32_t> words) {
            std::unordered_set<uint32_t> constants;
            std::vector<uint32_t> lengths;
            for(size_t offset = 5; offset < words.size();) {
                const auto count = words[offset] >> 16;
                const auto op = static_cast<SpvOp>(words[offset] & 0xffff);
                if(op == SpvOpConstant || op == SpvOpTypeArray) {
                    if(count < 4)
                        return Result<void>::failure("Invalid SPIR-V constant or array");
                    if(op == SpvOpConstant)
                        constants.insert(words[offset + 2]);
                    else
                        lengths.push_back(words[offset + 3]);
                }
                offset += count;
            }
            for(const auto length : lengths) {
                if(!constants.contains(length))
                    return Result<void>::failure(
                        "Specialization-dependent array lengths are not supported; use a compile-time define variant");
            }
            return Result<void>::success();
        }

        Result<ShaderInterface::ConstantValue> constant_default(
            const SpvReflectSpecializationConstant& constant) {
            const auto* type = constant.type_description;
            if(!type || !constant.default_value || constant.default_value_size != 4)
                return Result<ShaderInterface::ConstantValue>::failure(
                    "Only bool and 32-bit specialization constants are supported");
            uint32_t bits;
            std::memcpy(&bits, constant.default_value, sizeof(bits));
            if(type->op == SpvOpTypeBool)
                return Result<ShaderInterface::ConstantValue>::success(
                    ShaderInterface::ConstantValue(bits != 0));
            if(type->traits.numeric.scalar.width != 32)
                return Result<ShaderInterface::ConstantValue>::failure(
                    "Only 32-bit numeric specialization constants are supported");
            if(type->op == SpvOpTypeFloat)
                return Result<ShaderInterface::ConstantValue>::success(
                    ShaderInterface::ConstantValue(std::bit_cast<float>(bits)));
            if(type->op == SpvOpTypeInt) {
                if(type->traits.numeric.scalar.signedness)
                    return Result<ShaderInterface::ConstantValue>::success(
                        ShaderInterface::ConstantValue(std::bit_cast<int32_t>(bits)));
                return Result<ShaderInterface::ConstantValue>::success(
                    ShaderInterface::ConstantValue(bits));
            }
            return Result<ShaderInterface::ConstantValue>::failure(
                "Unsupported specialization constant type");
        }

        template<typename Variable> Format variable_format(const Variable& member) {
            if(member.member_count || member.array.dims_count || member.numeric.matrix.column_count
                || member.numeric.scalar.width != 32 || !member.type_description) {
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
                constexpr std::array unsigned_formats{Format::R32_UINT, Format::R32G32_UINT,
                    Format::R32G32B32_UINT, Format::R32G32B32A32_UINT};
                if(member.numeric.scalar.signedness)
                    return signed_formats[components - 1];
                return unsigned_formats[components - 1];
            }
            return Format::UNDEFINED;
        }

        Result<std::vector<ShaderInterface::StageVariable>> reflect_variables(
            std::span<SpvReflectInterfaceVariable* const> variables, std::string_view label) {
            using Variables = Result<std::vector<ShaderInterface::StageVariable>>;
            std::vector<ShaderInterface::StageVariable> result;
            for(const auto* variable : variables) {
                if(variable->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)
                    continue;
                const auto format = variable_format(*variable);
                if(format == Format::UNDEFINED
                    || variable->location == std::numeric_limits<uint32_t>::max()
                    || (variable->component != 0
                        && variable->component != std::numeric_limits<uint32_t>::max())) {
                    return Variables::failure(
                        std::string(label)
                        + " supports only location-based 32-bit scalar/vector interfaces: "
                        + (variable->name ? variable->name : "<unnamed>"));
                }
                result.push_back(
                    {variable->name ? variable->name : "", variable->location, format});
            }
            std::ranges::sort(result, {}, &ShaderInterface::StageVariable::location);
            for(size_t index = 1; index < result.size(); ++index) {
                if(result[index - 1].location == result[index].location)
                    return Variables::failure(std::string(label) + " has duplicate location "
                                              + std::to_string(result[index].location));
            }
            return Variables::success(std::move(result));
        }

        Result<ShaderStage> shader_stage(SpvReflectShaderStageFlagBits value) {
            switch(value) {
                case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:
                    return Result<ShaderStage>::success(ShaderStage::Vertex);
                case SPV_REFLECT_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
                    return Result<ShaderStage>::success(ShaderStage::TessellationControl);
                case SPV_REFLECT_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
                    return Result<ShaderStage>::success(ShaderStage::TessellationEvaluation);
                case SPV_REFLECT_SHADER_STAGE_GEOMETRY_BIT:
                    return Result<ShaderStage>::success(ShaderStage::Geometry);
                case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:
                    return Result<ShaderStage>::success(ShaderStage::Fragment);
                case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:
                    return Result<ShaderStage>::success(ShaderStage::Compute);
                default:
                    return Result<ShaderStage>::failure("Unsupported SPIR-V shader_stage");
            }
        }

        Result<DescriptorType> descriptor_type(SpvReflectDescriptorType value) {
            switch(value) {
                case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
                    return Result<DescriptorType>::success(DescriptorType::Sampler);
                case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    return Result<DescriptorType>::success(DescriptorType::CombinedImageSampler);
                case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    return Result<DescriptorType>::success(DescriptorType::SampledImage);
                case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    return Result<DescriptorType>::success(DescriptorType::StorageImage);
                case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                    return Result<DescriptorType>::success(DescriptorType::UniformTexelBuffer);
                case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                    return Result<DescriptorType>::success(DescriptorType::StorageTexelBuffer);
                case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    return Result<DescriptorType>::success(DescriptorType::UniformBuffer);
                case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    return Result<DescriptorType>::success(DescriptorType::StorageBuffer);
                case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                    return Result<DescriptorType>::success(DescriptorType::UniformBufferDynamic);
                case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                    return Result<DescriptorType>::success(DescriptorType::StoragesBufferDynamic);
                case SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                    return Result<DescriptorType>::success(DescriptorType::InputAttachment);
                default:
                    return Result<DescriptorType>::failure("Unsupported SPIR-V descriptor_type");
            }
        }
    }

    Result<ShaderInterface> ShaderInterface::reflect(
        std::span<const uint32_t> spirv_words, std::string entry_point) {
        ShaderInterface candidate;
        candidate.m_entry_point = std::move(entry_point);
        if(spirv_words.size() < 5 || candidate.m_entry_point.empty()
            || candidate.m_entry_point.find('\0') != std::string::npos) {
            return Result<ShaderInterface>::failure(
                "Shader requires SPIR-V and a valid entry point");
        }
        if(const auto checked = validate_word_ranges(spirv_words); !checked)
            return Result<ShaderInterface>::failure(checked.error());
        if(const auto checked = validate_fixed_array_lengths(spirv_words); !checked)
            return Result<ShaderInterface>::failure(checked.error());
        spv_reflect::ShaderModule module(spirv_words.size_bytes(), spirv_words.data());
        if(const auto status = module.GetResult(); status != SPV_REFLECT_RESULT_SUCCESS)
            return Result<ShaderInterface>::failure(
                "SPIR-V reflection failed: " + std::to_string(status));
        const auto* entry =
            spvReflectGetEntryPoint(&module.GetShaderModule(), candidate.m_entry_point.c_str());
        if(!entry) {
            return Result<ShaderInterface>::failure(
                "SPIR-V entry point not found: " + candidate.m_entry_point);
        }
        auto stage = shader_stage(entry->shader_stage);
        if(!stage)
            return Result<ShaderInterface>::failure(stage.error());
        candidate.m_stage = stage.value();
        auto inputs = reflect_variables({entry->input_variables, entry->input_variable_count},
            "Shader '" + candidate.m_entry_point + "' input");
        if(!inputs)
            return Result<ShaderInterface>::failure(inputs.error());
        auto outputs = reflect_variables({entry->output_variables, entry->output_variable_count},
            "Shader '" + candidate.m_entry_point + "' output");
        if(!outputs)
            return Result<ShaderInterface>::failure(outputs.error());
        candidate.m_inputs = std::move(inputs).value();
        candidate.m_outputs = std::move(outputs).value();
        uint32_t count = 0;
        if(const auto status = module.EnumerateSpecializationConstants(&count, nullptr);
            status != SPV_REFLECT_RESULT_SUCCESS)
            return Result<ShaderInterface>::failure(
                "SPIR-V reflection failed: " + std::to_string(status));
        std::vector<SpvReflectSpecializationConstant*> constants(count);
        if(const auto status = module.EnumerateSpecializationConstants(&count, constants.data());
            status != SPV_REFLECT_RESULT_SUCCESS)
            return Result<ShaderInterface>::failure(
                "SPIR-V reflection failed: " + std::to_string(status));
        for(const auto* constant : constants) {
            std::string name;
            if(constant->name)
                name = constant->name;
            auto value = constant_default(*constant);
            if(!value)
                return Result<ShaderInterface>::failure(value.error());
            candidate.m_specialization_constants.push_back(
                {constant->constant_id, std::move(name), value.value()});
        }
        std::ranges::sort(candidate.m_specialization_constants, {}, &SpecializationConstant::id);
        if(const auto status = module.EnumerateEntryPointDescriptorBindings(
               candidate.m_entry_point.c_str(), &count, nullptr);
            status != SPV_REFLECT_RESULT_SUCCESS)
            return Result<ShaderInterface>::failure(
                "SPIR-V reflection failed: " + std::to_string(status));
        std::vector<SpvReflectDescriptorBinding*> bindings(count);
        if(const auto status = module.EnumerateEntryPointDescriptorBindings(
               candidate.m_entry_point.c_str(), &count, bindings.data());
            status != SPV_REFLECT_RESULT_SUCCESS)
            return Result<ShaderInterface>::failure(
                "SPIR-V reflection failed: " + std::to_string(status));
        for(const auto* source : bindings) {
            if(source->count == 0) {
                return Result<ShaderInterface>::failure(
                    "Runtime descriptor arrays are not supported");
            }
            auto type = descriptor_type(source->descriptor_type);
            if(!type)
                return Result<ShaderInterface>::failure(type.error());
            DescriptorBinding binding{source->set, source->binding, type.value(), source->count,
                Flags<ShaderStage>(candidate.m_stage), source->block.padded_size, {}};
            for(uint32_t index = 0; index < source->block.member_count; ++index) {
                const auto& member = source->block.members[index];
                std::string name;
                if(member.name)
                    name = member.name;
                binding.members.push_back(
                    {std::move(name), member.offset, member.size, variable_format(member)});
            }
            candidate.m_bindings.push_back(std::move(binding));
        }
        std::ranges::sort(candidate.m_bindings, {},
            [](const auto& binding) { return std::pair(binding.set, binding.binding); });
        if(const auto status = module.EnumerateEntryPointPushConstantBlocks(
               candidate.m_entry_point.c_str(), &count, nullptr);
            status != SPV_REFLECT_RESULT_SUCCESS)
            return Result<ShaderInterface>::failure(
                "SPIR-V reflection failed: " + std::to_string(status));
        std::vector<SpvReflectBlockVariable*> blocks(count);
        if(const auto status = module.EnumerateEntryPointPushConstantBlocks(
               candidate.m_entry_point.c_str(), &count, blocks.data());
            status != SPV_REFLECT_RESULT_SUCCESS)
            return Result<ShaderInterface>::failure(
                "SPIR-V reflection failed: " + std::to_string(status));
        for(const auto* block : blocks) {
            // 块大小包含起始偏移和填充，实际范围按成员占用计算。
            uint64_t end = block->offset;
            for(uint32_t index = 0; index < block->member_count; ++index) {
                const auto& member = block->members[index];
                end = std::max(end, uint64_t(member.offset) + member.size);
            }
            if(end <= block->offset || end > std::numeric_limits<uint32_t>::max())
                return Result<ShaderInterface>::failure("Invalid SPIR-V push constant block");
            candidate.m_push_constants.emplace_back(Flags<ShaderStage>(candidate.m_stage),
                block->offset, static_cast<uint32_t>(end) - block->offset);
        }
        return Result<ShaderInterface>::success(std::move(candidate));
    }

    Result<void> ShaderInterface::validate_stage_link(const ShaderInterface& fragment) const {
        if(m_stage != ShaderStage::Vertex || fragment.m_stage != ShaderStage::Fragment)
            return Result<void>::failure("Stage link requires vertex and fragment interfaces");
        for(const auto& input : fragment.m_inputs) {
            const std::string label =
                "Fragment input '" + input.name + "' at location " + std::to_string(input.location);
            const auto output =
                std::ranges::find(m_outputs, input.location, &StageVariable::location);
            if(output == m_outputs.end())
                return Result<void>::failure(label + " has no vertex output");
            if(output->format != input.format)
                return Result<void>::failure(
                    label + " type does not match vertex output '" + output->name + "'");
        }
        return Result<void>::success();
    }

    Result<void> ShaderInterface::canonicalize_specialization(Specialization& values) const {
        for(const auto& [id, value] : values) {
            bool found = false;
            for(const auto& constant : m_specialization_constants) {
                if(constant.id != id)
                    continue;
                found = true;
                if(constant.default_value.get_type() != value.get_type())
                    return Result<void>::failure("Specialization constant " + std::to_string(id)
                                                 + " type mismatch in Shader '" + m_entry_point
                                                 + "'");
            }
            if(!found)
                return Result<void>::failure("Unknown specialization constant " + std::to_string(id)
                                             + " in Shader '" + m_entry_point + "'");
        }
        std::erase_if(values, [&](const auto& entry) {
            return std::ranges::all_of(m_specialization_constants, [&](const auto& constant) {
                return constant.id != entry.first || constant.default_value == entry.second;
            });
        });
        return Result<void>::success();
    }
}

#pragma once

#include "common/export.h"
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <bit>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace Comet {
    // 只拥有 CPU 值；不保留反射库指针、SPIR-V 输入或 Vulkan 对象。
    class COMET_API ShaderInterface {
    public:
        class ConstantValue {
        public:
            enum class Type { Boolean, SignedInteger, UnsignedInteger, Float };
            ConstantValue(bool value) : m_type(Type::Boolean), m_bits(value ? 1u : 0u) {}
            ConstantValue(int32_t value)
                : m_type(Type::SignedInteger), m_bits(std::bit_cast<uint32_t>(value)) {}
            ConstantValue(uint32_t value)
                : m_type(Type::UnsignedInteger), m_bits(value) {}
            ConstantValue(float value)
                : m_type(Type::Float), m_bits(std::bit_cast<uint32_t>(value)) {}
            [[nodiscard]] Type get_type() const { return m_type; }
            [[nodiscard]] uint32_t get_bits() const { return m_bits; }
            bool operator==(const ConstantValue&) const = default;

        private:
            Type m_type;
            uint32_t m_bits;
        };
        using Specialization = std::map<uint32_t, ConstantValue>;
        struct SpecializationConstant {
            uint32_t id;
            std::string name;
            ConstantValue default_value;
        };
        struct TypeShape {
            uint32_t type_flags = 0;
            uint32_t scalar_width = 0;
            uint32_t scalar_signedness = 0;
            uint32_t vector_components = 0;
            uint32_t matrix_rows = 0;
            uint32_t matrix_columns = 0;
            uint32_t matrix_stride = 0;
            bool row_major = false;
            uint32_t array_stride = 0;
            std::vector<uint32_t> array_dimensions;
            bool operator==(const TypeShape&) const = default;
        };
        struct BlockMember {
            std::string name;
            uint32_t offset;
            uint32_t size;
            // 当前参数编辑支持标量／向量；矩阵、数组、结构体保留为 Undefined。
            vk::Format format = vk::Format::eUndefined;
            TypeShape shape;
            std::vector<BlockMember> members;
            bool operator==(const BlockMember&) const = default;
        };
        struct StageVariable {
            uint32_t location;
            uint32_t component;
            int32_t built_in;
            uint32_t decorations;
            TypeShape shape;
            std::vector<StageVariable> members;
            bool operator==(const StageVariable&) const = default;
        };
        struct DescriptorBinding {
            uint32_t set;
            uint32_t binding;
            vk::DescriptorType type;
            uint32_t count;
            vk::ShaderStageFlags stages;
            uint32_t block_size;
            std::vector<BlockMember> members;
            bool operator==(const DescriptorBinding&) const = default;
        };

        explicit ShaderInterface(
            std::span<const uint32_t> spirv_words, std::string entry_point = "main");

        [[nodiscard]] const std::string& get_entry_point() const { return m_entry_point; }
        [[nodiscard]] vk::ShaderStageFlagBits get_stage() const { return m_stage; }
        [[nodiscard]] const std::vector<DescriptorBinding>& get_bindings() const {
            return m_bindings;
        }
        [[nodiscard]] const std::vector<vk::PushConstantRange>& get_push_constants()
            const {
            return m_push_constants;
        }

        void validate_set(
            uint32_t set, std::span<const vk::DescriptorSetLayoutBinding> bindings) const;
        void validate_push_constants(std::span<const vk::PushConstantRange> ranges) const;
        [[nodiscard]] const std::vector<SpecializationConstant>&
        get_specialization_constants() const {
            return m_specialization_constants;
        }
        // 校验类型/ID，并移除与默认位模式相同的显式覆盖。
        void canonicalize_specialization(Specialization& values) const;
        [[nodiscard]] bool has_same_layout(const ShaderInterface& other) const;

    private:
        std::string m_entry_point;
        vk::ShaderStageFlagBits m_stage;
        std::vector<DescriptorBinding> m_bindings;
        std::vector<vk::PushConstantRange> m_push_constants;
        std::vector<BlockMember> m_push_members;
        std::vector<StageVariable> m_inputs;
        std::vector<StageVariable> m_outputs;
        std::vector<SpecializationConstant> m_specialization_constants;
    };
}

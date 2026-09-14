#pragma once

#include "common/export.h"
#include "common/result.h"
#include "graphics/enums.h"

#include <cstdint>
#include <bit>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Comet {
    class COMET_API ShaderInterface {
    public:
        class ConstantValue {
        public:
            enum class Type { Boolean, SignedInteger, UnsignedInteger, Float };
            ConstantValue(bool value) : m_type(Type::Boolean), m_bits(value ? 1u : 0u) {}
            ConstantValue(int32_t value)
                : m_type(Type::SignedInteger), m_bits(std::bit_cast<uint32_t>(value)) {}
            ConstantValue(uint32_t value) : m_type(Type::UnsignedInteger), m_bits(value) {}
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
            enum class Scalar { Unknown, Boolean, SignedInteger, UnsignedInteger, Float };
            Scalar scalar = Scalar::Unknown;
            uint32_t width = 0;
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
            // 数组、矩阵和结构体保持 UNDEFINED。
            Format format = Format::UNDEFINED;
            TypeShape shape;
            std::vector<BlockMember> members;
            bool operator==(const BlockMember&) const = default;
        };
        struct DescriptorBinding {
            struct SampledImage {
                enum class Dimension { Unknown, One, Two, Three, Cube, Rectangle, Buffer, Subpass };
                Dimension dimension = Dimension::Unknown;
                TypeShape::Scalar scalar = TypeShape::Scalar::Unknown;
                uint32_t width = 0;
                bool arrayed = false;
                bool multisampled = false;
                bool depth = false;
                [[nodiscard]] bool is_float_2d() const;
                bool operator==(const SampledImage&) const = default;
            };
            uint32_t set;
            uint32_t binding;
            DescriptorType type;
            uint32_t count;
            Flags<ShaderStage> stages;
            uint32_t block_size;
            std::vector<BlockMember> members;
            std::optional<SampledImage> sampled_image;
            std::string name;
            bool operator==(const DescriptorBinding&) const = default;
        };

        struct PushConstant {
            Flags<ShaderStage> stages;
            uint32_t offset;
            uint32_t size;
            std::vector<BlockMember> members;
            bool operator==(const PushConstant&) const = default;
        };

        struct StageVariable {
            std::string name;
            uint32_t location;
            Format format;
        };

        static Result<ShaderInterface> reflect(
            std::span<const uint32_t> spirv_words, std::string entry_point = "main");

        [[nodiscard]] const std::string& get_entry_point() const { return m_entry_point; }
        [[nodiscard]] ShaderStage get_stage() const { return m_stage; }
        [[nodiscard]] const std::vector<StageVariable>& get_inputs() const { return m_inputs; }
        [[nodiscard]] const std::vector<StageVariable>& get_outputs() const { return m_outputs; }
        Result<void> validate_stage_link(const ShaderInterface& fragment) const;
        // 固定资源契约的保守比较；阶段连通性由 validate_stage_link 独立检查。
        [[nodiscard]] bool has_same_resource_layout(const ShaderInterface& other,
            std::optional<uint32_t> ignored_descriptor_set = std::nullopt) const;
        [[nodiscard]] const std::vector<DescriptorBinding>& get_bindings() const {
            return m_bindings;
        }
        [[nodiscard]] const std::vector<PushConstant>& get_push_constants() const {
            return m_push_constants;
        }
        [[nodiscard]] const std::vector<SpecializationConstant>& get_specialization_constants()
            const {
            return m_specialization_constants;
        }
        // 全部覆盖值校验成功后，才移除位模式与默认值相同的项。
        Result<void> canonicalize_specialization(Specialization& values) const;

    private:
        ShaderInterface() = default;
        std::string m_entry_point;
        ShaderStage m_stage;
        std::vector<StageVariable> m_inputs;
        std::vector<StageVariable> m_outputs;
        std::vector<DescriptorBinding> m_bindings;
        std::vector<PushConstant> m_push_constants;
        std::vector<SpecializationConstant> m_specialization_constants;
    };
}

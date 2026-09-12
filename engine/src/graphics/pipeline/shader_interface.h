#pragma once

#include "common/export.h"
#include "graphics/enums.h"

#include <cstdint>
#include <bit>
#include <map>
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

        struct BlockMember {
            std::string name;
            uint32_t offset;
            uint32_t size;
            // Arrays, matrices and structs remain Undefined.
            Format format = Format::UNDEFINED;
        };
        struct DescriptorBinding {
            uint32_t set;
            uint32_t binding;
            DescriptorType type;
            uint32_t count;
            Flags<ShaderStage> stages;
            uint32_t block_size;
            std::vector<BlockMember> members;
        };

        struct PushConstant {
            Flags<ShaderStage> stages;
            uint32_t offset;
            uint32_t size;
        };

        explicit ShaderInterface(
            std::span<const uint32_t> spirv_words, std::string entry_point = "main");

        [[nodiscard]] const std::string& get_entry_point() const { return m_entry_point; }
        [[nodiscard]] ShaderStage get_stage() const { return m_stage; }
        [[nodiscard]] const std::vector<DescriptorBinding>& get_bindings() const {
            return m_bindings;
        }
        [[nodiscard]] const std::vector<PushConstant>& get_push_constants() const {
            return m_push_constants;
        }
        [[nodiscard]] const std::vector<SpecializationConstant>&
        get_specialization_constants() const {
            return m_specialization_constants;
        }
        // Validate all overrides before removing bit-identical defaults.
        void canonicalize_specialization(Specialization& values) const;

    private:
        std::string m_entry_point;
        ShaderStage m_stage;
        std::vector<DescriptorBinding> m_bindings;
        std::vector<PushConstant> m_push_constants;
        std::vector<SpecializationConstant> m_specialization_constants;
    };
}

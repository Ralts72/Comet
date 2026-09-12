#pragma once

#include "common/export.h"
#include "graphics/enums.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Comet {
    // Owns CPU values, not reflection pointers, input bytecode or Vulkan objects.
    class COMET_API ShaderInterface {
    public:
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

    private:
        std::string m_entry_point;
        ShaderStage m_stage;
        std::vector<DescriptorBinding> m_bindings;
        std::vector<PushConstant> m_push_constants;
    };
}

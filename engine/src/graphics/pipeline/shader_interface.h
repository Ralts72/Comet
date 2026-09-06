#pragma once

#include "common/export.h"
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Comet {
    // 只拥有 CPU 值；不保留反射库指针、SPIR-V 输入或 Vulkan 对象。
    class COMET_API ShaderInterface {
    public:
        struct BlockMember {
            std::string name;
            uint32_t offset;
            uint32_t size;
            // 当前参数编辑支持标量／向量；矩阵、数组、结构体保留为 Undefined。
            vk::Format format = vk::Format::eUndefined;
        };
        struct DescriptorBinding {
            uint32_t set;
            uint32_t binding;
            vk::DescriptorType type;
            uint32_t count;
            vk::ShaderStageFlags stages;
            uint32_t block_size;
            std::vector<BlockMember> members;
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

    private:
        std::string m_entry_point;
        vk::ShaderStageFlagBits m_stage;
        std::vector<DescriptorBinding> m_bindings;
        std::vector<vk::PushConstantRange> m_push_constants;
    };
}

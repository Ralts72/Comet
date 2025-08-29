#pragma once
#include "vk_common.h"

namespace Comet {
    class Device;

    class DescriptorSetLayoutBindings {
    public:
        DescriptorSetLayoutBindings() = default;

        void add_binding(uint32_t binding, DescriptorType type, Flags<ShaderStage> stage_flags, uint32_t count = 1);

        [[nodiscard]] const std::vector<vk::DescriptorSetLayoutBinding>& get_bindings() const { return m_bindings; }
    private:
        std::vector<vk::DescriptorSetLayoutBinding> m_bindings;
    };

    class DescriptorPoolSizes {
    public:
        DescriptorPoolSizes() = default;

        void add_pool_size(DescriptorType type, uint32_t count);

        [[nodiscard]] const std::vector<vk::DescriptorPoolSize>& get_pool_sizes() const { return m_sizes; }
    private:
        std::vector<vk::DescriptorPoolSize> m_sizes;
    };

    class DescriptorSetLayout {
    public:
        DescriptorSetLayout(Device* device, const DescriptorSetLayoutBindings& bindings);
        ~DescriptorSetLayout();

        [[nodiscard]] vk::DescriptorSetLayout get() const { return m_descriptor_set_layout; }

    private:
        Device* m_device;
        vk::DescriptorSetLayout m_descriptor_set_layout;
    };

    class DescriptorSet {
    public:
        friend class DescriptorPool;
        DescriptorSet() = delete;

        [[nodiscard]] vk::DescriptorSet get() const { return m_descriptor_set; }
    private:
        explicit DescriptorSet(const vk::DescriptorSet descriptor_set): m_descriptor_set(descriptor_set) {}
        vk::DescriptorSet m_descriptor_set;
    };

    class DescriptorPool {
    public:
        DescriptorPool(Device* device, uint32_t max_sets, const DescriptorPoolSizes& pool_sizes);
        ~DescriptorPool();

        [[nodiscard]] std::vector<DescriptorSet> allocate_descriptor_set(const DescriptorSetLayout& set_layout, uint32_t count) const;
        [[nodiscard]] vk::DescriptorPool get() const { return m_descriptor_pool; }
    private:
        Device* m_device;
        vk::DescriptorPool m_descriptor_pool;
    };

    class PushConstantRange {
    public:
        PushConstantRange(ShaderStage stage, uint32_t offset, uint32_t size);
        [[nodiscard]] vk::PushConstantRange get() const { return m_const_range; }
    private:
        vk::PushConstantRange m_const_range;
    };
}
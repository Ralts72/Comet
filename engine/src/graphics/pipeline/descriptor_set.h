#pragma once
#include "graphics/vk_common.h"
#include "common/export.h"
#include "graphics/creation.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Comet {
    class Device;
    class Buffer;
    class ImageView;
    class Sampler;

    class COMET_API DescriptorSetLayoutBindings {
    public:
        DescriptorSetLayoutBindings() = default;

        void add_binding(uint32_t binding, DescriptorType type, Flags<ShaderStage> stage_flags,
            uint32_t count = 1);

        [[nodiscard]] const std::vector<vk::DescriptorSetLayoutBinding>& get_bindings() const {
            return m_bindings;
        }

    private:
        std::vector<vk::DescriptorSetLayoutBinding> m_bindings;
    };

    class COMET_API DescriptorPoolSizes {
    public:
        DescriptorPoolSizes() = default;

        void add_pool_size(DescriptorType type, uint32_t count);

        [[nodiscard]] const std::vector<vk::DescriptorPoolSize>& get_pool_sizes() const {
            return m_sizes;
        }

    private:
        std::vector<vk::DescriptorPoolSize> m_sizes;
    };

    class COMET_API DescriptorSetLayout {
    public:
        static Result<std::shared_ptr<DescriptorSetLayout>, GraphicsError> create(
            Device& device, const DescriptorSetLayoutBindings& bindings);
        ~DescriptorSetLayout() = default;

        DescriptorSetLayout(const DescriptorSetLayout&) = delete;
        DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;
        DescriptorSetLayout(DescriptorSetLayout&&) noexcept = delete;
        DescriptorSetLayout& operator=(DescriptorSetLayout&&) noexcept = delete;

        [[nodiscard]] vk::DescriptorSetLayout get() const { return m_descriptor_set_layout.get(); }
        [[nodiscard]] const std::vector<vk::DescriptorSetLayoutBinding>& get_bindings() const {
            return m_bindings;
        }

    private:
        DescriptorSetLayout(vk::UniqueDescriptorSetLayout layout,
            std::vector<vk::DescriptorSetLayoutBinding> bindings);
        vk::UniqueDescriptorSetLayout m_descriptor_set_layout;
        std::vector<vk::DescriptorSetLayoutBinding> m_bindings;
    };

    class COMET_API DescriptorSet {
    public:
        struct UniformBufferWrite {
            uint32_t binding;
            const Buffer& buffer;
            uint64_t range;
            uint64_t offset = 0;
            uint32_t array_element = 0;
        };

        struct ImageSamplerWrite {
            uint32_t binding;
            const ImageView& image;
            const Sampler& sampler;
            ImageLayout layout = ImageLayout::ShaderReadOnlyOptimal;
            uint32_t array_element = 0;
        };

        friend class DescriptorPool;
        DescriptorSet() = delete;

        // 立即消费写入描述；资源和池的生命周期仍由调用方保证。
        void update(Device& device, std::span<const UniformBufferWrite> buffers,
            std::span<const ImageSamplerWrite> images = {}) const;

        [[nodiscard]] vk::DescriptorSet get() const { return m_descriptor_set; }

    private:
        explicit DescriptorSet(const vk::DescriptorSet descriptor_set) noexcept
            : m_descriptor_set(descriptor_set) {}
        vk::DescriptorSet m_descriptor_set;
    };

    class COMET_API DescriptorPool {
    public:
        static Result<std::unique_ptr<DescriptorPool>, GraphicsError> create(Device& device,
            uint32_t max_sets, const DescriptorPoolSizes& pool_sizes,
            Flags<DescriptorPoolCreateFlag> flags = {});
        ~DescriptorPool() = default;

        DescriptorPool(const DescriptorPool&) = delete;
        DescriptorPool& operator=(const DescriptorPool&) = delete;
        DescriptorPool(DescriptorPool&&) noexcept = delete;
        DescriptorPool& operator=(DescriptorPool&&) noexcept = delete;

        Result<std::vector<DescriptorSet>, GraphicsError> allocate_descriptor_set(
            const DescriptorSetLayout& set_layout, uint32_t count) const;
        [[nodiscard]] vk::DescriptorPool get() const { return m_descriptor_pool.get(); }

    private:
        DescriptorPool(Device& device, vk::UniqueDescriptorPool pool);
        Device& m_device;
        vk::UniqueDescriptorPool m_descriptor_pool;
    };

    class COMET_API PushConstantRange {
    public:
        PushConstantRange(ShaderStage stage, uint32_t offset, uint32_t size);
        [[nodiscard]] vk::PushConstantRange get() const { return m_const_range; }

    private:
        vk::PushConstantRange m_const_range;
    };
}

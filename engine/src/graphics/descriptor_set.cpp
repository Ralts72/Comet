#include "descriptor_set.h"
#include "device.h"
#include "convert.h"

namespace Comet {
    void DescriptorSetLayoutBindings::add_binding(uint32_t binding, DescriptorType type, Flags<ShaderStage> stage_flags, uint32_t count) {
        m_bindings.emplace_back(binding, Graphics::description_type_to_vk(type), count,
            Graphics::shader_stage_to_vk(stage_flags), nullptr);
    }

    DescriptorSetLayout::DescriptorSetLayout(Device* device, const DescriptorSetLayoutBindings& bindings)
        : m_device(device) {
        vk::DescriptorSetLayoutCreateInfo create_info{};
        create_info.bindingCount = bindings.get_bindings().size();
        create_info.pBindings = bindings.get_bindings().data();
        m_descriptor_set_layout = m_device->get().createDescriptorSetLayout(create_info);
    }

    DescriptorSetLayout::~DescriptorSetLayout() {
        m_device->get().destroyDescriptorSetLayout(m_descriptor_set_layout);
    }

    DescriptorPool::DescriptorPool(Device* device, const uint32_t max_sets,
        const DescriptorPoolSizes& pool_sizes, Flags<DescriptorPoolCreateFlag> flags): m_device(device) {
        vk::DescriptorPoolCreateInfo create_info{};
        create_info.flags = Graphics::descriptor_pool_create_flags_to_vk(flags);
        create_info.maxSets = max_sets;
        create_info.poolSizeCount = pool_sizes.get_pool_sizes().size();
        create_info.pPoolSizes = pool_sizes.get_pool_sizes().data();
        m_descriptor_pool = m_device->get().createDescriptorPool(create_info);
    }

    void DescriptorPoolSizes::add_pool_size(DescriptorType type, uint32_t count) {
        m_sizes.emplace_back(Graphics::description_type_to_vk(type), count);
    }

    DescriptorPool::~DescriptorPool() {
        m_device->get().destroyDescriptorPool(m_descriptor_pool);
    }

    std::vector<DescriptorSet> DescriptorPool::allocate_descriptor_set(const DescriptorSetLayout& set_layout,
        const uint32_t count) const {
        std::vector<vk::DescriptorSetLayout> set_layouts(count);
        for(uint32_t i = 0; i < count; i++){
            set_layouts[i] = set_layout.get();
        }
        vk::DescriptorSetAllocateInfo allocate_info{};
        allocate_info.descriptorPool = m_descriptor_pool;
        allocate_info.descriptorSetCount = count;
        allocate_info.pSetLayouts = set_layouts.data();
        std::vector<DescriptorSet> descriptor_sets;
        descriptor_sets.reserve(count);
        const auto vk_descriptor_sets = m_device->get().allocateDescriptorSets(allocate_info);
        for(const auto vk_descriptor_set : vk_descriptor_sets) {
            descriptor_sets.emplace_back(DescriptorSet(vk_descriptor_set));
        }
        return descriptor_sets;
    }

    PushConstantRange::PushConstantRange(const ShaderStage stage, const uint32_t offset, const uint32_t size) {
        m_const_range.offset = offset;
        m_const_range.size = size;
        m_const_range.stageFlags = Graphics::shader_stage_to_vk(Flags<ShaderStage>(stage));
    }
}

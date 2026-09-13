#include "graphics/pipeline/descriptor_set.h"
#include "graphics/creation.h"
#include "graphics/device.h"
#include "graphics/convert.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace Comet {
    void DescriptorSet::update(Device& device, std::span<const UniformBufferWrite> buffers,
        std::span<const ImageSamplerWrite> images) const {
        if(buffers.empty() && images.empty())
            return;
        std::vector<vk::DescriptorBufferInfo> buffer_infos(buffers.size());
        std::vector<vk::DescriptorImageInfo> image_infos(images.size());
        std::vector<vk::WriteDescriptorSet> writes;
        writes.reserve(buffers.size() + images.size());
        for(size_t index = 0; index < buffers.size(); ++index) {
            const auto& buffer = buffers[index];
            buffer_infos[index] =
                vk::DescriptorBufferInfo(buffer.buffer.get(), buffer.offset, buffer.range);
            vk::WriteDescriptorSet write;
            write.dstSet = m_descriptor_set;
            write.dstBinding = buffer.binding;
            write.dstArrayElement = buffer.array_element;
            write.descriptorType = vk::DescriptorType::eUniformBuffer;
            write.descriptorCount = 1;
            write.pBufferInfo = &buffer_infos[index];
            writes.push_back(write);
        }
        for(size_t index = 0; index < images.size(); ++index) {
            const auto& image = images[index];
            image_infos[index] = vk::DescriptorImageInfo(
                image.sampler.get(), image.image.get(), Graphics::image_layout_to_vk(image.layout));
            vk::WriteDescriptorSet write;
            write.dstSet = m_descriptor_set;
            write.dstBinding = image.binding;
            write.dstArrayElement = image.array_element;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &image_infos[index];
            writes.push_back(write);
        }
        device.get().updateDescriptorSets(writes, {});
    }

    void DescriptorSetLayoutBindings::add_binding(uint32_t binding, const DescriptorType type,
        const Flags<ShaderStage> stage_flags, uint32_t count) {
        m_bindings.emplace_back(binding, Graphics::description_type_to_vk(type), count,
            Graphics::shader_stage_to_vk(stage_flags), nullptr);
    }

    DescriptorSetLayout::DescriptorSetLayout(
        vk::UniqueDescriptorSetLayout layout, std::vector<vk::DescriptorSetLayoutBinding> bindings)
        : m_descriptor_set_layout(std::move(layout)), m_bindings(std::move(bindings)) {}

    Result<std::shared_ptr<DescriptorSetLayout>, GraphicsError> DescriptorSetLayout::create(
        Device& device, const DescriptorSetLayoutBindings& bindings) {
        const auto& input = bindings.get_bindings();
        if(input.size() > std::numeric_limits<uint32_t>::max())
            return Result<std::shared_ptr<DescriptorSetLayout>, GraphicsError>::failure(
                {"Too many descriptor layout bindings"});
        std::unordered_set<uint32_t> indices;
        for(const auto& binding : input) {
            if(!indices.insert(binding.binding).second)
                return Result<std::shared_ptr<DescriptorSetLayout>, GraphicsError>::failure(
                    {"Duplicate descriptor layout binding " + std::to_string(binding.binding)});
            if(binding.descriptorCount > 0 && !binding.stageFlags)
                return Result<std::shared_ptr<DescriptorSetLayout>, GraphicsError>::failure(
                    {"Descriptor layout binding requires shader stages"});
        }
        vk::DescriptorSetLayoutCreateInfo info{};
        info.bindingCount = static_cast<uint32_t>(input.size());
        info.pBindings = input.data();
        auto layout = Graphics::create_handle<vk::DescriptorSetLayout>(device.get(),
            "Create descriptor set layout", [&](vk::DescriptorSetLayout* output) noexcept {
                return device.get().createDescriptorSetLayout(&info, nullptr, output);
            });
        if(!layout)
            return Result<std::shared_ptr<DescriptorSetLayout>, GraphicsError>::failure(
                layout.error());
        return Result<std::shared_ptr<DescriptorSetLayout>, GraphicsError>::success(
            std::shared_ptr<DescriptorSetLayout>(
                new DescriptorSetLayout(std::move(layout).value(), input)));
    }

    DescriptorPool::DescriptorPool(Device& device, vk::UniqueDescriptorPool pool)
        : m_device(device), m_descriptor_pool(std::move(pool)) {}

    Result<std::unique_ptr<DescriptorPool>, GraphicsError> DescriptorPool::create(Device& device,
        const uint32_t max_sets, const DescriptorPoolSizes& pool_sizes,
        const Flags<DescriptorPoolCreateFlag> flags) {
        const auto& sizes = pool_sizes.get_pool_sizes();
        if(max_sets == 0 || sizes.size() > std::numeric_limits<uint32_t>::max())
            return Result<std::unique_ptr<DescriptorPool>, GraphicsError>::failure(
                {"Descriptor pool requires a positive set limit and a bounded size list"});
        if(std::ranges::any_of(sizes, [](const auto& size) { return size.descriptorCount == 0; }))
            return Result<std::unique_ptr<DescriptorPool>, GraphicsError>::failure(
                {"Descriptor pool entries require a positive descriptor count"});
        vk::DescriptorPoolCreateInfo info{};
        info.flags = Graphics::descriptor_pool_create_flags_to_vk(flags);
        info.maxSets = max_sets;
        info.poolSizeCount = static_cast<uint32_t>(sizes.size());
        info.pPoolSizes = sizes.data();
        auto pool = Graphics::create_handle<vk::DescriptorPool>(
            device.get(), "Create descriptor pool", [&](vk::DescriptorPool* output) noexcept {
                return device.get().createDescriptorPool(&info, nullptr, output);
            });
        if(!pool)
            return Result<std::unique_ptr<DescriptorPool>, GraphicsError>::failure(pool.error());
        return Result<std::unique_ptr<DescriptorPool>, GraphicsError>::success(
            std::unique_ptr<DescriptorPool>(new DescriptorPool(device, std::move(pool).value())));
    }

    void DescriptorPoolSizes::add_pool_size(const DescriptorType type, uint32_t count) {
        m_sizes.emplace_back(Graphics::description_type_to_vk(type), count);
    }

    Result<std::vector<DescriptorSet>, GraphicsError> DescriptorPool::allocate_descriptor_set(
        const DescriptorSetLayout& set_layout, const uint32_t count) const {
        if(count == 0)
            return Result<std::vector<DescriptorSet>, GraphicsError>::failure(
                {"Descriptor set allocation requires a positive count"});
        const std::vector<vk::DescriptorSetLayout> layouts(count, set_layout.get());
        std::vector<vk::DescriptorSet> handles(count);
        std::vector<DescriptorSet> sets;
        sets.reserve(count);
        const vk::DescriptorSetAllocateInfo info(get(), count, layouts.data());
        const auto status = m_device.get().allocateDescriptorSets(&info, handles.data());
        if(status != vk::Result::eSuccess)
            return Result<std::vector<DescriptorSet>, GraphicsError>::failure(
                {"Allocate descriptor sets: " + vk::to_string(status), status});
        for(const auto handle : handles)
            sets.emplace_back(DescriptorSet(handle));
        return Result<std::vector<DescriptorSet>, GraphicsError>::success(std::move(sets));
    }

    PushConstantRange::PushConstantRange(
        const ShaderStage stage, const uint32_t offset, const uint32_t size) {
        m_const_range.offset = offset;
        m_const_range.size = size;
        m_const_range.stageFlags = Graphics::shader_stage_to_vk(Flags<ShaderStage>(stage));
    }
}

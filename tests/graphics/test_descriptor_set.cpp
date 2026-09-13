#include "graphics/pipeline/descriptor_set.h"
#include "graphics/device.h"
#include "graphics/resource/buffer.h"
#include "support/engine_fixture.h"

#include <array>

namespace Comet::Tests {
    using DescriptorSetTest = EngineTest;

    TEST_F(DescriptorSetTest, RejectsInvalidBindingsButAllowsEmptyAndReservedLayouts) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        DescriptorSetLayoutBindings duplicate;
        duplicate.add_binding(
            0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        duplicate.add_binding(
            0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Fragment));
        auto rejected = DescriptorSetLayout::create(device, duplicate);
        ASSERT_FALSE(rejected);
        EXPECT_FALSE(rejected.error().result.has_value());
        EXPECT_NE(rejected.error().message.find("Duplicate"), std::string::npos);

        DescriptorSetLayoutBindings missing_stage;
        missing_stage.add_binding(0, DescriptorType::UniformBuffer, {});
        rejected = DescriptorSetLayout::create(device, missing_stage);
        ASSERT_FALSE(rejected);
        EXPECT_FALSE(rejected.error().result.has_value());
        EXPECT_NE(rejected.error().message.find("stages"), std::string::npos);

        auto empty = DescriptorSetLayout::create(device, {});
        ASSERT_TRUE(empty) << empty.error();
        EXPECT_TRUE(empty.value()->get());
        DescriptorSetLayoutBindings reserved;
        reserved.add_binding(3, DescriptorType::UniformBuffer, {}, 0);
        auto layout = DescriptorSetLayout::create(device, reserved);
        ASSERT_TRUE(layout) << layout.error();
        ASSERT_EQ(layout.value()->get_bindings().size(), 1u);
        EXPECT_EQ(layout.value()->get_bindings().front().descriptorCount, 0u);
    }

    TEST_F(DescriptorSetTest, RejectsInvalidPoolSizesAndCanCreateAfterFailure) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto rejected = DescriptorPool::create(device, 0, {});
        ASSERT_FALSE(rejected);
        EXPECT_FALSE(rejected.error().result.has_value());
        DescriptorPoolSizes invalid;
        invalid.add_pool_size(DescriptorType::UniformBuffer, 0);
        rejected = DescriptorPool::create(device, 1, invalid);
        ASSERT_FALSE(rejected);
        EXPECT_FALSE(rejected.error().result.has_value());

        auto pool = DescriptorPool::create(device, 1, {});
        ASSERT_TRUE(pool) << pool.error();
        auto layout = DescriptorSetLayout::create(device, {});
        ASSERT_TRUE(layout) << layout.error();
        auto sets = pool.value()->allocate_descriptor_set(*layout.value(), 1);
        ASSERT_TRUE(sets) << sets.error();
        ASSERT_EQ(sets.value().size(), 1u);
        EXPECT_TRUE(sets.value().front().get());
    }

    TEST_F(DescriptorSetTest, InvalidAllocationDoesNotInvalidatePoolOrExistingSets) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(
            0, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex));
        auto layout = DescriptorSetLayout::create(device, bindings);
        ASSERT_TRUE(layout) << layout.error();
        DescriptorPoolSizes sizes;
        sizes.add_pool_size(DescriptorType::UniformBuffer, 3);
        auto pool = DescriptorPool::create(device, 3, sizes);
        ASSERT_TRUE(pool) << pool.error();
        auto first = pool.value()->allocate_descriptor_set(*layout.value(), 1);
        ASSERT_TRUE(first) << first.error();
        const auto first_handle = first.value().front().get();
        auto rejected = pool.value()->allocate_descriptor_set(*layout.value(), 0);
        ASSERT_FALSE(rejected);
        EXPECT_FALSE(rejected.error().result.has_value());
        auto second = pool.value()->allocate_descriptor_set(*layout.value(), 2);
        ASSERT_TRUE(second) << second.error();
        ASSERT_EQ(second.value().size(), 2u);
        EXPECT_EQ(first.value().front().get(), first_handle);
        EXPECT_TRUE(second.value()[0].get());
        EXPECT_TRUE(second.value()[1].get());
        EXPECT_NE(second.value()[0].get(), second.value()[1].get());
        EXPECT_NE(second.value()[0].get(), first_handle);
        EXPECT_NE(second.value()[1].get(), first_handle);
        auto buffer = Buffer::try_create_cpu_buffer(
            device, Flags<BufferUsage>(BufferUsage::Uniform), 16, false);
        ASSERT_TRUE(buffer);
        const DescriptorSet::UniformBufferWrite write{0, *buffer.value(), 16};
        first.value().front().update(device, std::span(&write, 1));
    }

    TEST_F(DescriptorSetTest, UpdatesMultipleBindingsAndArrayElements) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        DescriptorSetLayoutBindings bindings;
        bindings.add_binding(
            2, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Vertex), 2);
        bindings.add_binding(
            5, DescriptorType::UniformBuffer, Flags<ShaderStage>(ShaderStage::Fragment));
        auto layout = DescriptorSetLayout::create(device, bindings);
        ASSERT_TRUE(layout) << layout.error();
        DescriptorPoolSizes sizes;
        sizes.add_pool_size(DescriptorType::UniformBuffer, 3);
        auto pool = DescriptorPool::create(device, 1, sizes);
        ASSERT_TRUE(pool) << pool.error();
        auto sets = pool.value()->allocate_descriptor_set(*layout.value(), 1);
        ASSERT_TRUE(sets) << sets.error();
        auto buffer = Buffer::try_create_cpu_buffer(
            device, Flags<BufferUsage>(BufferUsage::Uniform), 32, false);
        ASSERT_TRUE(buffer);
        const std::array writes{DescriptorSet::UniformBufferWrite{2, *buffer.value(), 16, 0, 0},
            DescriptorSet::UniformBufferWrite{2, *buffer.value(), 32, 0, 1},
            DescriptorSet::UniformBufferWrite{5, *buffer.value(), 16}};
        sets.value().front().update(device, writes);
        sets.value().front().update(device, {});
    }
}

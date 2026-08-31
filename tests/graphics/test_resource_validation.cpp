#include <gtest/gtest.h>

#include <type_traits>

#include "graphics/resource/buffer.h"
#include "graphics/command/command_buffer.h"
#include "graphics/command/command_context.h"
#include "graphics/device.h"
#include "graphics/resource/image.h"
#include "graphics/resource/sampler.h"
#include "graphics/synchronization/resource_state.h"

namespace Comet::Tests {
namespace {
    template<typename T>
    concept SupportsBufferReferenceCopy = requires(
        T& context, const Buffer& source, const Buffer& destination) {
        context.copy_buffer(source, destination, 1);
    };

    template<typename T>
    concept SupportsBufferPointerCopy = requires(
        T& context, const Buffer* source, const Buffer* destination) {
        context.copy_buffer(source, destination, 1);
    };

    template<typename T>
    concept SupportsImageReferenceTransition = requires(
        T& context,
        const Image& image,
        const ImageState& before,
        const ImageState& after) {
        context.transition_image_state(image, before, after);
    };

    template<typename T>
    concept SupportsImagePointerTransition = requires(
        T& context,
        const Image* image,
        const ImageState& before,
        const ImageState& after) {
        context.transition_image_state(image, before, after);
    };

    template<typename T>
    concept SupportsBufferReferenceTransition = requires(
        T& context,
        const Buffer& buffer,
        const ResourceState& before,
        const ResourceState& after) {
        context.transition_buffer_state(buffer, before, after);
    };

    template<typename T>
    concept SupportsBufferPointerTransition = requires(
        T& context,
        const Buffer* buffer,
        const ResourceState& before,
        const ResourceState& after) {
        context.transition_buffer_state(buffer, before, after);
    };

    template<typename T>
    concept SupportsSingleVertexBufferBinding = requires(
        const T& command_buffer, const Buffer& buffer) {
        command_buffer.bind_vertex_buffer(VertexBufferBinding{buffer, 0});
    };

    template<typename T>
    concept SupportsMultipleVertexBufferBindings = requires(
        const T& command_buffer,
        const std::span<const VertexBufferBinding> bindings) {
        command_buffer.bind_vertex_buffers(bindings);
    };

    template<typename T>
    concept SupportsVertexBufferPointerBinding = requires(
        const T& command_buffer, const Buffer* buffer) {
        command_buffer.bind_vertex_buffer(buffer, 0);
    };

    template<typename T>
    concept SupportsSeparatedVertexBufferBinding = requires(
        const T& command_buffer, const Buffer& buffer) {
        command_buffer.bind_vertex_buffer(buffer, 0);
    };
}

TEST(ResourceValidationTest, RequiredDependenciesRejectNullAtCompileTime) {
    using CpuBufferFactory = decltype(&Buffer::create_cpu_buffer);
    using ImageFactory = decltype(&Image::create);
    using ImageWrapper = decltype(&Image::wrap);

    EXPECT_FALSE((std::is_constructible_v<Device, std::nullptr_t>));
    EXPECT_FALSE((std::is_constructible_v<Sampler, std::nullptr_t, SamplerDesc>));
    EXPECT_FALSE((std::is_invocable_v<CpuBufferFactory, std::nullptr_t,
        Flags<BufferUsage>, size_t, const void*, std::string_view>));
    EXPECT_FALSE((std::is_invocable_v<ImageFactory, std::nullptr_t,
        const ImageInfo&, SampleCount, std::string_view>));
    EXPECT_FALSE((std::is_invocable_v<ImageWrapper, std::nullptr_t,
        vk::Image, const ImageInfo&>));

    EXPECT_TRUE((std::is_invocable_v<CpuBufferFactory, Device&,
        Flags<BufferUsage>, size_t, const void*, std::string_view>));
    EXPECT_TRUE((std::is_invocable_v<ImageFactory, Device&,
        const ImageInfo&, SampleCount, std::string_view>));
    EXPECT_TRUE((std::is_invocable_v<ImageWrapper, Device&,
        vk::Image, const ImageInfo&>));
}

TEST(ResourceValidationTest, CommandRecordingUsesNonNullResourceReferences) {
    EXPECT_TRUE(SupportsBufferReferenceCopy<CommandContext>);
    EXPECT_FALSE(SupportsBufferPointerCopy<CommandContext>);
    EXPECT_TRUE(SupportsImageReferenceTransition<CommandContext>);
    EXPECT_FALSE(SupportsImagePointerTransition<CommandContext>);
    EXPECT_TRUE(SupportsBufferReferenceTransition<CommandContext>);
    EXPECT_FALSE(SupportsBufferPointerTransition<CommandContext>);
    EXPECT_TRUE(SupportsSingleVertexBufferBinding<CommandBuffer>);
    EXPECT_TRUE(SupportsMultipleVertexBufferBindings<CommandBuffer>);
    EXPECT_FALSE(SupportsVertexBufferPointerBinding<CommandBuffer>);
    EXPECT_FALSE(SupportsSeparatedVertexBufferBinding<CommandBuffer>);
}

} // namespace Comet::Tests

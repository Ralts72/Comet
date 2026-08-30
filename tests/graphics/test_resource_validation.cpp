#include <gtest/gtest.h>

#include <type_traits>

#include "graphics/resource/buffer.h"
#include "graphics/command/command_buffer.h"
#include "graphics/command/command_context.h"
#include "graphics/device.h"
#include "graphics/frame_buffer.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "graphics/synchronization/resource_state.h"
#include "render/render_target.h"

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

    template<typename T>
    concept SupportsRenderTargetResize = requires(T& target) {
        target.resize(1280, 720);
    };

    template<typename T>
    concept SupportsRenderTargetFrameCountMutation = requires(T& target) {
        target.set_frame_count(2);
    };

    template<typename T>
    concept SupportsRenderTargetRecreation = requires(T& target) {
        target.recreate();
    };
}

TEST(ResourceValidationTest, RequiredDependenciesRejectNullAtCompileTime) {
    using CpuBufferFactory = decltype(&Buffer::create_cpu_buffer);
    using ImageFactory = decltype(&Image::create);
    using RecoverableImageFactory = decltype(&Image::try_create);
    using ImageWrapper = decltype(&Image::wrap);
    using ImageViewFactory = decltype(&ImageView::create);
    using RecoverableImageViewFactory = decltype(&ImageView::try_create);
    using FrameBufferFactory = decltype(&FrameBuffer::create);
    using RecoverableFrameBufferFactory = decltype(&FrameBuffer::try_create);
    using RecoverableMultiTargetFactory =
        decltype(&RenderTarget::try_create_multi_target);

    EXPECT_FALSE((std::is_constructible_v<Device, std::nullptr_t>));
    EXPECT_FALSE((std::is_constructible_v<Sampler, std::nullptr_t, SamplerDesc>));
    EXPECT_FALSE((std::is_invocable_v<CpuBufferFactory, std::nullptr_t,
        Flags<BufferUsage>, size_t, const void*, std::string_view>));
    EXPECT_FALSE((std::is_invocable_v<ImageFactory, std::nullptr_t,
        const ImageInfo&, SampleCount, std::string_view>));
    EXPECT_FALSE((std::is_invocable_v<RecoverableImageFactory, std::nullptr_t,
        const ImageInfo&, bool, SampleCount, std::string_view>));
    EXPECT_FALSE((std::is_invocable_v<ImageWrapper, std::nullptr_t,
        vk::Image, const ImageInfo&>));

    EXPECT_TRUE((std::is_invocable_v<CpuBufferFactory, Device&,
        Flags<BufferUsage>, size_t, const void*, std::string_view>));
    EXPECT_TRUE((std::is_invocable_v<ImageFactory, Device&,
        const ImageInfo&, SampleCount, std::string_view>));
    EXPECT_TRUE((std::is_invocable_r_v<
        GpuResourceResult<std::shared_ptr<Image>>,
        RecoverableImageFactory,
        Device&,
        const ImageInfo&,
        bool,
        SampleCount,
        std::string_view>));
    EXPECT_TRUE((std::is_invocable_v<ImageWrapper, Device&,
        vk::Image, const ImageInfo&>));
    EXPECT_TRUE((std::is_invocable_r_v<
        std::shared_ptr<ImageView>,
        ImageViewFactory,
        Device&,
        std::shared_ptr<Image>,
        Flags<ImageAspect>>));
    EXPECT_TRUE((std::is_invocable_r_v<
        GpuResourceResult<std::shared_ptr<ImageView>>,
        RecoverableImageViewFactory,
        Device&,
        std::shared_ptr<Image>,
        Flags<ImageAspect>>));
    EXPECT_TRUE((std::is_invocable_r_v<
        std::shared_ptr<FrameBuffer>,
        FrameBufferFactory,
        Device&,
        RenderPass&,
        const std::vector<std::shared_ptr<ImageView>>&,
        uint32_t,
        uint32_t>));
    EXPECT_TRUE((std::is_invocable_r_v<
        GpuResourceResult<std::shared_ptr<FrameBuffer>>,
        RecoverableFrameBufferFactory,
        Device&,
        RenderPass&,
        const std::vector<std::shared_ptr<ImageView>>&,
        uint32_t,
        uint32_t>));
    EXPECT_TRUE((std::is_invocable_r_v<
        GpuResourceResult<std::unique_ptr<RenderTarget>>,
        RecoverableMultiTargetFactory,
        Device&,
        RenderPass&,
        Math::Vec2u,
        uint32_t>));
    EXPECT_FALSE((std::is_constructible_v<OwnedImage,
        Device&, const ImageInfo&, SampleCount, std::string_view>));
    EXPECT_FALSE((std::is_constructible_v<
        ImageView,
        Device&,
        std::shared_ptr<Image>,
        Flags<ImageAspect>>));
    EXPECT_FALSE((std::is_constructible_v<
        FrameBuffer,
        Device&,
        RenderPass&,
        const std::vector<std::shared_ptr<ImageView>>&,
        uint32_t,
        uint32_t>));
    EXPECT_FALSE((std::is_constructible_v<MultiTarget,
        Device&, RenderPass&, Math::Vec2u, uint32_t>));
    EXPECT_FALSE(SupportsRenderTargetResize<RenderTarget>);
    EXPECT_FALSE(SupportsRenderTargetFrameCountMutation<RenderTarget>);
    EXPECT_FALSE(SupportsRenderTargetRecreation<RenderTarget>);
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

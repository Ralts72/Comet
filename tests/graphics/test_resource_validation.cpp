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
        concept SupportsBufferReferenceCopy = requires(T& context, const Buffer& source,
            const Buffer& destination) { context.copy_buffer(source, destination, 1); };

        template<typename T>
        concept SupportsBufferPointerCopy = requires(T& context, const Buffer* source,
            const Buffer* destination) { context.copy_buffer(source, destination, 1); };

        template<typename T>
        concept SupportsImageReferenceTransition = requires(T& context,
            const Image& image, const ImageState& before, const ImageState& after) {
            context.transition_image_state(image, before, after);
        };

        template<typename T>
        concept SupportsImagePointerTransition = requires(T& context, const Image* image,
            const ImageState& before, const ImageState& after) {
            context.transition_image_state(image, before, after);
        };

        template<typename T>
        concept SupportsBufferReferenceTransition =
            requires(T& context, const Buffer& buffer, const ResourceState& before,
                const ResourceState& after) {
                context.transition_buffer_state(buffer, before, after);
            };

        template<typename T>
        concept SupportsBufferPointerTransition =
            requires(T& context, const Buffer* buffer, const ResourceState& before,
                const ResourceState& after) {
                context.transition_buffer_state(buffer, before, after);
            };

        template<typename T>
        concept SupportsSingleVertexBufferBinding =
            requires(const T& command_buffer, const Buffer& buffer) {
                command_buffer.bind_vertex_buffer(VertexBufferBinding{buffer, 0});
            };

        template<typename T>
        concept SupportsMultipleVertexBufferBindings = requires(const T& command_buffer,
            const std::span<const VertexBufferBinding> bindings) {
            command_buffer.bind_vertex_buffers(bindings);
        };

        template<typename T>
        concept SupportsVertexBufferPointerBinding = requires(const T& command_buffer,
            const Buffer* buffer) { command_buffer.bind_vertex_buffer(buffer, 0); };

        template<typename T>
        concept SupportsSeparatedVertexBufferBinding = requires(const T& command_buffer,
            const Buffer& buffer) { command_buffer.bind_vertex_buffer(buffer, 0); };
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

        static_assert(!std::is_constructible_v<Device, std::nullptr_t>);
        static_assert(!std::is_constructible_v<Sampler, std::nullptr_t, SamplerDesc>);
        static_assert(!std::is_invocable_v<CpuBufferFactory, std::nullptr_t,
            Flags<BufferUsage>, size_t, const void*, std::string_view>);
        static_assert(!std::is_invocable_v<ImageFactory, std::nullptr_t, const ImageInfo&,
            SampleCount, std::string_view>);
        static_assert(!std::is_invocable_v<RecoverableImageFactory, std::nullptr_t,
            const ImageInfo&, bool, SampleCount, std::string_view>);
        static_assert(!std::is_invocable_v<ImageWrapper, std::nullptr_t, vk::Image,
            const ImageInfo&>);

        static_assert(std::is_invocable_v<CpuBufferFactory, Device&, Flags<BufferUsage>,
            size_t, const void*, std::string_view>);
        static_assert(std::is_invocable_v<ImageFactory, Device&, const ImageInfo&,
            SampleCount, std::string_view>);
        static_assert(std::is_invocable_r_v<GpuResourceResult<std::shared_ptr<Image>>,
            RecoverableImageFactory, Device&, const ImageInfo&, bool, SampleCount,
            std::string_view>);
        static_assert(
            std::is_invocable_v<ImageWrapper, Device&, vk::Image, const ImageInfo&>);
        static_assert(std::is_invocable_r_v<std::shared_ptr<ImageView>, ImageViewFactory,
            Device&, std::shared_ptr<Image>, Flags<ImageAspect>>);
        static_assert(std::is_invocable_r_v<GpuResourceResult<std::shared_ptr<ImageView>>,
            RecoverableImageViewFactory, Device&, std::shared_ptr<Image>,
            Flags<ImageAspect>>);
        static_assert(std::is_invocable_r_v<std::shared_ptr<FrameBuffer>,
            FrameBufferFactory, Device&, RenderPass&,
            const std::vector<std::shared_ptr<ImageView>>&, uint32_t, uint32_t>);
        static_assert(
            std::is_invocable_r_v<GpuResourceResult<std::shared_ptr<FrameBuffer>>,
                RecoverableFrameBufferFactory, Device&, RenderPass&,
                const std::vector<std::shared_ptr<ImageView>>&, uint32_t, uint32_t>);
        static_assert(
            std::is_invocable_r_v<GpuResourceResult<std::unique_ptr<RenderTarget>>,
                RecoverableMultiTargetFactory, Device&, RenderPass&, Math::Vec2u,
                uint32_t>);
        static_assert(!std::is_constructible_v<OwnedImage, Device&, const ImageInfo&,
            SampleCount, std::string_view>);
        static_assert(!std::is_constructible_v<ImageView, Device&, std::shared_ptr<Image>,
            Flags<ImageAspect>>);
        static_assert(!std::is_constructible_v<FrameBuffer, Device&, RenderPass&,
            const std::vector<std::shared_ptr<ImageView>>&, uint32_t, uint32_t>);
        static_assert(!std::is_constructible_v<MultiTarget, Device&, RenderPass&,
            Math::Vec2u, uint32_t>);

        static_assert(SupportsBufferReferenceCopy<CommandContext>);
        static_assert(!SupportsBufferPointerCopy<CommandContext>);
        static_assert(SupportsImageReferenceTransition<CommandContext>);
        static_assert(!SupportsImagePointerTransition<CommandContext>);
        static_assert(SupportsBufferReferenceTransition<CommandContext>);
        static_assert(!SupportsBufferPointerTransition<CommandContext>);
        static_assert(SupportsSingleVertexBufferBinding<CommandBuffer>);
        static_assert(SupportsMultipleVertexBufferBindings<CommandBuffer>);
        static_assert(!SupportsVertexBufferPointerBinding<CommandBuffer>);
        static_assert(!SupportsSeparatedVertexBufferBinding<CommandBuffer>);
    }

} // namespace Comet::Tests

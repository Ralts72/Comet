#pragma once
#include "graphics/vk_common.h"
#include "core/math_utils.h"
#include "common/export.h"

namespace Comet {
    class Image;
    class ImageView;
    class FrameBuffer;
    class RenderPass;
    class Swapchain;
    class Device;
    class CommandBuffer;

    struct RenderResource {
        std::vector<std::shared_ptr<Image>> color_images;
        std::vector<std::shared_ptr<ImageView>> color_views;
        std::shared_ptr<Image> depth_image;
        std::shared_ptr<ImageView> depth_view;
        std::shared_ptr<FrameBuffer> frame_buffer;
    };

    class COMET_API RenderTarget {
    public:
        static std::unique_ptr<RenderTarget> create_swapchain_target(Device* device, RenderPass* render_pass, Swapchain* swapchain);

        static std::unique_ptr<RenderTarget> create_offscreen_target(Device* device, RenderPass* render_pass, Math::Vec2u size);

        static std::unique_ptr<RenderTarget> create_multi_target(Device* device, RenderPass* render_pass, Math::Vec2u size, uint32_t frame_count);

        virtual ~RenderTarget() = default;

        virtual void recreate() = 0;

        void resize(uint32_t width, uint32_t height);

        void set_frame_count(uint32_t frame_count);

        void set_clear_value(const ClearValue& clear_value, int index = -1);

        virtual void begin_render_target(CommandBuffer& command_buffer);

        virtual void end_render_target(CommandBuffer& command_buffer);

        [[nodiscard]] Math::Vec2u get_size() const { return m_extent; }

        [[nodiscard]] virtual std::shared_ptr<FrameBuffer> get_framebuffer(uint32_t index) const = 0;

        [[nodiscard]] uint32_t get_frame_count() const { return m_frame_count; }
        [[nodiscard]] bool is_dirty() const { return m_needs_recreate; }

    protected:
        RenderTarget(Device* device, RenderPass* render_pass,
                     const Math::Vec2u size, const uint32_t frame_count) : m_device(device), m_render_pass(render_pass), m_extent(size), m_frame_count(frame_count),
                                                                           m_clear_values({}), m_needs_recreate(false), m_current_image_index(0) {}

        void clear_render_resources(std::vector<RenderResource>& resources);

        Device* m_device;
        RenderPass* m_render_pass;
        Math::Vec2u m_extent;
        uint32_t m_frame_count;
        std::vector<ClearValue> m_clear_values;
        bool m_needs_recreate;
        uint32_t m_current_image_index;
    };

    class COMET_API SwapchainTarget final: public RenderTarget {
    public:
        SwapchainTarget(Device* device, RenderPass* render_pass, Swapchain* swapchain);

        ~SwapchainTarget() override;

        void recreate() override;

        void begin_render_target(CommandBuffer& command_buffer) override;

        [[nodiscard]] std::shared_ptr<FrameBuffer> get_framebuffer(const uint32_t index) const override { return m_render_resources.at(index).frame_buffer; }

    private:
        Swapchain* m_swapchain;
        std::vector<RenderResource> m_render_resources;
    };

    class COMET_API OffscreenTarget final: public RenderTarget {
    public:
        OffscreenTarget(Device* device, RenderPass* render_pass, Math::Vec2u size);

        ~OffscreenTarget() override;

        void recreate() override;

        [[nodiscard]] std::shared_ptr<FrameBuffer> get_framebuffer(const uint32_t index) const override { return m_frame_buffer; }

    private:
        std::shared_ptr<FrameBuffer> m_frame_buffer;
        std::shared_ptr<Image> m_color_image;
        std::shared_ptr<ImageView> m_color_view;
        std::shared_ptr<Image> m_depth_image;
        std::shared_ptr<ImageView> m_depth_view;
    };

    class COMET_API MultiTarget final: public RenderTarget {
    public:
        MultiTarget(Device* device, RenderPass* render_pass, Math::Vec2u size, uint32_t frame_count);

        ~MultiTarget() override;

        void recreate() override;

        [[nodiscard]] std::shared_ptr<FrameBuffer> get_framebuffer(const uint32_t index) const override { return m_render_resources.at(index).frame_buffer; }

    private:
        std::vector<RenderResource> m_render_resources;
    };
}

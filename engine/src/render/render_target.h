#pragma once
#include "graphics/vk_common.h"
#include "core/math_utils.h"

namespace Comet {
    class Image;
    class ImageView;
    class FrameBuffer;
    class RenderPass;
    class Swapchain;
    class Device;
    class CommandBuffer;

    enum class RenderTargetType { Swapchain, Offscreen, MultiAttachment };

    struct RenderResource {
        std::vector<std::shared_ptr<Image>> color_images;
        std::vector<std::shared_ptr<ImageView>> color_views;
        std::shared_ptr<Image> depth_image;
        std::shared_ptr<ImageView> depth_view;
        std::shared_ptr<FrameBuffer> frame_buffer;
    };

    class RenderTarget {
    public:
        static std::unique_ptr<RenderTarget> create_swapchain_target(Device* device, RenderPass* render_pass, Swapchain* swapchain);

        static std::unique_ptr<RenderTarget> create_offscreen_target(Device* device, RenderPass* render_pass, Math::Vec2i size);

        static std::unique_ptr<RenderTarget> create_multi_target(Device* device, RenderPass* render_pass, Math::Vec2i size, uint32_t frame_count);

        virtual ~RenderTarget() = default;

        virtual void recreate() = 0;

        void resize(uint32_t width, uint32_t height);

        void set_frame_count(uint32_t frame_count);

        void set_color_clear_value(Math::Vec4 color);

        void set_color_clear_value(uint32_t index, Math::Vec4 color);

        void set_depth_stencil_clear_value(Math::Vec2 depth_stencil);

        void set_depth_stencil_clear_value(uint32_t index, Math::Vec2 depth_stencil);

        virtual void begin_render_target(CommandBuffer& command_buffer);

        virtual void end_render_target(CommandBuffer& command_buffer);

        [[nodiscard]] Math::Vec2i get_size() const { return {m_extent.width, m_extent.height}; }

        [[nodiscard]] virtual std::shared_ptr<FrameBuffer> get_framebuffer(uint32_t index) const = 0;

        [[nodiscard]] uint32_t get_frame_count() const { return m_frame_count; }
        [[nodiscard]] RenderTargetType get_type() const { return m_type; }
        [[nodiscard]] bool is_dirty() const { return m_needs_recreate; }

    protected:
        RenderTarget(Device* device, RenderPass* render_pass, const RenderTargetType type, Math::Vec2i size,
                     const uint32_t frame_count) : m_device(device), m_render_pass(render_pass), m_type(type),
                                                   m_frame_count(frame_count), m_clear_values({}), m_needs_recreate(false), m_current_image_index(0) {
            m_extent.width = size.x;
            m_extent.height = size.y;
        }

        void clear_render_resources(std::vector<RenderResource>& resources);

        Device* m_device;
        RenderPass* m_render_pass;
        RenderTargetType m_type;
        vk::Extent2D m_extent;
        uint32_t m_frame_count;
        std::vector<vk::ClearValue> m_clear_values;
        bool m_needs_recreate;
        uint32_t m_current_image_index;
    };

    class SwapchainTarget final: public RenderTarget {
    public:
        SwapchainTarget(Device* device, RenderPass* render_pass, Swapchain* swapchain);

        ~SwapchainTarget() override;

        void recreate() override;

        void begin_render_target(CommandBuffer& command_buffer) override;

        [[nodiscard]] std::shared_ptr<FrameBuffer> get_framebuffer(const uint32_t index = 0) const override { return m_render_resources.at(index).frame_buffer; }

    private:
        Swapchain* m_swapchain;
        std::vector<RenderResource> m_render_resources;
    };

    class OffscreenTarget final: public RenderTarget {
    public:
        OffscreenTarget(Device* device, RenderPass* render_pass, Math::Vec2i size);

        ~OffscreenTarget() override;

        void recreate() override;

        [[nodiscard]] std::shared_ptr<FrameBuffer> get_framebuffer(const uint32_t index = 0) const override { return m_frame_buffer; }

    private:
        std::shared_ptr<FrameBuffer> m_frame_buffer;
        std::shared_ptr<Image> m_color_image;
        std::shared_ptr<ImageView> m_color_view;
        std::shared_ptr<Image> m_depth_image;
        std::shared_ptr<ImageView> m_depth_view;
    };

    class MultiTarget final: public RenderTarget {
    public:
        MultiTarget(Device* device, RenderPass* render_pass, Math::Vec2i size, uint32_t frame_count);

        ~MultiTarget() override;

        void recreate() override;

        [[nodiscard]] std::shared_ptr<FrameBuffer> get_framebuffer(const uint32_t index = 0) const override { return m_render_resources.at(index).frame_buffer; }

    private:
        std::vector<RenderResource> m_render_resources;
    };
}

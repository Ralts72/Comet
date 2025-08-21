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

    enum class RenderTargetType {
        Swapchain, Offscreen, MultiAttachment
    };

    struct RenderTargetInfo {
        Math::Vec2i extent;
        uint32_t frame_count;
    };

    struct RenderResource {
        std::vector<std::shared_ptr<Image>> color_images;
        std::vector<std::shared_ptr<ImageView>> color_views;
        std::shared_ptr<Image> depth_image;
        std::shared_ptr<ImageView> depth_view;
        std::shared_ptr<FrameBuffer> frame_buffer;
    };

    class RenderTarget {
    public:
        static std::unique_ptr<RenderTarget> create_swapchain_target(Device* device, RenderPass* render_pass, Swapchain* swapchain, const RenderTargetInfo info = {});
        static std::unique_ptr<RenderTarget> create_offscreen_target(Device* device, RenderPass* render_pass, const RenderTargetInfo info = {});
        static std::unique_ptr<RenderTarget> create_multi_target(Device* device, RenderPass* render_pass, const RenderTargetInfo info = {});

        virtual ~RenderTarget() = default;
        virtual void recreate() = 0;
        void resize(uint32_t width, uint32_t height);
        void set_frame_count(uint32_t frame_count);
        void set_render_target_info(const RenderTargetInfo& info) {m_info = info;}
        void set_color_clear_value(Math::Vec4 color);
        void set_color_clear_value(uint32_t index, Math::Vec4 color);
        void set_depth_stencil_clear_value(Math::Vec2 depth_stencil);
        void set_depth_stencil_clear_value(uint32_t index, Math::Vec2 depth_stencil);

        virtual void begin_render_target(CommandBuffer& command_buffer);
        virtual void end_render_target(CommandBuffer& command_buffer);

        [[nodiscard]] const RenderTargetInfo& get_render_target_info() const { return m_info; }
        [[nodiscard]] virtual std::shared_ptr<FrameBuffer> get_framebuffer(uint32_t index = 0) const = 0;
        [[nodiscard]] uint32_t get_frame_count() const { return m_info.frame_count;}
        [[nodiscard]] RenderTargetType get_type() const { return m_type; }
        [[nodiscard]] bool is_dirty() const { return m_needs_recreate; }

    protected:
        RenderTarget(Device* device, RenderPass* render_pass, const RenderTargetType type, const RenderTargetInfo info = {},
        const std::vector<vk::ClearValue>& clear_values = {}, const bool is_dirty = false): m_device(device), m_render_pass(render_pass),
        m_type(type), m_info(info), m_clear_values(clear_values), m_needs_recreate(is_dirty), m_current_image_index(0) {}

        void clear_render_resources(std::vector<RenderResource>& resources);

        Device* m_device;
        RenderPass* m_render_pass;
        RenderTargetType m_type;
        RenderTargetInfo m_info;
        std::vector<vk::ClearValue> m_clear_values;
        bool m_needs_recreate;
        uint32_t m_current_image_index;
    };

    class SwapchainTarget final: public RenderTarget {
    public:
        SwapchainTarget(Device* device, RenderPass* render_pass, Swapchain* swapchain, const RenderTargetInfo info = {});
        ~SwapchainTarget() override;

        void recreate() override;
        void begin_render_target(CommandBuffer& command_buffer) override;

        std::shared_ptr<FrameBuffer> get_framebuffer(const uint32_t index = 0) const override { return m_render_resources.at(index).frame_buffer; }

    private:
        Swapchain* m_swapchain;
        std::vector<RenderResource> m_render_resources;
    };

    class OffscreenTarget final: public RenderTarget {
    public:
        OffscreenTarget(Device* device, RenderPass* render_pass, const RenderTargetInfo info = {});
        ~OffscreenTarget() override;

        void recreate() override;

        std::shared_ptr<FrameBuffer> get_framebuffer(const uint32_t index = 0) const override { return m_frame_buffer; }

    private:
        std::shared_ptr<FrameBuffer> m_frame_buffer;
        std::shared_ptr<Image> m_color_image;
        std::shared_ptr<ImageView> m_color_view;
        std::shared_ptr<Image> m_depth_image;
        std::shared_ptr<ImageView> m_depth_view;
    };

    class MultiTarget final: public RenderTarget {
    public:
        MultiTarget(Device* device, RenderPass* render_pass, const RenderTargetInfo info = {});
        ~MultiTarget() override;

        void recreate() override;

        std::shared_ptr<FrameBuffer> get_framebuffer(const uint32_t index = 0) const override { return m_render_resources.at(index).frame_buffer; }

    private:
        std::vector<RenderResource> m_render_resources;
    };
}

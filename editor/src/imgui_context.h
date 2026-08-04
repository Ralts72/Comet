#pragma once
#include "graphics/vk_common.h"
#include "graphics/enums.h"
#include "common/config.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Comet {
    class RenderContext;
    class RenderPass;
    class CommandBuffer;
    class Window;
    class FrameBuffer;
    class DescriptorPool;
    class RenderTarget;
    class Sampler;
}

namespace CometEditor {

    struct RenderFormatInfo {
        Comet::Format color_format{};
        Comet::Format depth_format{};
    };

    class ImGuiContext {
    public:
        ImGuiContext(const Comet::Window* window, Comet::RenderContext* render_context,
                     const Comet::Config::Vulkan& vulkan_config);
        ~ImGuiContext();

        ImGuiContext(const ImGuiContext&) = delete;
        ImGuiContext& operator=(const ImGuiContext&) = delete;

        void update_frame() const;
        void render(Comet::CommandBuffer& command_buffer) const;

        void recreate_swapchain();

        void set_viewport_images(
            std::vector<vk::ImageView> image_views,
            std::shared_ptr<Comet::Sampler> sampler);

        [[nodiscard]] std::uint64_t get_viewport_texture_id(uint32_t frame_index) const;

        using UICallback = std::function<void()>;
        void set_ui_callback(UICallback callback) { m_ui_callback = std::move(callback); }

    private:

        void init_vulkan();
        void create_render_pass();
        void cleanup();
        void register_viewport_textures();
        void unregister_viewport_textures();

        const Comet::Window* m_window;
        Comet::RenderContext* m_render_context;
        std::unique_ptr<Comet::RenderPass> m_render_pass;
        std::unique_ptr<Comet::RenderTarget> m_render_target;
        std::unique_ptr<Comet::DescriptorPool> m_descriptor_pool;
        std::vector<vk::ImageView> m_viewport_image_views;
        std::shared_ptr<Comet::Sampler> m_viewport_sampler;
        std::vector<VkDescriptorSet> m_viewport_texture_ids;
        UICallback m_ui_callback;
        bool m_initialized = false;
        bool m_is_recreating = false;  // 标记是否正在重建 swapchain
        uint32_t m_backend_image_count = 0;

        RenderFormatInfo m_render_format_info;
        Comet::Config::Vulkan m_vulkan_config;
    };
}

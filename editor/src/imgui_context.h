#pragma once

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
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
    class ImageView;
}

namespace CometEditor {
    class ImGuiContext {
    public:
        ImGuiContext(const Comet::Window& window, Comet::RenderContext& render_context,
            std::filesystem::path ini_path);
        ~ImGuiContext();

        ImGuiContext(const ImGuiContext&) = delete;
        ImGuiContext& operator=(const ImGuiContext&) = delete;

        void update_frame() const;
        void render(Comet::CommandBuffer& command_buffer) const;

        void recreate_swapchain();

        void set_viewport_images(
            std::vector<std::shared_ptr<Comet::ImageView>> image_views,
            std::shared_ptr<Comet::Sampler> sampler);

        [[nodiscard]] ImTextureID get_viewport_texture_id(uint32_t frame_index) const;

        using UICallback = std::function<void()>;
        void set_ui_callback(UICallback callback) { m_ui_callback = std::move(callback); }

    private:
        class TextureBinding;

        void init_vulkan();
        void create_render_pass();
        void cleanup();
        void register_viewport_textures();
        void unregister_viewport_textures();

        const Comet::Window& m_window;
        Comet::RenderContext& m_render_context;
        std::string m_ini_path;
        std::unique_ptr<Comet::RenderPass> m_render_pass;
        std::unique_ptr<Comet::RenderTarget> m_render_target;
        std::unique_ptr<Comet::DescriptorPool> m_descriptor_pool;
        std::vector<std::unique_ptr<TextureBinding>> m_viewport_textures;
        UICallback m_ui_callback;
        bool m_initialized = false;
        bool m_is_recreating = false; // 标记是否正在重建 Swapchain
        uint32_t m_backend_image_count = 0;
    };
}

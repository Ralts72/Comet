#pragma once

#include <imgui.h>
#include "graphics/result.h"

#include <cstdint>
#include <filesystem>
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
    struct SwapchainCompatibility;
}

namespace CometUi {
    class ImGuiContext {
    public:
        // Preserve 要求当前交换链图像已由场景 pass 写入并处于 PresentSrcKHR。
        enum class Composition { Clear, Preserve };
        struct Options {
            // 空路径不保存布局；空字体目录使用 16px 内建字体。
            std::filesystem::path ini_path;
            std::filesystem::path font_directory;
            bool docking = false;
            Composition composition = Composition::Preserve;
        };

        static Comet::Result<std::unique_ptr<ImGuiContext>, Comet::GraphicsError> create(
            const Comet::Window& window, Comet::RenderContext& render_context, Options options);
        ~ImGuiContext();

        ImGuiContext(const ImGuiContext&) = delete;
        ImGuiContext& operator=(const ImGuiContext&) = delete;

        [[nodiscard]] bool begin_frame();
        void end_frame();
        void render(Comet::CommandBuffer& command_buffer) const;

        void release_swapchain_resources();
        Comet::Result<void, Comet::GraphicsError> rebuild_swapchain_resources(
            const Comet::SwapchainCompatibility& compatibility);

        void set_viewport_image(uint32_t frame_slot_index,
            std::shared_ptr<Comet::ImageView> image_view, std::shared_ptr<Comet::Sampler> sampler);

        [[nodiscard]] ImTextureID get_viewport_texture_id(uint32_t frame_index) const;

    private:
        class TextureBinding;
        struct ContextDeleter {
            void operator()(::ImGuiContext* context) const noexcept;
        };

        ImGuiContext(
            const Comet::Window& window, Comet::RenderContext& render_context, Options options);
        Comet::Result<void, Comet::GraphicsError> initialize();
        Comet::Result<void, Comet::GraphicsError> init_vulkan();
        Comet::Result<void, Comet::GraphicsError> create_render_pass();
        void cleanup() noexcept;
        void register_viewport_textures();
        void unregister_viewport_textures();

        const Comet::Window& m_window;
        Comet::RenderContext& m_render_context;
        Options m_options;
        std::string m_ini_path;
        std::unique_ptr<Comet::RenderPass> m_render_pass;
        std::unique_ptr<Comet::RenderTarget> m_render_target;
        std::unique_ptr<Comet::DescriptorPool> m_descriptor_pool;
        std::vector<std::unique_ptr<TextureBinding>> m_viewport_textures;
        bool m_draw_data_ready = false;
        bool m_initialized = false;
        bool m_is_recreating = false;
        uint32_t m_backend_image_count = 0;
        // 后端借用上面的 GPU 资源；构造失败时必须先关闭后端。
        std::unique_ptr<::ImGuiContext, ContextDeleter> m_context;
    };
}

#pragma once

#include <imgui.h>
#include "graphics/result.h"
#include "render/overlay_record_context.h"
#include "ui/icons.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace Comet {
    class RenderContext;
    class RenderPass;
    class Window;
    class DescriptorPool;
    class RenderTarget;
    class Sampler;
    class ImageView;
    struct SwapchainCompatibility;
    class OverlayRecordContext;
}

namespace CometEditor::Ui {
    class ImGuiContext {
    public:
        class Frame final: public Comet::OverlayDraw {
        public:
            ~Frame() override;
            Comet::Result<void, Comet::GraphicsError> record(
                Comet::OverlayRecordContext& overlay) override;
            Frame(const Frame&) = delete;
            Frame& operator=(const Frame&) = delete;

        private:
            friend class ImGuiContext;
            class Data;
            explicit Frame(ImGuiContext& backend);
            ImGuiContext& m_backend;
            std::unique_ptr<Data> m_data;
        };
        // Preserve 要求当前交换链图像已由场景 pass 写入并处于 PresentSrcKHR。
        enum class Composition { Clear, Preserve };
        struct Options {
            // 空路径不保存布局；空字体目录使用 engine 中的 16px 共用字体。
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
        // 交付独立持有的绘制数据和字体像素；录制消费一次并由帧槽保活 GPU 资源。
        std::unique_ptr<Frame> end_frame();

        void release_swapchain_resources();
        // GPU 已空闲后在绘制所属线程调用；窗口输入和布局仍由 Main 关闭。
        void shutdown_gpu() noexcept;
        Comet::Result<void, Comet::GraphicsError> rebuild_swapchain_resources(
            const Comet::SwapchainCompatibility& compatibility);

        // 前端只保存图片引用；同一 View／帧槽的标识稳定，end_frame 固定当时的图片版本。
        void set_viewport_image(uint32_t frame_slot_index,
            std::shared_ptr<Comet::ImageView> image_view, std::shared_ptr<Comet::Sampler> sampler,
            uint32_t view_id = 0);

        // 仅用于本上下文的 ImGui 图片绘制；此标识不是 Vulkan descriptor。
        [[nodiscard]] ImTextureID get_viewport_texture_id(
            uint32_t frame_index, uint32_t view_id = 0) const;

    private:
        Comet::Result<void, Comet::GraphicsError> render(
            Comet::OverlayRecordContext& overlay, const Frame& frame);
        class TextureBinding;
        class FontTexture;
        struct FontPixels;
        struct ViewportImage;
        struct ContextDeleter {
            void operator()(::ImGuiContext* context) const noexcept;
        };

        ImGuiContext(
            const Comet::Window& window, Comet::RenderContext& render_context, Options options);
        Comet::Result<void, Comet::GraphicsError> initialize();
        Comet::Result<void, Comet::GraphicsError> init_vulkan();
        void shutdown_vulkan() noexcept;
        Comet::Result<void, Comet::GraphicsError> create_render_pass();
        void cleanup() noexcept;

        const Comet::Window& m_window;
        Comet::RenderContext& m_render_context;
        Options m_options;
        std::string m_ini_path;
        std::unique_ptr<Comet::RenderPass> m_render_pass;
        std::unique_ptr<Comet::RenderTarget> m_render_target;
        std::unique_ptr<Comet::DescriptorPool> m_descriptor_pool;
        std::map<uint32_t, std::vector<std::shared_ptr<const ViewportImage>>> m_viewport_images;
        ImTextureID m_next_viewport_texture_id = 1;
        std::map<int, std::shared_ptr<const FontPixels>> m_font_pixels;
        std::map<ImTextureID, std::shared_ptr<TextureBinding>> m_viewport_textures;
        std::vector<std::weak_ptr<TextureBinding>> m_viewport_versions;
        std::map<int, std::shared_ptr<FontTexture>> m_font_textures;
        std::vector<std::weak_ptr<FontTexture>> m_font_versions;
        std::uint64_t m_presentation_generation = 0;
        bool m_initialized = false;
        bool m_is_recreating = false;
        uint32_t m_backend_image_count = 0;
        IconAtlas m_icons;
        // 前端只持有输入、布局和字体；GPU 上下文只持有 Vulkan 后端及录制视口。
        std::unique_ptr<::ImGuiContext, ContextDeleter> m_frontend_context;
        // 后端借用上面的 GPU 资源；构造失败时必须先关闭后端。
        std::unique_ptr<::ImGuiContext, ContextDeleter> m_backend_context;
    };
}

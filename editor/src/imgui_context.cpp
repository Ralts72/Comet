#include "imgui_context.h"
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/render_pass.h"
#include "graphics/command/command_buffer.h"
#include "graphics/attachment.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "graphics/swapchain.h"
#include "graphics/enums.h"
#include "graphics/pipeline/descriptor_set.h"
#include "render/render_context.h"
#include "render/render_target.h"
#include "diagnostics/logger.h"
#include "core/window.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace CometEditor {
    namespace {
        template<typename Handle>
        ImTextureID handle_to_texture_id(const Handle handle) {
            if constexpr(std::is_pointer_v<Handle>) {
                return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(handle));
            } else {
                return static_cast<ImTextureID>(handle);
            }
        }
    }

    class ImGuiContext::TextureBinding final {
    public:
        TextureBinding(
            std::shared_ptr<Comet::ImageView> image_view,
            std::shared_ptr<Comet::Sampler> sampler)
            : m_image_view(std::move(image_view)), m_sampler(std::move(sampler)) {
            if(!m_image_view || !m_sampler) {
                LOG_FATAL("ImGui texture binding requires a valid image view and sampler");
            }
        }

        ~TextureBinding() {
            unregister_texture();
        }

        TextureBinding(const TextureBinding&) = delete;
        TextureBinding& operator=(const TextureBinding&) = delete;
        TextureBinding(TextureBinding&&) noexcept = delete;
        TextureBinding& operator=(TextureBinding&&) noexcept = delete;

        [[nodiscard]] bool matches(
            const std::shared_ptr<Comet::ImageView>& image_view,
            const std::shared_ptr<Comet::Sampler>& sampler) const {
            return m_image_view == image_view && m_sampler == sampler;
        }

        void register_texture() {
            if(m_descriptor_set != VK_NULL_HANDLE) {
                return;
            }

            m_descriptor_set = ImGui_ImplVulkan_AddTexture(
                static_cast<VkSampler>(m_sampler->get()),
                static_cast<VkImageView>(m_image_view->get()),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        void unregister_texture() {
            if(m_descriptor_set == VK_NULL_HANDLE) {
                return;
            }

            ImGui_ImplVulkan_RemoveTexture(m_descriptor_set);
            m_descriptor_set = VK_NULL_HANDLE;
        }

        [[nodiscard]] ImTextureID get_texture_id() const {
            return handle_to_texture_id(m_descriptor_set);
        }

    private:
        std::shared_ptr<Comet::ImageView> m_image_view;
        std::shared_ptr<Comet::Sampler> m_sampler;
        VkDescriptorSet m_descriptor_set = VK_NULL_HANDLE;
    };

    ImGuiContext::ImGuiContext(
        const Comet::Window& window,
        Comet::RenderContext& render_context,
        std::filesystem::path ini_path)
        : m_window(window),
          m_render_context(render_context),
          m_ini_path(std::move(ini_path).string()) {
        LOG_INFO("Initializing ImGui layer");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        const std::filesystem::path ini_directory =
            std::filesystem::path(m_ini_path).parent_path();
        if(!ini_directory.empty()) {
            std::error_code error;
            std::filesystem::create_directories(ini_directory, error);
            if(error) {
                LOG_WARN(
                    "Failed to create ImGui state directory '{}': {}",
                    ini_directory.string(),
                    error.message());
            }
        }
        io.IniFilename = m_ini_path.c_str();

        const std::string font_path =
            std::string(COMET_EDITOR_RESOURCE_DIRECTORY) + "/fonts/Roboto-Regular.ttf";
        io.Fonts->AddFontFromFileTTF(font_path.c_str(), 16.0f);

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(window.get(), true);

        auto& swapchain = m_render_context.get_swapchain();

        create_render_pass();

        auto& device = m_render_context.get_device();
        m_render_target = Comet::RenderTarget::create_swapchain_target(
            device, *m_render_pass, swapchain);
        m_render_target->set_clear_value(Comet::ClearValue(Comet::Math::Vec4(0.0f, 0.0f, 0.0f, 0.0f)), 0);
        // 初始化 Vulkan backend
        init_vulkan();

        m_initialized = true;
        LOG_INFO("ImGui layer initialized successfully");
    }

    void ImGuiContext::create_render_pass() {
        LOG_INFO("Creating independent RenderPass for ImGui");

        std::vector<Comet::Attachment> attachments;
        const auto color_format = m_render_context.get_swapchain()
                                  .get_images()[0]->get_info().format;
        auto color_attachment = Comet::Attachment::get_color_attachment(color_format, Comet::SampleCount::Count1);
        color_attachment.description.load_op = Comet::AttachmentLoadOp::Clear;
        color_attachment.description.initial_layout = Comet::ImageLayout::Undefined;
        color_attachment.description.final_layout = Comet::ImageLayout::PresentSrcKHR;
        color_attachment.description.store_op = Comet::AttachmentStoreOp::Store;
        attachments.emplace_back(color_attachment);

        // 创建 SubPass
        std::vector<Comet::RenderSubPass> render_sub_passes;
        Comet::RenderSubPass render_sub_pass = {
            {},
            {Comet::SubpassColorAttachment(0)},
            {}, // ImGui 不需要深度测试
            Comet::SampleCount::Count1 // ImGui 不使用 MSAA
        };
        render_sub_passes.emplace_back(render_sub_pass);

        // 创建独立的 RenderPass
        auto& device = m_render_context.get_device();
        m_render_pass = std::make_unique<Comet::RenderPass>(
            device, attachments, render_sub_passes);
    }

    void ImGuiContext::init_vulkan() {
        const auto& context = m_render_context.get_context();
        auto& device = m_render_context.get_device();
        const auto& swapchain = m_render_context.get_swapchain();
        m_backend_image_count = static_cast<uint32_t>(swapchain.get_images().size());
        if(m_backend_image_count < 2) {
            LOG_FATAL("ImGui Vulkan backend requires at least two swapchain images");
        }

        // 使用自定义 DescriptorPool
        Comet::DescriptorPoolSizes pool_sizes;
        pool_sizes.add_pool_size(Comet::DescriptorType::CombinedImageSampler, 100);

        m_descriptor_pool = std::make_unique<Comet::DescriptorPool>(device, 100, pool_sizes,
            Comet::Flags<Comet::DescriptorPoolCreateFlag>(Comet::DescriptorPoolCreateFlag::FreeDescriptorSet));

        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.ApiVersion = VK_API_VERSION_1_0;
        init_info.Instance = context.instance();
        init_info.PhysicalDevice = context.get_physical_device();
        init_info.Device = device.get();
        init_info.QueueFamily = context.get_graphics_queue_family().queue_family_index.value();
        init_info.Queue = device.get_graphics_queue(0).get();
        init_info.DescriptorPool = m_descriptor_pool->get();
        init_info.MinImageCount = m_backend_image_count;
        init_info.ImageCount = m_backend_image_count;
        init_info.PipelineInfoMain.RenderPass = m_render_pass->get();
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init(&init_info);
    }

    ImGuiContext::~ImGuiContext() {
        if(m_initialized) {
            cleanup();
        }
    }

    void ImGuiContext::cleanup() {
        LOG_INFO("Cleaning up ImGui layer");

        if(!m_initialized) {
            return;
        }

        m_render_context.wait_idle();

        unregister_viewport_textures();

        // 先关闭 ImGui Vulkan backend，让 ImGui 释放 DescriptorPool 引用
        ImGui_ImplVulkan_Shutdown();

        // 销毁 DescriptorPool（必须在关闭 backend 之后）
        m_descriptor_pool.reset();

        // 销毁其他资源
        m_render_target.reset();
        m_render_pass.reset();
        m_viewport_textures.clear();

        // 销毁 GLFW backend 和 ImGui context
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        m_initialized = false;
    }

    void ImGuiContext::update_frame() const {
        if(!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized, skipping update_frame");
            return;
        }

        // 如果正在重建 Swapchain，跳过 ImGui 更新
        if(m_is_recreating) {
            return;
        }

        // 检查窗口有效性
        if(!m_window.get()) {
            LOG_WARN("Window is invalid, skipping ImGui frame");
            return;
        }

        ImGui_ImplVulkan_NewFrame();

        // 检查窗口是否最小化（最小化时跳过 GLFW backend 更新）
        if(glfwGetWindowAttrib(m_window.get(), GLFW_ICONIFIED)) {
            ImGui::NewFrame();
        } else {
            // 正常更新 GLFW backend
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        // 添加 UI 元素
        if(m_ui_callback) {
            m_ui_callback();
        }

        // 生成绘制命令
        ImGui::Render();
    }

    void ImGuiContext::render(Comet::CommandBuffer& command_buffer) const {
        if(!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized");
            return;
        }

        m_render_target->begin_render_target(command_buffer);

        ImDrawData* draw_data = ImGui::GetDrawData();
        if(draw_data && draw_data->CmdListsCount > 0) {
            ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer.get());
        }

        m_render_target->end_render_target(command_buffer);
    }

    void ImGuiContext::recreate_swapchain() {
        LOG_INFO("Recreating ImGui resources for swapchain");

        if(!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized, cannot recreate swapchain");
            return;
        }

        // 标记正在重建，防止在重建过程中调用 update_frame
        m_is_recreating = true;

        // 等待设备空闲
        m_render_context.wait_idle();

        // 重建 RenderTarget
        m_render_target->recreate();
        const auto image_count = static_cast<uint32_t>(
            m_render_context.get_swapchain().get_images().size());
        if(image_count != m_backend_image_count) {
            unregister_viewport_textures();
            ImGui_ImplVulkan_Shutdown();
            m_descriptor_pool.reset();
            init_vulkan();
            register_viewport_textures();
        }
        // 重建完成，恢复 ImGui 更新
        m_is_recreating = false;

        LOG_INFO("ImGui resources recreated successfully");
    }

    void ImGuiContext::set_viewport_image(
        const uint32_t frame_slot_index,
        std::shared_ptr<Comet::ImageView> image_view,
        std::shared_ptr<Comet::Sampler> sampler) {
        if(!image_view || !sampler) {
            LOG_FATAL("Viewport texture binding requires a valid image view and sampler");
        }
        if(frame_slot_index < m_viewport_textures.size()
           && m_viewport_textures[frame_slot_index]
           && m_viewport_textures[frame_slot_index]->matches(
               image_view, sampler)) {
            return;
        }

        if(frame_slot_index >= m_viewport_textures.size()) {
            m_viewport_textures.resize(frame_slot_index + 1);
        }
        auto binding = std::make_unique<TextureBinding>(
            std::move(image_view), std::move(sampler));
        if(m_initialized) {
            binding->register_texture();
        }
        m_viewport_textures[frame_slot_index] = std::move(binding);
    }

    ImTextureID ImGuiContext::get_viewport_texture_id(const uint32_t frame_index) const {
        if(frame_index >= m_viewport_textures.size()) {
            return ImTextureID_Invalid;
        }
        const auto& binding = m_viewport_textures[frame_index];
        return binding ? binding->get_texture_id() : ImTextureID_Invalid;
    }

    void ImGuiContext::register_viewport_textures() {
        if(!m_initialized) {
            return;
        }

        for(const auto& texture: m_viewport_textures) {
            if(texture) {
                texture->register_texture();
            }
        }
    }

    void ImGuiContext::unregister_viewport_textures() {
        if(m_initialized) {
            for(const auto& texture: m_viewport_textures) {
                if(texture) {
                    texture->unregister_texture();
                }
            }
        }
    }
}

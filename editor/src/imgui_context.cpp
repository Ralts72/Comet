#include "imgui_context.h"
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/render_pass.h"
#include "graphics/command_buffer.h"
#include "graphics/image_view.h"
#include "graphics/frame_buffer.h"
#include "graphics/attachment.h"
#include "graphics/swapchain.h"
#include "graphics/image.h"
#include "graphics/enums.h"
#include "graphics/descriptor_set.h"
#include "render/render_context.h"
#include "common/logger.h"
#include "common/config.h"
#include "core/window.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>

namespace CometEditor {

    ImGuiContext::ImGuiContext(const Comet::Window* window, Comet::RenderContext* render_context)
        : m_window(window), m_render_context(render_context) {
        LOG_INFO("Initializing ImGui layer");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        // 设置 imgui.ini 路径到 editor 目录
        static std::string ini_path = std::string(PROJECT_ROOT_DIR) + "/editor/imgui.ini";
        io.IniFilename = ini_path.c_str();

        // 加载字体
        std::string font_path = std::string(PROJECT_ROOT_DIR) + "/editor/assets/fonts/Roboto-Regular.ttf";
        io.Fonts->AddFontFromFileTTF(font_path.c_str(), 16.0f);

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(window->get(), true);

        // 创建独立的 RenderPass
        create_render_pass();

        // 创建独立的 Framebuffers
        create_framebuffers();

        // 初始化 Vulkan backend
        init_vulkan();

        m_initialized = true;
        LOG_INFO("ImGui layer initialized successfully");
    }

    void ImGuiContext::create_render_pass() {
        LOG_INFO("Creating independent RenderPass for ImGui");

        auto swapchain = m_render_context->get_swapchain();

        // 从 Swapchain 获取格式
        Comet::Format color_format = swapchain->get_images()[0]->get_info().format;

        // 从配置读取深度格式和 MSAA 设置
        const auto depth_format = static_cast<Comet::Format>(Comet::Config::get<int>("vulkan.depth_format", 126)); // D32_SFLOAT = 126
        const auto msaa_samples = static_cast<Comet::SampleCount>(Comet::Config::get<int>("vulkan.msaa_samples", 4)); // Count4 = 4

        // 创建 Attachment
        std::vector<Comet::Attachment> attachments;
        attachments.emplace_back(Comet::Attachment::get_color_attachment(color_format, msaa_samples));
        attachments.emplace_back(Comet::Attachment::get_depth_attachment(depth_format, msaa_samples));

        // 创建 SubPass
        std::vector<Comet::RenderSubPass> render_sub_passes;
        Comet::RenderSubPass render_sub_pass = {
            {},
            {Comet::SubpassColorAttachment(0)},
            {Comet::SubpassDepthStencilAttachment(1)},
            msaa_samples
        };
        render_sub_passes.emplace_back(render_sub_pass);

        // 创建独立的 RenderPass
        auto* device = m_render_context->get_device();
        m_render_pass = std::make_unique<Comet::RenderPass>(device, attachments, render_sub_passes);
    }

    void ImGuiContext::create_framebuffers() {
        LOG_INFO("Creating independent Framebuffers for ImGui");

        auto* device = m_render_context->get_device();
        auto swapchain = m_render_context->get_swapchain();
        uint32_t image_count = static_cast<uint32_t>(swapchain->get_images().size());

        m_framebuffers.clear();
        m_framebuffers.reserve(image_count);

        // 获取深度格式和 MSAA 设置
        const auto depth_format = static_cast<Comet::Format>(Comet::Config::get<int>("vulkan.depth_format", 126));
        const auto msaa_samples = static_cast<Comet::SampleCount>(Comet::Config::get<int>("vulkan.msaa_samples", 4));

        // 为每个 Swapchain Image 创建 Framebuffer
        for (uint32_t i = 0; i < image_count; ++i) {
            std::vector<std::shared_ptr<Comet::ImageView>> all_views;

            if (msaa_samples > Comet::SampleCount::Count1) {
                // MSAA > 1: 需要 3 个 attachments (color multisampled, resolve, depth)

                // 1. Color Attachment (multisampled) - 创建临时的 multisampled image
                Comet::ImageInfo color_info = {};
                color_info.format = swapchain->get_images()[i]->get_info().format;
                color_info.extent = {swapchain->get_width(), swapchain->get_height(), 1};
                color_info.usage = Comet::Flags<Comet::ImageUsage>(Comet::ImageUsage::ColorAttachment);

                auto color_image = Comet::Image::create(device, color_info, msaa_samples);
                auto color_view = std::make_shared<Comet::ImageView>(
                    device, *color_image, Comet::Flags<Comet::ImageAspect>(Comet::ImageAspect::Color));
                all_views.push_back(color_view);

                // 2. Depth Attachment (multisampled)
                Comet::ImageInfo depth_info = {};
                depth_info.format = depth_format;
                depth_info.extent = {swapchain->get_width(), swapchain->get_height(), 1};
                depth_info.usage = Comet::Flags<Comet::ImageUsage>(Comet::ImageUsage::DepthStencilAttachment);

                auto depth_image = Comet::Image::create(device, depth_info, msaa_samples);
                auto depth_view = std::make_shared<Comet::ImageView>(
                    device, *depth_image, Comet::Flags<Comet::ImageAspect>(Comet::ImageAspect::Depth));
                all_views.push_back(depth_view);

                // 3. Resolve Attachment (single sampled) - 使用 Swapchain Image
                auto swapchain_image = swapchain->get_images()[i];
                auto resolve_view = std::make_shared<Comet::ImageView>(
                    device, *swapchain_image, Comet::Flags<Comet::ImageAspect>(Comet::ImageAspect::Color));
                all_views.push_back(resolve_view);
            } else {
                // MSAA = 1: 只需要 2 个 attachments (color, depth)

                // 1. Color Attachment - 使用 Swapchain Image
                auto swapchain_image = swapchain->get_images()[i];
                auto color_view = std::make_shared<Comet::ImageView>(
                    device, *swapchain_image, Comet::Flags<Comet::ImageAspect>(Comet::ImageAspect::Color));
                all_views.push_back(color_view);

                // 2. Depth Attachment
                Comet::ImageInfo depth_info = {};
                depth_info.format = depth_format;
                depth_info.extent = {swapchain->get_width(), swapchain->get_height(), 1};
                depth_info.usage = Comet::Flags<Comet::ImageUsage>(Comet::ImageUsage::DepthStencilAttachment);

                auto depth_image = Comet::Image::create(device, depth_info, msaa_samples);
                auto depth_view = std::make_shared<Comet::ImageView>(
                    device, *depth_image, Comet::Flags<Comet::ImageAspect>(Comet::ImageAspect::Depth));
                all_views.push_back(depth_view);
            }

            // 创建 Framebuffer
            auto framebuffer = std::make_shared<Comet::FrameBuffer>(
                device, m_render_pass.get(), all_views,
                swapchain->get_width(), swapchain->get_height());

            m_framebuffers.push_back(framebuffer);
        }

        LOG_INFO("Created {} Framebuffers for ImGui", m_framebuffers.size());
    }


    void ImGuiContext::init_vulkan() {
        auto* context = m_render_context->get_context();
        auto* device = m_render_context->get_device();

        // 使用自定义的 DescriptorPool
        Comet::DescriptorPoolSizes pool_sizes;
        pool_sizes.add_pool_size(Comet::DescriptorType::CombinedImageSampler, 100);

        m_descriptor_pool = std::make_unique<Comet::DescriptorPool>(
            device, 100, pool_sizes, Comet::Flags<Comet::DescriptorPoolCreateFlag>(Comet::DescriptorPoolCreateFlag::FreeDescriptorSet));

        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.ApiVersion = VK_API_VERSION_1_0;
        init_info.Instance = context->instance();
        init_info.PhysicalDevice = context->get_physical_device();
        init_info.Device = device->get();
        init_info.QueueFamily = context->get_graphics_queue_family().queue_family_index.value();
        init_info.Queue = device->get_graphics_queue(0).get();
        init_info.DescriptorPool = m_descriptor_pool->get();
        init_info.MinImageCount = 2;
        init_info.ImageCount = 2;
        init_info.PipelineInfoMain.RenderPass = m_render_pass->get();
        // 从配置读取 MSAA 设置
        int msaa = Comet::Config::get<int>("vulkan.msaa_samples", 4);
        init_info.PipelineInfoMain.MSAASamples = static_cast<VkSampleCountFlagBits>(msaa);

        ImGui_ImplVulkan_Init(&init_info);
    }

    ImGuiContext::~ImGuiContext() {
        if (m_initialized) {
            cleanup();
        }
    }

    void ImGuiContext::cleanup() {
        LOG_INFO("Cleaning up ImGui layer");

        if (!m_initialized) {
            return;
        }

        auto* device = m_render_context->get_device();
        m_render_context->wait_idle();

        // 先 Shutdown ImGui Vulkan backend，让 ImGui 释放 DescriptorPool 的引用
        ImGui_ImplVulkan_Shutdown();

        // 销毁 DescriptorPool（必须在 Shutdown 之后）
        m_descriptor_pool.reset();

        // 销毁其他资源
        m_framebuffers.clear();
        m_render_pass.reset();

        // 销毁 GLFW backend 和 ImGui context
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        m_initialized = false;
    }

    void ImGuiContext::begin_frame() {
        if (!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized, skipping begin_frame");
            return;
        }

        // 如果正在重建 swapchain，跳过 ImGui 更新
        if (m_is_recreating) {
            return;
        }

        // 检查窗口有效性（防止在窗口 resize 时崩溃）
        if (!m_window || !m_window->get()) {
            LOG_WARN("Window is invalid, skipping ImGui frame");
            return;
        }

        ImGui_ImplVulkan_NewFrame();

        // 在窗口 resize 时，GLFW 可能会使窗口处于无效状态
        // 为了安全，我们在调用 ImGui_ImplGlfw_NewFrame 之前再次验证窗口指针
        GLFWwindow* glfw_window = m_window->get();
        if (!glfw_window) {
            LOG_WARN("Window pointer is invalid, skipping GLFW backend update");
            ImGui::NewFrame();
            return;
        }

        // 检查窗口是否最小化（最小化时跳过 GLFW backend 更新）
        // 注意：glfwGetWindowAttrib 在窗口无效时会断言失败，所以我们在调用前已经检查了窗口指针
        int iconified = glfwGetWindowAttrib(glfw_window, GLFW_ICONIFIED);
        if (iconified) {
            // 窗口最小化时，只更新 Vulkan backend，不更新 GLFW backend
            ImGui::NewFrame();
            return;
        }

        // 正常更新 GLFW backend
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (m_ui_callback) {
            m_ui_callback();
        }
    }

    void ImGuiContext::end_frame() {
        ImGui::Render();
    }

    void ImGuiContext::render(Comet::CommandBuffer& command_buffer) {
        if (!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized");
            return;
        }

        ImDrawData* draw_data = ImGui::GetDrawData();
        if (!draw_data || draw_data->CmdListsCount == 0) {
            return;
        }

        // 注意：ImGui 渲染应该在独立的 RenderPass 中进行
        // 但当前实现中，我们仍然在同一个 RenderPass 内渲染（为了简化）
        // 如果需要完全独立，需要在这里开始新的 RenderPass
        ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer.get());
    }

    void ImGuiContext::recreate_swapchain() {
        LOG_INFO("Recreating ImGui resources for swapchain");

        if (!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized, cannot recreate swapchain");
            return;
        }

        // 标记正在重建，防止在重建过程中调用 begin_frame
        m_is_recreating = true;

        // 1. 等待设备空闲
        auto* device = m_render_context->get_device();
        m_render_context->wait_idle();

        // 2. 销毁旧的 Framebuffers
        m_framebuffers.clear();

        // 3. 重建 RenderPass（如果格式变化）
        create_render_pass();

        // 4. 重建 Framebuffers
        create_framebuffers();

        // 5. 重新初始化 ImGui Vulkan Backend（更新 RenderPass）
        // 注意：ImGui 的 RenderPass 已经改变，需要重新初始化
        // 先 Shutdown，让 ImGui 释放 DescriptorPool 的引用
        ImGui_ImplVulkan_Shutdown();

        // 销毁旧的 DescriptorPool（在 Shutdown 之后）
        m_descriptor_pool.reset();

        // 重新初始化 GLFW backend（确保窗口指针正确）
        ImGui_ImplGlfw_Shutdown();
        ImGui_ImplGlfw_InitForVulkan(m_window->get(), true);

        init_vulkan();

        // 注意：新版本的 ImGui 会自动在 NewFrame() 时创建字体纹理
        // 不需要手动调用 ImGui_ImplVulkan_CreateFontsTexture()

        // 重建完成，恢复 ImGui 更新
        m_is_recreating = false;

        LOG_INFO("ImGui resources recreated successfully");
    }
}

#include "imgui_context.h"
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/render_pass.h"
#include "graphics/command_buffer.h"
#include "graphics/attachment.h"
#include "graphics/image.h"
#include "graphics/swapchain.h"
#include "graphics/enums.h"
#include "graphics/descriptor_set.h"
#include "render/render_context.h"
#include "render/render_target.h"
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
        const std::string font_path = std::string(PROJECT_ROOT_DIR) + "/editor/assets/fonts/Roboto-Regular.ttf";
        io.Fonts->AddFontFromFileTTF(font_path.c_str(), 16.0f);

        ImGui::StyleColorsDark();

        ImGui_ImplGlfw_InitForVulkan(window->get(), true);

        // 初始化缓存的格式和采样信息
        const auto swapchain = m_render_context->get_swapchain();
        m_render_format_info.color_format = swapchain->get_images()[0]->get_info().format;
        m_render_format_info.depth_format = static_cast<Comet::Format>(Comet::Config::get<int>("vulkan.depth_format", 126));

        create_render_pass();

        auto* device = m_render_context->get_device();
        m_render_target = Comet::RenderTarget::create_swapchain_target(device, m_render_pass.get(), swapchain);
        m_render_target->set_clear_value(Comet::ClearValue(Comet::Math::Vec4(0.0f, 0.0f, 0.0f, 0.0f)), 0);
        // 初始化 Vulkan backend
        init_vulkan();

        m_initialized = true;
        LOG_INFO("ImGui layer initialized successfully");
    }

    void ImGuiContext::create_render_pass() {
        LOG_INFO("Creating independent RenderPass for ImGui");

        // 创建 Attachment
        std::vector<Comet::Attachment> attachments;
        // 创建 color attachment，但修改 load_op 为 Load，以保留场景渲染的内容
        auto color_attachment = Comet::Attachment::get_color_attachment(m_render_format_info.color_format, Comet::SampleCount::Count1);
        color_attachment.description.load_op = Comet::AttachmentLoadOp::Load;  // 加载而不是清除
        // 场景渲染结束后，swapchain image 处于 PresentSrcKHR 布局
        color_attachment.description.initial_layout = Comet::ImageLayout::PresentSrcKHR;
        // final_layout 应该保持为 PresentSrcKHR，因为这是最终呈现的布局
        color_attachment.description.final_layout = Comet::ImageLayout::PresentSrcKHR;
        // 确保 store_op 是 Store，以保存渲染结果
        color_attachment.description.store_op = Comet::AttachmentStoreOp::Store;
        attachments.emplace_back(color_attachment);

        // 创建 SubPass
        std::vector<Comet::RenderSubPass> render_sub_passes;
        Comet::RenderSubPass render_sub_pass = {
            {},
            {Comet::SubpassColorAttachment(0)},
            {},  // ImGui 不需要深度测试
            Comet::SampleCount::Count1  // ImGui 不使用 MSAA
        };
        render_sub_passes.emplace_back(render_sub_pass);

        // 创建独立的 RenderPass
        auto* device = m_render_context->get_device();
        m_render_pass = std::make_unique<Comet::RenderPass>(device, attachments, render_sub_passes);
    }

    void ImGuiContext::init_vulkan() {
        const auto* context = m_render_context->get_context();
        auto* device = m_render_context->get_device();

        // 使用自定义的 DescriptorPool
        Comet::DescriptorPoolSizes pool_sizes;
        pool_sizes.add_pool_size(Comet::DescriptorType::CombinedImageSampler, 100);

        m_descriptor_pool = std::make_unique<Comet::DescriptorPool>(device, 100, pool_sizes,
            Comet::Flags<Comet::DescriptorPoolCreateFlag>(Comet::DescriptorPoolCreateFlag::FreeDescriptorSet));

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
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

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

        m_render_context->wait_idle();

        // 先 Shutdown ImGui Vulkan backend，让 ImGui 释放 DescriptorPool 的引用
        ImGui_ImplVulkan_Shutdown();

        // 销毁 DescriptorPool（必须在 Shutdown 之后）
        m_descriptor_pool.reset();

        // 销毁其他资源
        m_render_target.reset();
        m_render_pass.reset();

        // 销毁 GLFW backend 和 ImGui context
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        m_initialized = false;
    }

    void ImGuiContext::update_frame() const {
        if (!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized, skipping update_frame");
            return;
        }

        // 如果正在重建 swapchain，跳过 ImGui 更新
        if (m_is_recreating) {
            return;
        }

        // 检查窗口有效性
        if (!m_window || !m_window->get()) {
            LOG_WARN("Window is invalid, skipping ImGui frame");
            return;
        }

        ImGui_ImplVulkan_NewFrame();

        // 检查窗口是否最小化（最小化时跳过 GLFW backend 更新）
        if (glfwGetWindowAttrib(m_window->get(), GLFW_ICONIFIED)) {
            ImGui::NewFrame();
        } else {
            // 正常更新 GLFW backend
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        // 添加 UI 元素
        if (m_ui_callback) {
            m_ui_callback();
        }

        // 生成绘制命令
        ImGui::Render();
    }

    void ImGuiContext::render(Comet::CommandBuffer& command_buffer) const {
        if (!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized");
            return;
        }

        ImDrawData* draw_data = ImGui::GetDrawData();
        if (!draw_data || draw_data->CmdListsCount == 0) {
            return;
        }
        m_render_target->begin_render_target(command_buffer);

        ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer.get());

        m_render_target->end_render_target(command_buffer);
    }

    void ImGuiContext::recreate_swapchain() {
        LOG_INFO("Recreating ImGui resources for swapchain");

        if (!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized, cannot recreate swapchain");
            return;
        }

        // 标记正在重建，防止在重建过程中调用 update_frame
        m_is_recreating = true;

        // 等待设备空闲
        m_render_context->wait_idle();

        // 重建render target
        m_render_target->recreate();
        // 重建完成，恢复 ImGui 更新
        m_is_recreating = false;

        LOG_INFO("ImGui resources recreated successfully");
    }
}

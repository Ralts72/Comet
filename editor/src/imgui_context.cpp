#include "imgui_context.h"
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/render_pass.h"
#include "graphics/command_buffer.h"
#include "common/logger.h"
#include "common/config.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace CometEditor {

    ImGuiContext::ImGuiContext(const Comet::Window* window, Comet::Context* context, Comet::Device* device, Comet::RenderPass* render_pass)
        : m_device(device) {
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
        init_vulkan(context, device, render_pass);

        m_initialized = true;
        LOG_INFO("ImGui layer initialized successfully");
    }

    void ImGuiContext::init_vulkan(Comet::Context* context, Comet::Device* device, Comet::RenderPass* render_pass) {
        std::array<vk::DescriptorPoolSize, 1> pool_sizes = {{
            { vk::DescriptorType::eCombinedImageSampler, 100 }
        }};

        vk::DescriptorPoolCreateInfo pool_info{};
        pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        pool_info.maxSets = 100;
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();

        m_descriptor_pool = device->get().createDescriptorPool(pool_info);

        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.ApiVersion = VK_API_VERSION_1_0;
        init_info.Instance = context->instance();
        init_info.PhysicalDevice = context->get_physical_device();
        init_info.Device = device->get();
        init_info.QueueFamily = context->get_graphics_queue_family().queue_family_index.value();
        init_info.Queue = device->get_graphics_queue(0).get();
        init_info.DescriptorPool = m_descriptor_pool;
        init_info.MinImageCount = 2;
        init_info.ImageCount = 2;
        init_info.PipelineInfoMain.RenderPass = render_pass->get();
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

        m_device->wait_idle();

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        if (m_descriptor_pool) {
            m_device->get().destroyDescriptorPool(m_descriptor_pool);
        }

        m_initialized = false;
    }

    void ImGuiContext::begin_frame() {
        ImGui_ImplVulkan_NewFrame();
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
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer.get());
    }
}


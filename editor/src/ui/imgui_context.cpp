#include "ui/imgui_context.h"
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

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace CometEditor {
    namespace {
        template<typename Handle> ImTextureID handle_to_texture_id(const Handle handle) {
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
            std::shared_ptr<Comet::ImageView> image_view, std::shared_ptr<Comet::Sampler> sampler)
            : m_image_view(std::move(image_view)), m_sampler(std::move(sampler)) {
            if(!m_image_view || !m_sampler) {
                LOG_FATAL("ImGui texture binding requires a valid image view and sampler");
            }
        }

        ~TextureBinding() { unregister_texture(); }

        TextureBinding(const TextureBinding&) = delete;
        TextureBinding& operator=(const TextureBinding&) = delete;
        TextureBinding(TextureBinding&&) noexcept = delete;
        TextureBinding& operator=(TextureBinding&&) noexcept = delete;

        [[nodiscard]] bool matches(const std::shared_ptr<Comet::ImageView>& image_view,
            const std::shared_ptr<Comet::Sampler>& sampler) const {
            return m_image_view == image_view && m_sampler == sampler;
        }

        void register_texture() {
            if(m_descriptor_set != VK_NULL_HANDLE) {
                return;
            }

            m_descriptor_set = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(m_sampler->get()),
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

    void ImGuiContext::ContextDeleter::operator()(::ImGuiContext* context) const noexcept {
        auto* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(context);
        if(ImGui::GetIO().BackendRendererUserData)
            ImGui_ImplVulkan_Shutdown();
        if(ImGui::GetIO().BackendPlatformUserData)
            ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(context);
        if(previous != context)
            ImGui::SetCurrentContext(previous);
    }

    ImGuiContext::ImGuiContext(const Comet::Window& window, Comet::RenderContext& render_context,
        std::filesystem::path ini_path)
        : m_window(window), m_render_context(render_context),
          m_ini_path(std::move(ini_path).string()) {}

    Comet::Result<std::unique_ptr<ImGuiContext>, Comet::GraphicsError> ImGuiContext::create(
        const Comet::Window& window, Comet::RenderContext& render_context,
        std::filesystem::path ini_path) {
        using CreationResult = Comet::Result<std::unique_ptr<ImGuiContext>, Comet::GraphicsError>;
        std::unique_ptr<ImGuiContext> context(
            new ImGuiContext(window, render_context, std::move(ini_path)));
        auto initialized = context->initialize();
        if(!initialized)
            return CreationResult::failure(initialized.error());
        return CreationResult::success(std::move(context));
    }

    Comet::Result<void, Comet::GraphicsError> ImGuiContext::initialize() {
        LOG_INFO("Initializing ImGui layer");

        IMGUI_CHECKVERSION();
        m_context.reset(ImGui::CreateContext());
        ImGui::SetCurrentContext(m_context.get());
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        const std::filesystem::path ini_directory = std::filesystem::path(m_ini_path).parent_path();
        if(!ini_directory.empty()) {
            std::error_code error;
            std::filesystem::create_directories(ini_directory, error);
            if(error) {
                LOG_WARN("Failed to create ImGui state directory '{}': {}", ini_directory.string(),
                    error.message());
            }
        }
        io.IniFilename = m_ini_path.c_str();

        const std::string font_path =
            std::string(COMET_EDITOR_RESOURCE_DIRECTORY) + "/fonts/Roboto-Regular.ttf";
        io.Fonts->AddFontFromFileTTF(font_path.c_str(), 16.0f);

        ImGui::StyleColorsDark();

        if(!ImGui_ImplGlfw_InitForVulkan(m_window.get(), true))
            return Comet::Result<void, Comet::GraphicsError>::failure(
                {"Cannot initialize ImGui GLFW backend"});

        auto& swapchain = m_render_context.get_swapchain();

        if(auto result = create_render_pass(); !result)
            return result;

        auto& device = m_render_context.get_device();
        auto target =
            Comet::RenderTarget::create_swapchain_target(device, *m_render_pass, swapchain);
        if(!target)
            return Comet::Result<void, Comet::GraphicsError>::failure(target.error());
        m_render_target = std::move(target).value();
        m_render_target->set_clear_value(
            Comet::ClearValue(Comet::Math::Vec4(0.0f, 0.0f, 0.0f, 0.0f)), 0);
        if(auto result = init_vulkan(); !result)
            return result;

        m_initialized = true;
        LOG_INFO("ImGui layer initialized successfully");
        return Comet::Result<void, Comet::GraphicsError>::success();
    }

    Comet::Result<void, Comet::GraphicsError> ImGuiContext::create_render_pass() {
        LOG_INFO("Creating independent RenderPass for ImGui");

        std::vector<Comet::Attachment> attachments;
        const auto color_format =
            m_render_context.get_swapchain().get_images()[0]->get_info().format;
        auto color_attachment =
            Comet::Attachment::get_color_attachment(color_format, Comet::SampleCount::Count1);
        color_attachment.description.load_op = Comet::AttachmentLoadOp::Clear;
        color_attachment.description.initial_layout = Comet::ImageLayout::Undefined;
        color_attachment.description.final_layout = Comet::ImageLayout::PresentSrcKHR;
        color_attachment.description.store_op = Comet::AttachmentStoreOp::Store;
        attachments.emplace_back(color_attachment);

        std::vector<Comet::RenderSubPass> render_sub_passes;
        Comet::RenderSubPass render_sub_pass = {
            {}, {Comet::SubpassColorAttachment(0)}, {}, // ImGui 不需要深度测试
            Comet::SampleCount::Count1                  // ImGui 不使用 MSAA
        };
        render_sub_passes.emplace_back(render_sub_pass);

        auto& device = m_render_context.get_device();
        auto pass = Comet::RenderPass::create(device, attachments, render_sub_passes);
        if(!pass)
            return Comet::Result<void, Comet::GraphicsError>::failure(pass.error());
        m_render_pass = std::move(pass).value();
        return Comet::Result<void, Comet::GraphicsError>::success();
    }

    Comet::Result<void, Comet::GraphicsError> ImGuiContext::init_vulkan() {
        const auto& context = m_render_context.get_context();
        auto& device = m_render_context.get_device();
        const auto& swapchain = m_render_context.get_swapchain();
        m_backend_image_count = static_cast<uint32_t>(swapchain.get_images().size());
        if(m_backend_image_count < 2) {
            return Comet::Result<void, Comet::GraphicsError>::failure(
                {"ImGui Vulkan backend requires at least two swapchain images"});
        }

        Comet::DescriptorPoolSizes pool_sizes;
        pool_sizes.add_pool_size(Comet::DescriptorType::CombinedImageSampler, 100);

        auto pool = Comet::DescriptorPool::create(device, 100, pool_sizes,
            Comet::Flags<Comet::DescriptorPoolCreateFlag>(
                Comet::DescriptorPoolCreateFlag::FreeDescriptorSet));
        if(!pool)
            return Comet::Result<void, Comet::GraphicsError>::failure(pool.error());
        m_descriptor_pool = std::move(pool).value();

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

        if(!ImGui_ImplVulkan_Init(&init_info))
            return Comet::Result<void, Comet::GraphicsError>::failure(
                {"Cannot initialize ImGui Vulkan backend"});
        return Comet::Result<void, Comet::GraphicsError>::success();
    }

    ImGuiContext::~ImGuiContext() {
        cleanup();
    }

    void ImGuiContext::cleanup() noexcept {
        if(!m_context) {
            return;
        }
        auto* previous = ImGui::GetCurrentContext();
        auto* context = m_context.get();
        ImGui::SetCurrentContext(context);
        if(m_initialized) {
            m_render_context.get_device().wait_idle_for_shutdown();
        }
        // 后端可能尚未初始化，或者已在 swapchain 重建中关闭。
        if(ImGui::GetIO().BackendRendererUserData) {
            unregister_viewport_textures();
        }
        m_context.reset();

        m_descriptor_pool.reset();

        m_render_target.reset();
        m_render_pass.reset();
        m_viewport_textures.clear();

        if(previous != context)
            ImGui::SetCurrentContext(previous);

        m_initialized = false;
    }

    bool ImGuiContext::begin_frame() {
        m_draw_data_ready = false;
        if(!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized, skipping begin_frame");
            return false;
        }

        if(m_is_recreating) {
            return false;
        }

        if(!m_window.get()) {
            LOG_WARN("Window is invalid, skipping ImGui frame");
            return false;
        }

        ImGui_ImplVulkan_NewFrame();

        if(m_window.is_minimized()) {
            ImGui::NewFrame();
        } else {
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        return true;
    }

    void ImGuiContext::end_frame() {
        ImGui::Render();
        m_draw_data_ready = true;
    }

    void ImGuiContext::render(Comet::CommandBuffer& command_buffer) const {
        if(!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized");
            return;
        }

        m_render_target->begin_render_target(command_buffer);

        ImDrawData* draw_data = ImGui::GetDrawData();
        if(m_draw_data_ready && draw_data && draw_data->CmdListsCount > 0) {
            ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer.get());
        }

        m_render_target->end_render_target(command_buffer);
    }

    void ImGuiContext::release_swapchain_resources() {
        if(!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized, cannot release swapchain resources");
            return;
        }
        LOG_INFO("Releasing ImGui swapchain resources");
        m_is_recreating = true;
        m_render_target.reset();
    }

    Comet::Result<void, Comet::GraphicsError> ImGuiContext::rebuild_swapchain_resources(
        const Comet::SwapchainCompatibility& compatibility) {
        if(!m_initialized) {
            return Comet::Result<void, Comet::GraphicsError>::failure(
                {"ImGuiContext is not initialized"});
        }
        if(!m_is_recreating) {
            return Comet::Result<void, Comet::GraphicsError>::failure(
                {"ImGui swapchain resources must be released before rebuilding"});
        }

        LOG_INFO("Rebuilding ImGui swapchain resources");
        auto& device = m_render_context.get_device();
        auto& swapchain = m_render_context.get_swapchain();
        const bool rebuild_backend = compatibility.format_changed
                                     || compatibility.image_count_changed
                                     || !ImGui::GetIO().BackendRendererUserData;
        if(rebuild_backend) {
            if(ImGui::GetIO().BackendRendererUserData) {
                unregister_viewport_textures();
                ImGui_ImplVulkan_Shutdown();
            }
            // Vulkan 后端关闭时也会清除主视口的平台数据。
            if(ImGui::GetIO().BackendPlatformUserData)
                ImGui_ImplGlfw_Shutdown();
            m_descriptor_pool.reset();
        }
        if(compatibility.format_changed) {
            m_render_pass.reset();
            if(auto result = create_render_pass(); !result)
                return result;
        }
        auto target =
            Comet::RenderTarget::create_swapchain_target(device, *m_render_pass, swapchain);
        if(!target)
            return Comet::Result<void, Comet::GraphicsError>::failure(target.error());
        m_render_target = std::move(target).value();
        m_render_target->set_clear_value(Comet::ClearValue(Comet::Math::Vec4(0.0f)), 0);
        if(rebuild_backend) {
            if(!ImGui_ImplGlfw_InitForVulkan(m_window.get(), true))
                return Comet::Result<void, Comet::GraphicsError>::failure(
                    {"Cannot reinitialize ImGui GLFW backend"});
            if(auto result = init_vulkan(); !result)
                return result;
            register_viewport_textures();
        }
        m_is_recreating = false;
        LOG_INFO("ImGui swapchain resources rebuilt successfully");
        return Comet::Result<void, Comet::GraphicsError>::success();
    }

    void ImGuiContext::set_viewport_image(const uint32_t frame_slot_index,
        std::shared_ptr<Comet::ImageView> image_view, std::shared_ptr<Comet::Sampler> sampler) {
        if(!image_view || !sampler) {
            LOG_FATAL("Viewport texture binding requires a valid image view and sampler");
        }
        if(frame_slot_index < m_viewport_textures.size() && m_viewport_textures[frame_slot_index]
            && m_viewport_textures[frame_slot_index]->matches(image_view, sampler)) {
            return;
        }

        if(frame_slot_index >= m_viewport_textures.size()) {
            m_viewport_textures.resize(frame_slot_index + 1);
        }
        auto binding = std::make_unique<TextureBinding>(std::move(image_view), std::move(sampler));
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

        for(const auto& texture : m_viewport_textures) {
            if(texture) {
                texture->register_texture();
            }
        }
    }

    void ImGuiContext::unregister_viewport_textures() {
        if(m_initialized) {
            for(const auto& texture : m_viewport_textures) {
                if(texture) {
                    texture->unregister_texture();
                }
            }
        }
    }
}

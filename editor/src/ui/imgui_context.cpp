#include "ui/imgui_context.h"
#include "common/scope_exit.h"
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
#include "render/overlay_record_context.h"
#include "diagnostics/logger.h"
#include "core/window.h"
#include "imgui_hdr_frag.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>

namespace CometEditor::Ui {
    namespace {
        class CurrentContext {
        public:
            explicit CurrentContext(::ImGuiContext* context)
                : m_previous(ImGui::GetCurrentContext()) {
                ImGui::SetCurrentContext(context);
            }
            ~CurrentContext() { ImGui::SetCurrentContext(m_previous); }

        private:
            ::ImGuiContext* m_previous;
        };

        void prepare_mouse_input(const Comet::Window& window) {
            auto& io = ImGui::GetIO();
            if(window.is_cursor_locked()) {
                io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
                io.ClearInputMouse();
                // NoMouse 仅禁用悬停；残留点击仍会改变焦点或触发 WantCaptureMouse。
                auto& events = ImGui::GetCurrentContext()->InputEventsQueue;
                for(int index = events.Size - 1; index >= 0; --index)
                    if(events[index].Source == ImGuiInputSource_Mouse)
                        events.erase(events.begin() + index);
            } else if(io.ConfigFlags & ImGuiConfigFlags_NoMouse) {
                io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
                // 恢复基线须先于已排队的点击，不能把位置事件追加到点击之后。
                const auto position = window.get_cursor_position();
                io.MousePos = {position.x, position.y};
            }
        }

        template<typename Handle> ImTextureID handle_to_texture_id(const Handle handle) {
            if constexpr(std::is_pointer_v<Handle>) {
                return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(handle));
            } else {
                return static_cast<ImTextureID>(handle);
            }
        }
    }

    struct ImGuiContext::ViewportImage {
        ImTextureID id;
        uint32_t frame_slot;
        std::shared_ptr<Comet::ImageView> image_view;
        std::shared_ptr<Comet::Sampler> sampler;
    };

    class ImGuiContext::TextureBinding final {
    public:
        TextureBinding(::ImGuiContext* context, const std::shared_ptr<const ViewportImage>& source)
            : m_context(context), m_source(source), m_image_view(source->image_view),
              m_sampler(source->sampler) {
            const CurrentContext current(m_context);
            m_descriptor_set = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(m_sampler->get()),
                static_cast<VkImageView>(m_image_view->get()),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        ~TextureBinding() { release(); }

        TextureBinding(const TextureBinding&) = delete;
        TextureBinding& operator=(const TextureBinding&) = delete;
        TextureBinding(TextureBinding&&) noexcept = delete;
        TextureBinding& operator=(TextureBinding&&) noexcept = delete;

        [[nodiscard]] bool matches(const std::shared_ptr<const ViewportImage>& source) const {
            return m_source.lock() == source;
        }
        [[nodiscard]] bool expired() const { return m_source.expired(); }

        void release() {
            if(m_descriptor_set == VK_NULL_HANDLE) {
                return;
            }

            const CurrentContext current(m_context);
            ImGui_ImplVulkan_RemoveTexture(m_descriptor_set);
            m_descriptor_set = VK_NULL_HANDLE;
        }

        [[nodiscard]] ImTextureID get_texture_id() const {
            return handle_to_texture_id(m_descriptor_set);
        }

    private:
        ::ImGuiContext* m_context;
        std::weak_ptr<const ViewportImage> m_source;
        std::shared_ptr<Comet::ImageView> m_image_view;
        std::shared_ptr<Comet::Sampler> m_sampler;
        VkDescriptorSet m_descriptor_set = VK_NULL_HANDLE;
    };

    struct ImGuiContext::FontPixels {
        int width = 0;
        int height = 0;
        ImTextureFormat format = ImTextureFormat_RGBA32;
        std::vector<unsigned char> pixels;
    };

    class ImGuiContext::FontTexture final {
    public:
        FontTexture(::ImGuiContext* context, std::shared_ptr<const FontPixels> source)
            : m_context(context), m_source(source) {
            const CurrentContext current(m_context);
            m_texture.Create(source->format, source->width, source->height);
            std::memcpy(m_texture.Pixels, source->pixels.data(), source->pixels.size());
            ImGui_ImplVulkan_UpdateTexture(&m_texture);
            m_texture.DestroyPixels();
        }
        ~FontTexture() { release(); }

        void release() {
            if(!m_texture.BackendUserData)
                return;
            const CurrentContext current(m_context);
            m_texture.WantDestroyNextFrame = true;
            m_texture.UnusedFrames = std::numeric_limits<int>::max();
            m_texture.SetStatus(ImTextureStatus_WantDestroy);
            ImGui_ImplVulkan_UpdateTexture(&m_texture);
        }

        [[nodiscard]] bool matches(const std::shared_ptr<const FontPixels>& source) const {
            return m_source.lock() == source;
        }
        [[nodiscard]] ImTextureID id() const { return m_texture.GetTexID(); }

    private:
        ::ImGuiContext* m_context;
        std::weak_ptr<const FontPixels> m_source;
        ImTextureData m_texture;
    };

    class ImGuiContext::Frame::Data {
    public:
        ~Data() {
            for(auto* list : draw.CmdLists)
                IM_DELETE(list);
        }

        ImDrawData draw;
        std::map<int, std::shared_ptr<const FontPixels>> fonts;
        std::vector<std::pair<ImDrawCmd*, int>> font_commands;
        std::map<ImTextureID, std::shared_ptr<const ViewportImage>> viewport_images;
        std::vector<std::pair<ImDrawCmd*, ImTextureID>> viewport_commands;
        std::uint64_t presentation_generation = 0;
        std::string error;
    };

    ImGuiContext::Frame::Frame(ImGuiContext& backend)
        : m_backend(backend), m_data(std::make_unique<Data>()) {}
    ImGuiContext::Frame::~Frame() = default;

    Comet::Result<void, Comet::GraphicsError> ImGuiContext::Frame::record(
        Comet::OverlayRecordContext& overlay) {
        if(!m_data)
            return Comet::Result<void, Comet::GraphicsError>::failure(
                {"ImGui frame already recorded"});
        const Comet::ScopeExit consumed([this] { m_data.reset(); });
        return m_backend.render(overlay, *this);
    }

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

    ImGuiContext::ImGuiContext(
        const Comet::Window& window, Comet::RenderContext& render_context, Options options)
        : m_window(window), m_render_context(render_context), m_options(std::move(options)),
          m_ini_path(m_options.ini_path.string()) {}

    Comet::Result<std::unique_ptr<ImGuiContext>, Comet::GraphicsError> ImGuiContext::create(
        const Comet::Window& window, Comet::RenderContext& render_context, Options options) {
        using CreationResult = Comet::Result<std::unique_ptr<ImGuiContext>, Comet::GraphicsError>;
        std::unique_ptr<ImGuiContext> context(
            new ImGuiContext(window, render_context, std::move(options)));
        auto initialized = context->initialize();
        if(!initialized)
            return CreationResult::failure(initialized.error());
        return CreationResult::success(std::move(context));
    }

    Comet::Result<void, Comet::GraphicsError> ImGuiContext::initialize() {
        LOG_INFO("Initializing ImGui layer");

        IMGUI_CHECKVERSION();
        m_frontend_context.reset(ImGui::CreateContext());
        ImGui::SetCurrentContext(m_frontend_context.get());
        ImGuiIO& io = ImGui::GetIO();
        // 前端把纹理请求写入 Frame，原生 Vulkan 后端不访问此上下文。
        io.BackendRendererName = "comet_imgui_frame";
        io.BackendFlags |=
            ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
        if(m_options.docking)
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
        io.IniFilename = m_ini_path.empty() ? nullptr : m_ini_path.c_str();

        auto font_directory = m_options.font_directory;
        if(font_directory.empty())
            font_directory = COMET_EDITOR_FONT_DIRECTORY;
        if(!io.Fonts->AddFontFromFileTTF(
               (font_directory / "Roboto-Bold.ttf").string().c_str(), 16.0f))
            return Comet::Result<void, Comet::GraphicsError>::failure(
                {"Cannot load ImGui Latin font"});
        ImFontConfig chinese_font;
        chinese_font.MergeMode = true;
        if(!io.Fonts->AddFontFromFileTTF(
               (font_directory / "NotoSansSC-Bold.otf").string().c_str(), 16.0f, &chinese_font))
            return Comet::Result<void, Comet::GraphicsError>::failure(
                {"Cannot load ImGui Chinese font"});

        if(auto icons = m_icons.load(*io.Fonts, COMET_EDITOR_ICON_DIRECTORY); !icons)
            return Comet::Result<void, Comet::GraphicsError>::failure({icons.error()});

        ImGui::StyleColorsDark();

        if(!ImGui_ImplGlfw_InitForVulkan(m_window.get(), true))
            return Comet::Result<void, Comet::GraphicsError>::failure(
                {"Cannot initialize ImGui GLFW backend"});

        m_backend_context.reset(ImGui::CreateContext());
        {
            const CurrentContext current(m_backend_context.get());
            ImGui::GetIO().IniFilename = nullptr;
        }

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
        if(m_options.composition == Composition::Preserve) {
            color_attachment.description.load_op = Comet::AttachmentLoadOp::Load;
            color_attachment.description.initial_layout = Comet::ImageLayout::PresentSrcKHR;
        }
        color_attachment.description.final_layout = Comet::ImageLayout::PresentSrcKHR;
        color_attachment.description.store_op = Comet::AttachmentStoreOp::Store;
        attachments.emplace_back(color_attachment);

        std::vector<Comet::RenderSubPass> render_sub_passes;
        Comet::RenderSubPass render_sub_pass = {
            {}, {Comet::SubpassColorAttachment(0)}, {}, Comet::SampleCount::Count1};
        render_sub_passes.emplace_back(render_sub_pass);

        auto& device = m_render_context.get_device();
        auto pass = Comet::RenderPass::create(device, attachments, render_sub_passes);
        if(!pass)
            return Comet::Result<void, Comet::GraphicsError>::failure(pass.error());
        m_render_pass = std::move(pass).value();
        return Comet::Result<void, Comet::GraphicsError>::success();
    }

    Comet::Result<void, Comet::GraphicsError> ImGuiContext::init_vulkan() {
        const CurrentContext current(m_backend_context.get());
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
        init_info.PipelineCache = device.get_pipeline_cache().get();
        init_info.QueueFamily = context.get_graphics_queue_family().queue_family_index.value();
        init_info.Queue = device.get_graphics_queue(0).get();
        init_info.DescriptorPool = m_descriptor_pool->get();
        init_info.MinImageCount = m_backend_image_count;
        init_info.ImageCount = m_backend_image_count;
        init_info.PipelineInfoMain.RenderPass = m_render_pass->get();
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        const auto surface_format = swapchain.get_active_generation()->get_config().surface_format;
        if(surface_format.colorSpace == vk::ColorSpaceKHR::eExtendedSrgbLinearEXT) {
            if(surface_format.format != vk::Format::eR16G16B16A16Sfloat)
                return Comet::Result<void, Comet::GraphicsError>::failure(
                    {"ImGui HDR output requires RGBA16F extended linear sRGB"});
            auto& shader = init_info.CustomShaderFragCreateInfo;
            shader.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            shader.codeSize = IMGUI_HDR_FRAG.size() * sizeof(std::uint32_t);
            shader.pCode = IMGUI_HDR_FRAG.data();
        }

        if(!ImGui_ImplVulkan_Init(&init_info))
            return Comet::Result<void, Comet::GraphicsError>::failure(
                {"Cannot initialize ImGui Vulkan backend"});
        return Comet::Result<void, Comet::GraphicsError>::success();
    }

    void ImGuiContext::shutdown_vulkan() noexcept {
        if(!m_backend_context)
            return;
        const CurrentContext current(m_backend_context.get());
        if(!ImGui::GetIO().BackendRendererUserData)
            return;
        for(const auto& version : m_viewport_versions)
            if(auto texture = version.lock())
                texture->release();
        m_viewport_textures.clear();
        m_viewport_versions.clear();
        for(const auto& version : m_font_versions)
            if(auto texture = version.lock())
                texture->release();
        m_font_textures.clear();
        m_font_versions.clear();
        ImGui_ImplVulkan_Shutdown();
    }

    ImGuiContext::~ImGuiContext() {
        cleanup();
    }

    void ImGuiContext::cleanup() noexcept {
        if(m_initialized) {
            m_render_context.get_device().wait_idle_for_shutdown();
        }
        shutdown_vulkan();
        m_backend_context.reset();
        m_font_pixels.clear();
        m_frontend_context.reset();

        m_descriptor_pool.reset();

        m_render_target.reset();
        m_render_pass.reset();
        m_viewport_images.clear();

        m_initialized = false;
    }

    bool ImGuiContext::begin_frame() {
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

        // 控件构建期间保持前端为当前上下文；录制会临时切换并恢复。
        ImGui::SetCurrentContext(m_frontend_context.get());
        if(!m_window.is_minimized())
            ImGui_ImplGlfw_NewFrame();
        prepare_mouse_input(m_window);
        ImGui::NewFrame();

        return true;
    }

    std::unique_ptr<ImGuiContext::Frame> ImGuiContext::end_frame() {
        ImGui::Render();
        auto frame = std::unique_ptr<Frame>(new Frame(*this));
        auto& captured = *frame->m_data;
        captured.presentation_generation = m_presentation_generation;
        const auto& source = *ImGui::GetDrawData();
        captured.draw.Valid = source.Valid;
        captured.draw.CmdListsCount = source.CmdListsCount;
        captured.draw.TotalVtxCount = source.TotalVtxCount;
        captured.draw.TotalIdxCount = source.TotalIdxCount;
        captured.draw.DisplayPos = source.DisplayPos;
        captured.draw.DisplaySize = source.DisplaySize;
        captured.draw.FramebufferScale = source.FramebufferScale;
        if(source.Textures) {
            for(auto* texture : *source.Textures) {
                if(texture->Status == ImTextureStatus_WantDestroy
                    || texture->Status == ImTextureStatus_Destroyed)
                    continue;
                auto& pixels = m_font_pixels[texture->UniqueID];
                if(!pixels || texture->Status == ImTextureStatus_WantCreate
                    || texture->Status == ImTextureStatus_WantUpdates) {
                    auto candidate = std::make_shared<FontPixels>();
                    candidate->width = texture->Width;
                    candidate->height = texture->Height;
                    candidate->format = texture->Format;
                    candidate->pixels.assign(
                        texture->Pixels, texture->Pixels + texture->GetSizeInBytes());
                    pixels = std::move(candidate);
                    // Main 只确认像素已交给帧；GPU 不回写前端纹理句柄。
                    texture->SetStatus(ImTextureStatus_OK);
                }
                captured.fonts.emplace(texture->UniqueID, pixels);
            }
        }
        std::erase_if(m_font_pixels,
            [&](const auto& entry) { return !captured.fonts.contains(entry.first); });
        std::set<ImTextureID> viewport_ids;
        for(const auto* list : source.CmdLists) {
            auto* copy = list->CloneOutput();
            captured.draw.CmdLists.push_back(copy);
            copy->_CallbacksDataBuf = list->_CallbacksDataBuf;
            for(auto& command : copy->CmdBuffer) {
                if(command.UserCallback
                    && command.UserCallback != ImDrawCallback_ResetRenderState) {
                    if(command.UserCallbackDataSize > 0)
                        command.UserCallbackData =
                            copy->_CallbacksDataBuf.Data + command.UserCallbackDataOffset;
                    else if(command.UserCallbackData)
                        captured.error = "ImGui draw callbacks must own their payload";
                }
                if(command.TexRef._TexData) {
                    captured.font_commands.emplace_back(
                        &command, command.TexRef._TexData->UniqueID);
                    command.TexRef = ImTextureRef{};
                } else {
                    viewport_ids.insert(command.GetTexID());
                    captured.viewport_commands.emplace_back(&command, command.GetTexID());
                }
            }
        }
        for(const auto& [view, images] : m_viewport_images)
            for(const auto& image : images)
                if(image && viewport_ids.contains(image->id))
                    captured.viewport_images.emplace(image->id, image);
        return frame;
    }

    Comet::Result<void, Comet::GraphicsError> ImGuiContext::render(
        Comet::OverlayRecordContext& overlay, const Frame& frame) {
        using RenderResult = Comet::Result<void, Comet::GraphicsError>;
        if(!m_initialized) {
            return RenderResult::failure({"ImGuiContext not initialized"});
        }
        const CurrentContext current(m_backend_context.get());
        const bool current_frame =
            frame.m_data->presentation_generation == m_presentation_generation;
        if(current_frame && !frame.m_data->error.empty())
            return RenderResult::failure({frame.m_data->error});
        const bool has_draw_data = current_frame && frame.m_data->draw.CmdListsCount > 0;
        if(m_options.composition == Composition::Preserve && !has_draw_data)
            return RenderResult::success();

        if(has_draw_data) {
            auto& captured = *frame.m_data;
            for(const auto& [id, image] : captured.viewport_images)
                if(image->frame_slot != overlay.frame_slot())
                    return RenderResult::failure(
                        {"ImGui viewport image belongs to another frame slot"});
            std::erase_if(
                m_viewport_textures, [](const auto& entry) { return entry.second->expired(); });
            for(const auto& [id, image] : captured.viewport_images) {
                auto& texture = m_viewport_textures[id];
                if(!texture || !texture->matches(image)) {
                    texture = std::make_shared<TextureBinding>(m_backend_context.get(), image);
                    std::erase_if(
                        m_viewport_versions, [](const auto& version) { return version.expired(); });
                    m_viewport_versions.push_back(texture);
                }
            }
            for(const auto& [command, id] : captured.viewport_commands)
                if(captured.viewport_images.contains(id))
                    command->TexRef = m_viewport_textures.at(id)->get_texture_id();
            std::erase_if(m_font_textures,
                [&](const auto& entry) { return !captured.fonts.contains(entry.first); });
            for(const auto& [id, pixels] : captured.fonts) {
                auto& texture = m_font_textures[id];
                if(!texture || !texture->matches(pixels)) {
                    texture = std::make_shared<FontTexture>(m_backend_context.get(), pixels);
                    std::erase_if(
                        m_font_versions, [](const auto& version) { return version.expired(); });
                    m_font_versions.push_back(texture);
                }
            }
            for(const auto& [command, id] : captured.font_commands) {
                const auto found = m_font_textures.find(id);
                if(found == m_font_textures.end())
                    return RenderResult::failure({"ImGui frame has no captured font texture"});
                command->TexRef = found->second->id();
            }
            captured.draw.OwnerViewport = ImGui::GetMainViewport();
        }

        auto& command_buffer = overlay.command_buffer();
        m_render_target->begin_render_target(command_buffer);
        if(has_draw_data) {
            ImGui_ImplVulkan_RenderDrawData(&frame.m_data->draw, command_buffer.get());
            for(const auto& [id, pixels] : frame.m_data->fonts)
                overlay.retain(m_font_textures.at(id));
            for(const auto& [id, image] : frame.m_data->viewport_images)
                overlay.retain(m_viewport_textures.at(id));
        }

        m_render_target->end_render_target(command_buffer);
        return RenderResult::success();
    }

    void ImGuiContext::release_swapchain_resources() {
        if(!m_initialized) {
            LOG_ERROR("ImGuiContext not initialized, cannot release swapchain resources");
            return;
        }
        LOG_INFO("Releasing ImGui swapchain resources");
        m_is_recreating = true;
        ++m_presentation_generation;
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
        const CurrentContext current(m_backend_context.get());
        auto& device = m_render_context.get_device();
        auto& swapchain = m_render_context.get_swapchain();
        const bool rebuild_backend = compatibility.format_changed
                                     || compatibility.image_count_changed
                                     || !ImGui::GetIO().BackendRendererUserData;
        if(rebuild_backend) {
            shutdown_vulkan();
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
            if(auto result = init_vulkan(); !result)
                return result;
        }
        m_is_recreating = false;
        LOG_INFO("ImGui swapchain resources rebuilt successfully");
        return Comet::Result<void, Comet::GraphicsError>::success();
    }

    void ImGuiContext::set_viewport_image(const uint32_t frame_slot_index,
        std::shared_ptr<Comet::ImageView> image_view, std::shared_ptr<Comet::Sampler> sampler,
        const uint32_t view_id) {
        if(!image_view || !sampler)
            LOG_FATAL("Viewport texture binding requires a valid image view and sampler");
        auto& images = m_viewport_images[view_id];
        if(frame_slot_index >= images.size())
            images.resize(frame_slot_index + 1);
        auto& image = images[frame_slot_index];
        if(image && image->image_view == image_view && image->sampler == sampler)
            return;
        const auto id = image ? image->id : m_next_viewport_texture_id++;
        image = std::make_shared<const ViewportImage>(
            ViewportImage{id, frame_slot_index, std::move(image_view), std::move(sampler)});
    }

    ImTextureID ImGuiContext::get_viewport_texture_id(
        const uint32_t frame_index, const uint32_t view_id) const {
        const auto found = m_viewport_images.find(view_id);
        if(found == m_viewport_images.end() || frame_index >= found->second.size())
            return ImTextureID_Invalid;
        const auto& image = found->second[frame_index];
        return image ? image->id : ImTextureID_Invalid;
    }
}

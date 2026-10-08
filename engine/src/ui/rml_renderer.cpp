#include "rml_renderer.h"
#include "graphics/frame_buffer.h"

#include "asset/data/texture_data.h"
#include "common/scope_exit.h"
#include "graphics/command/command_buffer.h"
#include "graphics/convert.h"
#include "graphics/device.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/render_pass.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/resource/sampler.h"
#include "graphics/swapchain.h"
#include "render/overlay_record_context.h"
#include "render/render_context.h"
#include "render/render_target.h"
#include "render/renderer.h"
#include "render/resource/render_resources.h"
#include "render/resource/sampled_image_binding.h"
#include "render/resource/texture.h"
#include "rml_ui_frag.h"
#include "rml_ui_vert.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_MAX_DIMENSIONS 4096
#include <stb_image.h>

#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace Comet::Ui {
    namespace {
        using Status = Comet::Result<void, Comet::GraphicsError>;
        constexpr std::size_t MAX_IMAGE_FILE_BYTES = 32 * 1024 * 1024;
        constexpr int MAX_TEXTURE_DIMENSION = 4096;
        constexpr std::size_t MAX_GEOMETRY_BYTES = 32 * 1024 * 1024;

        float srgb_to_linear(const float value) {
            if(value <= 0.04045f)
                return value / 12.92f;
            return std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        Comet::Result<bool, Comet::GraphicsError> encode_srgb(
            const Comet::Format format, const Comet::ImageColorSpace color_space) {
            using Encoding = Comet::Result<bool, Comet::GraphicsError>;
            if(color_space == Comet::ImageColorSpace::ExtendedSrgbLinearEXT) {
                if(format == Comet::Format::R16G16B16A16_SFLOAT)
                    return Encoding::success(false);
                return Encoding::failure(
                    {"RmlUi HDR output requires RGBA16F extended linear sRGB"});
            }
            if(color_space != Comet::ImageColorSpace::SrgbNonlinearKHR)
                return Encoding::failure({"Unsupported RmlUi output color space"});
            switch(format) {
                case Comet::Format::R8G8B8A8_SRGB:
                case Comet::Format::B8G8R8A8_SRGB:
                    return Encoding::success(false);
                case Comet::Format::R8G8B8A8_UNORM:
                case Comet::Format::B8G8R8A8_UNORM:
                    // 普通 UNORM 沿现有呈现编码域混合；严格线性混合需要统一合成目标。
                    return Encoding::success(true);
                default:
                    return Encoding::failure({"Unsupported RmlUi output format"});
            }
        }

        // 公共 ShaderInterface 要求 float 输入匹配 32 位顶点格式。
        struct GpuVertex {
            glm::vec2 position;
            glm::vec4 colour;
            glm::vec2 tex_coord;
        };

        struct GeometryParameters {
            glm::mat4 transform;
            glm::vec2 translation;
            glm::vec2 dimensions;
        };
        static_assert(sizeof(GeometryParameters) == 80);
        static_assert(sizeof(int) == sizeof(std::uint32_t));
        static_assert(sizeof(Rml::ColourbPremultiplied) == 4);
    }

    namespace {
        class ViewTarget final: public Comet::RenderTarget {
        public:
            ViewTarget(Comet::Device& device, std::shared_ptr<Comet::RenderPass> pass,
                std::shared_ptr<Comet::ImageView> view, std::shared_ptr<Comet::FrameBuffer> buffer,
                Comet::Math::Vec2u size)
                : RenderTarget(device, *pass, size, 1), m_pass(std::move(pass)),
                  m_view(std::move(view)), m_buffer(std::move(buffer)) {}

            std::shared_ptr<Comet::FrameBuffer> get_framebuffer(uint32_t) const override {
                return m_buffer;
            }
            std::shared_ptr<Comet::ImageView> get_color_view(uint32_t) const override {
                return m_view;
            }

        private:
            std::shared_ptr<Comet::RenderPass> m_pass;
            std::shared_ptr<Comet::ImageView> m_view;
            std::shared_ptr<Comet::FrameBuffer> m_buffer;
        };
    }

    class RmlRenderer::Impl final: public Rml::RenderInterface {
    public:
        explicit Impl(Comet::Renderer& renderer)
            : m_renderer(renderer), m_device(m_renderer.get_render_context().get_device()),
              m_resources(m_renderer.get_render_resources()) {}

        Status initialize() {
            const auto support = m_device.query_format_support(Comet::Format::R16G16B16A16_SFLOAT);
            if(!support.sampled || !support.linear_filter)
                return Status::failure({"RmlUi requires linearly filtered RGBA16F textures"});
            Comet::DescriptorSetLayoutBindings bindings;
            bindings.add_binding(0, Comet::DescriptorType::CombinedImageSampler,
                Comet::Flags<Comet::ShaderStage>(Comet::ShaderStage::Fragment));
            auto created_layout = Comet::DescriptorSetLayout::create(m_device, bindings);
            if(!created_layout)
                return Status::failure(created_layout.error());
            m_layout = std::move(created_layout).value();
            auto created_sampler = Comet::Sampler::create(
                m_device, {.address_mode_u = Comet::SamplerAddressMode::ClampToEdge,
                              .address_mode_v = Comet::SamplerAddressMode::ClampToEdge,
                              .address_mode_w = Comet::SamplerAddressMode::ClampToEdge});
            if(!created_sampler)
                return Status::failure(created_sampler.error());
            m_sampler = std::move(created_sampler).value();
            auto vertex_shader = Comet::Shader::create(m_device, "rml_ui_vertex", RML_UI_VERT);
            if(!vertex_shader)
                return Status::failure(vertex_shader.error());
            m_vertex = std::move(vertex_shader).value();
            auto fragment_shader = Comet::Shader::create(m_device, "rml_ui_fragment", RML_UI_FRAG);
            if(!fragment_shader)
                return Status::failure(fragment_shader.error());
            m_fragment = std::move(fragment_shader).value();
            const std::array<Rml::byte, 4> white_pixels{255, 255, 255, 255};
            auto created_white = create_texture({white_pixels.data(), white_pixels.size()}, {1, 1});
            if(!created_white)
                return Status::failure(created_white.error());
            m_white = std::move(created_white).value();
            return rebuild_swapchain_resources();
        }

        Rml::CompiledGeometryHandle CompileGeometry(const Rml::Span<const Rml::Vertex> vertices,
            const Rml::Span<const int> indices) override {
            if(vertices.empty() || indices.empty()
                || vertices.size() > MAX_GEOMETRY_BYTES / sizeof(GpuVertex)
                || indices.size() > MAX_GEOMETRY_BYTES / sizeof(int)
                || indices.size() > std::numeric_limits<std::uint32_t>::max()) {
                fail({"Invalid or oversized RmlUi geometry"});
                return 0;
            }
            for(const auto index : indices) {
                if(index < 0 || static_cast<std::size_t>(index) >= vertices.size()) {
                    fail({"RmlUi geometry contains an out-of-range index"});
                    return 0;
                }
            }
            std::vector<GpuVertex> converted;
            converted.reserve(vertices.size());
            for(const auto& value : vertices) {
                if(!std::isfinite(value.position.x) || !std::isfinite(value.position.y)
                    || !std::isfinite(value.tex_coord.x) || !std::isfinite(value.tex_coord.y)) {
                    fail({"RmlUi geometry contains non-finite coordinates"});
                    return 0;
                }
                converted.push_back({{value.position.x, value.position.y},
                    {value.colour.red / 255.f, value.colour.green / 255.f,
                        value.colour.blue / 255.f, value.colour.alpha / 255.f},
                    {value.tex_coord.x, value.tex_coord.y}});
            }
            auto vertex_buffer = Comet::Buffer::try_create_upload_buffer(m_device,
                Comet::Flags<Comet::BufferUsage>(Comet::BufferUsage::Vertex),
                converted.size() * sizeof(GpuVertex), true, converted.data(), "RmlUi vertices");
            if(!vertex_buffer) {
                fail(vertex_buffer.error());
                return 0;
            }
            auto index_buffer = Comet::Buffer::try_create_upload_buffer(m_device,
                Comet::Flags<Comet::BufferUsage>(Comet::BufferUsage::Index),
                indices.size() * sizeof(int), true, indices.data(), "RmlUi indices");
            if(!index_buffer) {
                fail(index_buffer.error());
                return 0;
            }
            const auto handle = m_next_geometry++;
            m_geometries.emplace(handle,
                std::make_shared<Geometry>(Geometry{std::move(vertex_buffer).value(),
                    std::move(index_buffer).value(), static_cast<std::uint32_t>(indices.size())}));
            return handle;
        }

        void RenderGeometry(const Rml::CompiledGeometryHandle handle,
            const Rml::Vector2f translation, const Rml::TextureHandle texture_handle) override {
            if(m_error)
                return;
            if(!m_validating && (!m_recording || !m_active_pipeline)) {
                fail({"RmlUi geometry was drawn outside an overlay recording"});
                return;
            }
            const auto found = m_geometries.find(handle);
            if(found == m_geometries.end()) {
                fail({"RmlUi referenced an unknown geometry"});
                return;
            }
            auto texture = m_white;
            if(texture_handle != 0) {
                const auto found_texture = m_textures.find(texture_handle);
                if(found_texture == m_textures.end()) {
                    fail({"RmlUi referenced an unknown texture"});
                    return;
                }
                texture = found_texture->second;
            }
            if(!std::isfinite(translation.x) || !std::isfinite(translation.y)) {
                fail({"RmlUi geometry translation is not finite"});
                return;
            }
            if(m_validating)
                return;
            const auto bounds = scissor();
            if(bounds.extent.width == 0 || bounds.extent.height == 0)
                return;
            const auto& geometry = found->second;
            m_recording->retain(geometry);
            m_recording->retain(texture);
            m_recording->wait_for(texture->texture->get_ready_completion(),
                Comet::Flags<Comet::PipelineStage>(Comet::PipelineStage::FragmentShader));
            auto& command = m_recording->command_buffer();
            command.set_scissor(bounds);
            command.bind_descriptor_sets(*m_active_pipeline->pipeline->get_layout(),
                std::span(&texture->binding->descriptor, 1));
            command.bind_vertex_buffer({*geometry->vertices});
            command.bind_index_buffer(*geometry->indices, 0);
            const GeometryParameters parameters{m_transform, {translation.x, translation.y},
                {float(m_dimensions.x), float(m_dimensions.y)}};
            command.push_constants(*m_active_pipeline->pipeline->get_layout(),
                Comet::Flags<Comet::ShaderStage>(Comet::ShaderStage::Vertex), 0, &parameters,
                sizeof(parameters));
            command.draw_indexed(geometry->index_count);
        }

        void ReleaseGeometry(const Rml::CompiledGeometryHandle geometry) override {
            m_geometries.erase(geometry);
        }

        Rml::TextureHandle LoadTexture(
            Rml::Vector2i& texture_dimensions, const Rml::String& source) override {
            auto* files = Rml::GetFileInterface();
            if(!files) {
                fail({"RmlUi has no file interface"});
                return 0;
            }
            const auto file = files->Open(source);
            if(!file) {
                fail({"Cannot open RmlUi texture: " + source});
                return 0;
            }
            const Comet::ScopeExit close_file([&] { files->Close(file); });
            const auto length = files->Length(file);
            if(length == 0 || length > MAX_IMAGE_FILE_BYTES) {
                fail({"RmlUi image file is empty or exceeds the 32 MiB budget: " + source});
                return 0;
            }
            std::vector<Rml::byte> bytes(length);
            if(files->Read(bytes.data(), length, file) != length) {
                fail({"Cannot read RmlUi texture: " + source});
                return 0;
            }
            int width = 0, height = 0, channels = 0;
            if(!stbi_info_from_memory(
                   bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels)
                || width <= 0 || height <= 0 || width > MAX_TEXTURE_DIMENSION
                || height > MAX_TEXTURE_DIMENSION) {
                fail({"Invalid or oversized RmlUi image: " + source});
                return 0;
            }
            std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
                stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                    &channels, STBI_rgb_alpha),
                stbi_image_free);
            if(!pixels) {
                fail({"Cannot decode RmlUi texture: " + source});
                return 0;
            }
            auto candidate =
                create_texture({pixels.get(), std::size_t(width) * std::size_t(height) * 4},
                    {width, height}, false);
            if(!candidate) {
                fail(candidate.error());
                return 0;
            }
            const auto handle = m_next_texture++;
            m_textures.emplace(handle, std::move(candidate).value());
            texture_dimensions = {width, height};
            return handle;
        }

        Rml::TextureHandle GenerateTexture(
            const Rml::Span<const Rml::byte> source, const Rml::Vector2i size) override {
            auto candidate = create_texture(source, size);
            if(!candidate) {
                fail(candidate.error());
                return 0;
            }
            const auto handle = m_next_texture++;
            m_textures.emplace(handle, std::move(candidate).value());
            return handle;
        }

        void ReleaseTexture(const Rml::TextureHandle texture) override {
            m_textures.erase(texture);
        }

        void EnableScissorRegion(const bool enable) override { m_scissor_enabled = enable; }

        void SetScissorRegion(const Rml::Rectanglei region) override { m_scissor_region = region; }

        void SetTransform(const Rml::Matrix4f* matrix) override {
            m_transform = glm::mat4(1);
            if(!matrix)
                return;
            // 显式按列读取，兼容 RmlUi 的矩阵存储配置。
            for(int column = 0; column < 4; ++column) {
                const auto source = matrix->GetColumn(column);
                for(int row = 0; row < 4; ++row) {
                    if(!std::isfinite(source[row])) {
                        fail({"RmlUi transform contains non-finite values"});
                        return;
                    }
                    m_transform[column][row] = source[row];
                }
            }
        }

        void EnableClipMask(const bool enable) override {
            if(enable)
                fail({"RmlUi clip masks are not supported by the basic renderer"});
        }

        void RenderToClipMask(
            Rml::ClipMaskOperation, Rml::CompiledGeometryHandle, Rml::Vector2f) override {
            fail({"RmlUi clip masks are not supported by the basic renderer"});
        }

        Rml::LayerHandle PushLayer() override {
            fail({"RmlUi layers and filters are not supported by the basic renderer"});
            return 0;
        }

        void CompositeLayers(Rml::LayerHandle, Rml::LayerHandle, Rml::BlendMode,
            Rml::Span<const Rml::CompiledFilterHandle>) override {
            fail({"RmlUi layer compositing is not supported by the basic renderer"});
        }

        Rml::CompiledFilterHandle CompileFilter(
            const Rml::String&, const Rml::Dictionary&) override {
            fail({"RmlUi filters are not supported by the basic renderer"});
            return 0;
        }

        Rml::CompiledShaderHandle CompileShader(
            const Rml::String&, const Rml::Dictionary&) override {
            fail({"RmlUi custom shaders are not supported by the basic renderer"});
            return 0;
        }

        void PopLayer() override { fail({"RmlUi layers are not supported by the basic renderer"}); }

        Rml::TextureHandle SaveLayerAsTexture() override {
            fail({"RmlUi layers are not supported by the basic renderer"});
            return 0;
        }

        Rml::CompiledFilterHandle SaveLayerAsMaskImage() override {
            fail({"RmlUi mask images are not supported by the basic renderer"});
            return 0;
        }

        void RenderShader(Rml::CompiledShaderHandle, Rml::CompiledGeometryHandle, Rml::Vector2f,
            Rml::TextureHandle) override {
            fail({"RmlUi custom shaders are not supported by the basic renderer"});
        }

        std::optional<Comet::GraphicsError> take_error() {
            return std::exchange(m_error, std::nullopt);
        }

        void release_swapchain_resources() {
            m_swapchain_target = {};
            m_offscreen_targets.clear();
            m_pipeline_cache.clear();
        }

        Status rebuild_swapchain_resources() {
            auto& swapchain = m_renderer.get_render_context().get_swapchain();
            const auto surface = swapchain.get_active_generation()->get_config().surface_format;
            const auto format = Comet::Graphics::vk_to_format(surface.format);
            const auto color_space = Comet::Graphics::vk_to_image_color_space(surface.colorSpace);
            const auto encoding = encode_srgb(format, color_space);
            if(!encoding)
                return Status::failure(encoding.error());
            auto color = Comet::Attachment::get_color_attachment(format);
            color.description.load_op = Comet::AttachmentLoadOp::Load;
            color.description.store_op = Comet::AttachmentStoreOp::Store;
            color.description.initial_layout = Comet::ImageLayout::PresentSrcKHR;
            color.description.final_layout = Comet::ImageLayout::PresentSrcKHR;
            auto created_pass = Comet::RenderPass::create(m_device, {color},
                {{.color_attachments = {Comet::SubpassColorAttachment(0)}}}, format);
            if(!created_pass)
                return Status::failure(created_pass.error());
            std::shared_ptr<Comet::RenderPass> pass = std::move(created_pass).value();
            auto created_target =
                Comet::RenderTarget::create_swapchain_target(m_device, *pass, swapchain);
            if(!created_target)
                return Status::failure(created_target.error());
            Target candidate{pass, std::move(created_target).value(), format, color_space};
            auto created_pipeline = pipeline_for(candidate);
            if(!created_pipeline)
                return Status::failure(created_pipeline.error());
            m_swapchain_target = std::move(candidate);
            return Status::success();
        }

        Status validate(Rml::Context& context) {
            if(auto pending = take_error())
                return Status::failure(std::move(*pending));
            if(m_recording || m_validating)
                return Status::failure({"RmlUi cannot nest document validation or recording"});
            const auto size = context.GetDimensions();
            if(size.x <= 0 || size.y <= 0)
                return Status::failure({"Invalid RmlUi context dimensions"});
            m_validating = true;
            m_transform = glm::mat4(1);
            m_scissor_enabled = false;
            const Comet::ScopeExit end_validation([&] { m_validating = false; });
            if(!context.Render() && !m_error)
                fail({"RmlUi context validation failed"});
            if(auto pending = take_error())
                return Status::failure(std::move(*pending));
            return Status::success();
        }

        Status render(Comet::OverlayRecordContext& frame, Rml::Context& context,
            const Target& target, const bool swapchain, const uint32_t target_index) {
            if(auto pending = take_error())
                return Status::failure(std::move(*pending));
            if(m_recording || m_validating)
                return Status::failure({"RmlUi cannot nest overlay recordings"});
            if(!target.pass || !target.target)
                return Status::failure({"RmlUi render target is unavailable"});
            const auto view = target.target->get_color_view(0);
            if(!view || &view->get_image()->get_device() != &m_device
                || view->get_image()->get_info().format != target.format)
                return Status::failure(
                    {"RmlUi render target belongs to a different device or format"});
            const auto size = target.target->get_size();
            const auto context_size = context.GetDimensions();
            if(size.x == 0 || size.y == 0 || context_size.x <= 0 || context_size.y <= 0
                || size.x > std::uint32_t(std::numeric_limits<int>::max())
                || size.y > std::uint32_t(std::numeric_limits<int>::max()))
                return Status::failure({"Invalid RmlUi render target or context dimensions"});
            auto candidate = pipeline_for(target);
            if(!candidate)
                return Status::failure(candidate.error());
            m_active_pipeline = std::move(candidate).value();
            frame.retain(m_active_pipeline);
            frame.retain(target.target);
            m_recording = &frame;
            m_dimensions = {context_size.x, context_size.y};
            m_framebuffer_size = size;
            m_transform = glm::mat4(1);
            m_scissor_enabled = false;
            const Comet::ScopeExit clear_recording([&] {
                m_recording = nullptr;
                m_active_pipeline.reset();
            });
            auto& command = frame.command_buffer();
            if(swapchain)
                target.target->begin_render_target(command);
            else
                target.target->begin_render_target(command, target_index);
            const Comet::ScopeExit end_pass([&] { target.target->end_render_target(command); });
            command.set_viewport(vk::Viewport(0, 0, float(size.x), float(size.y), 0, 1));
            command.bind_pipeline(*m_active_pipeline->pipeline);
            const std::uint32_t encoding = m_active_pipeline->encode_srgb ? 1 : 0;
            command.push_constants(*m_active_pipeline->pipeline->get_layout(),
                Comet::Flags<Comet::ShaderStage>(Comet::ShaderStage::Fragment), 80, &encoding,
                sizeof(encoding));
            if(!context.Render() && !m_error)
                fail({"RmlUi context rendering failed"});
            if(auto pending = take_error())
                return Status::failure(std::move(*pending));
            return Status::success();
        }

        Target m_swapchain_target;

        Status render_offscreen(Comet::OverlayRecordContext& frame, Rml::Context& context) {
            const auto output = m_renderer.get_offscreen_frame();
            if(!output.color_view || output.size.x == 0 || output.size.y == 0)
                return Status::failure({"RmlUi scene output is unavailable"});
            if(output.slot != frame.frame_slot())
                return Status::failure({"RmlUi scene output does not match the active frame"});
            const auto format = output.color_view->get_image()->get_info().format;
            if(m_offscreen_targets.size() <= output.slot)
                m_offscreen_targets.resize(output.slot + 1);
            auto& target = m_offscreen_targets[output.slot];
            if(!target.target || target.target->get_color_view(0) != output.color_view) {
                std::shared_ptr<Comet::RenderPass> pass;
                for(const auto& cached : m_offscreen_targets)
                    if(cached.pass && cached.format == format) {
                        pass = cached.pass;
                        break;
                    }
                if(!pass) {
                    auto color = Comet::Attachment::get_color_attachment(format);
                    color.description.load_op = Comet::AttachmentLoadOp::Load;
                    color.description.store_op = Comet::AttachmentStoreOp::Store;
                    color.description.initial_layout = Comet::ImageLayout::ColorAttachmentOptimal;
                    color.description.final_layout = Comet::ImageLayout::ShaderReadOnlyOptimal;
                    auto created = Comet::RenderPass::create(m_device, {color},
                        {{.color_attachments = {Comet::SubpassColorAttachment(0)}}}, format);
                    if(!created)
                        return Status::failure(created.error());
                    pass = std::move(created).value();
                }
                auto buffer = Comet::FrameBuffer::try_create(
                    m_device, *pass, {output.color_view}, output.size.x, output.size.y);
                if(!buffer)
                    return Status::failure(buffer.error());
                auto color_space = Comet::ImageColorSpace::SrgbNonlinearKHR;
                if(format == Comet::Format::R16G16B16A16_SFLOAT)
                    color_space = Comet::ImageColorSpace::ExtendedSrgbLinearEXT;
                target = {pass,
                    std::make_shared<ViewTarget>(
                        m_device, pass, output.color_view, std::move(buffer).value(), output.size),
                    format, color_space};
            }
            vk::ImageMemoryBarrier2 barrier;
            barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput
                                   | vk::PipelineStageFlagBits2::eFragmentShader;
            barrier.srcAccessMask =
                vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eShaderRead;
            barrier.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentRead
                                    | vk::AccessFlagBits2::eColorAttachmentWrite;
            barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = output.color_view->get_image()->get();
            barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            frame.command_buffer().get().pipelineBarrier2(
                vk::DependencyInfo{}.setImageMemoryBarriers(barrier));
            return render(frame, context, target, false, 0);
        }

    private:
        std::vector<Target> m_offscreen_targets;
        struct Geometry {
            std::shared_ptr<Comet::CPUBuffer> vertices;
            std::shared_ptr<Comet::CPUBuffer> indices;
            std::uint32_t index_count;
        };
        struct Texture {
            std::shared_ptr<Comet::Texture> texture;
            std::shared_ptr<Comet::SampledImageBinding> binding;
        };
        struct Pipeline {
            std::shared_ptr<Comet::RenderPass> pass;
            std::shared_ptr<Comet::Pipeline> pipeline;
            Comet::Format format;
            Comet::ImageColorSpace color_space;
            bool encode_srgb;
            std::weak_ptr<Comet::RenderTarget> target;
        };

        void fail(Comet::GraphicsError failure) {
            if(!m_error)
                m_error = std::move(failure);
        }

        Comet::Result<std::shared_ptr<Texture>, Comet::GraphicsError> create_texture(
            const Rml::Span<const Rml::byte> source, const Rml::Vector2i size,
            const bool premultiplied = true) {
            using Creation = Comet::Result<std::shared_ptr<Texture>, Comet::GraphicsError>;
            if(size.x <= 0 || size.y <= 0 || size.x > MAX_TEXTURE_DIMENSION
                || size.y > MAX_TEXTURE_DIMENSION
                || source.size() != std::size_t(size.x) * std::size_t(size.y) * 4)
                return Creation::failure({"Invalid or oversized RmlUi texture data"});
            Comet::TextureData data{
                .width = size.x, .height = size.y, .format = Comet::Format::R16G16B16A16_SFLOAT};
            data.pixels.resize(source.size() * 2);
            for(std::size_t pixel = 0; pixel < source.size() / 4; ++pixel) {
                const float alpha = float(source[pixel * 4 + 3]) / 255;
                for(std::size_t channel = 0; channel < 4; ++channel) {
                    float value = alpha;
                    if(channel < 3) {
                        float straight = float(source[pixel * 4 + channel]) / 255;
                        if(premultiplied)
                            straight = alpha > 0 ? std::min(straight / alpha, 1.f) : 0;
                        value = srgb_to_linear(straight) * alpha;
                    }
                    const auto half = glm::packHalf1x16(value);
                    std::memcpy(
                        data.pixels.data() + (pixel * 4 + channel) * 2, &half, sizeof(half));
                }
            }
            auto created = m_resources.try_create_texture(data);
            if(!created)
                return Creation::failure(created.error());
            auto texture = std::move(created).value();
            auto binding = Comet::SampledImageBinding::create(
                m_device, {texture->get_image_view()}, m_layout, m_sampler);
            if(!binding)
                return Creation::failure(binding.error());
            return Creation::success(
                std::make_shared<Texture>(Texture{std::move(texture), std::move(binding).value()}));
        }

        Comet::Result<std::shared_ptr<Pipeline>, Comet::GraphicsError> pipeline_for(
            const Target& target) {
            using Creation = Comet::Result<std::shared_ptr<Pipeline>, Comet::GraphicsError>;
            if(!target.pass || target.pass->get_subpass_count() != 1
                || target.pass->get_color_attachment_count(0) != 1
                || target.pass->get_attachments().size() != 1
                || target.pass->get_attachments()[0].description.format != target.format
                || target.pass->get_attachments()[0].description.samples
                       != Comet::SampleCount::Count1)
                return Creation::failure({"RmlUi requires a matching single-sample color target"});
            const auto encoding = encode_srgb(target.format, target.color_space);
            if(!encoding)
                return Creation::failure(encoding.error());
            std::erase_if(
                m_pipeline_cache, [](const auto& entry) { return entry.second->target.expired(); });
            if(const auto found = m_pipeline_cache.find(target.pass.get());
                found != m_pipeline_cache.end()) {
                if(found->second->format != target.format
                    || found->second->color_space != target.color_space)
                    return Creation::failure(
                        {"RmlUi target color contract changed without rebuilding"});
                found->second->target = target.target;
                return Creation::success(found->second);
            }
            Comet::ShaderLayout shader_layout;
            shader_layout.descriptor_set_layouts = {m_layout};
            shader_layout.push_constants = {
                std::make_shared<Comet::PushConstantRange>(Comet::ShaderStage::Vertex, 0, 80),
                std::make_shared<Comet::PushConstantRange>(Comet::ShaderStage::Fragment, 80, 4)};
            Comet::PipelineConfig config;
            config.vertex_input_state.vertex_bindings = {
                {0, sizeof(GpuVertex), vk::VertexInputRate::eVertex}};
            config.vertex_input_state.vertex_attributes = {
                {0, 0, vk::Format::eR32G32Sfloat, offsetof(GpuVertex, position)},
                {1, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(GpuVertex, colour)},
                {2, 0, vk::Format::eR32G32Sfloat, offsetof(GpuVertex, tex_coord)}};
            config.set_dynamic_state({Comet::DynamicState::Viewport, Comet::DynamicState::Scissor});
            config.set_color_blend_attachment_state({.blend_enable = true,
                .src_color_blend_factor = Comet::BlendFactor::One,
                .dst_color_blend_factor = Comet::BlendFactor::OneMinusSrcAlpha,
                .src_alpha_blend_factor = Comet::BlendFactor::One,
                .dst_alpha_blend_factor = Comet::BlendFactor::OneMinusSrcAlpha});
            Comet::PipelineManager manager(m_device, *target.pass);
            auto created =
                manager.create_pipeline("rml_ui", shader_layout, config, m_vertex, m_fragment);
            if(!created)
                return Creation::failure(created.error());
            auto result =
                std::make_shared<Pipeline>(Pipeline{target.pass, std::move(created).value(),
                    target.format, target.color_space, encoding.value(), target.target});
            m_pipeline_cache.emplace(target.pass.get(), result);
            return Creation::success(std::move(result));
        }

        vk::Rect2D scissor() const {
            if(!m_scissor_enabled)
                return {{0, 0}, {m_framebuffer_size.x, m_framebuffer_size.y}};
            const double scale_x = double(m_framebuffer_size.x) / m_dimensions.x;
            const double scale_y = double(m_framebuffer_size.y) / m_dimensions.y;
            const auto left = std::clamp(
                std::floor(m_scissor_region.Left() * scale_x), 0., double(m_framebuffer_size.x));
            const auto top = std::clamp(
                std::floor(m_scissor_region.Top() * scale_y), 0., double(m_framebuffer_size.y));
            const auto right = std::clamp(
                std::ceil(m_scissor_region.Right() * scale_x), left, double(m_framebuffer_size.x));
            const auto bottom = std::clamp(
                std::ceil(m_scissor_region.Bottom() * scale_y), top, double(m_framebuffer_size.y));
            return {{static_cast<std::int32_t>(left), static_cast<std::int32_t>(top)},
                {static_cast<std::uint32_t>(right - left),
                    static_cast<std::uint32_t>(bottom - top)}};
        }

        Comet::Renderer& m_renderer;
        Comet::Device& m_device;
        Comet::RenderResources& m_resources;
        std::shared_ptr<Comet::DescriptorSetLayout> m_layout;
        std::shared_ptr<Comet::Sampler> m_sampler;
        std::shared_ptr<Comet::Shader> m_vertex;
        std::shared_ptr<Comet::Shader> m_fragment;
        std::shared_ptr<Texture> m_white;
        std::map<Rml::CompiledGeometryHandle, std::shared_ptr<Geometry>> m_geometries;
        std::map<Rml::TextureHandle, std::shared_ptr<Texture>> m_textures;
        std::map<Comet::RenderPass*, std::shared_ptr<Pipeline>> m_pipeline_cache;
        Rml::CompiledGeometryHandle m_next_geometry = 1;
        Rml::TextureHandle m_next_texture = 1;
        std::optional<Comet::GraphicsError> m_error;
        Comet::OverlayRecordContext* m_recording = nullptr;
        std::shared_ptr<Pipeline> m_active_pipeline;
        glm::mat4 m_transform{1};
        Comet::Math::Vec2u m_framebuffer_size{};
        Rml::Vector2i m_dimensions{};
        Rml::Rectanglei m_scissor_region;
        bool m_scissor_enabled = false;
        bool m_validating = false;
    };

    Comet::Result<std::unique_ptr<RmlRenderer>, Comet::GraphicsError> RmlRenderer::create(
        Comet::Renderer& renderer) {
        using Creation = Comet::Result<std::unique_ptr<RmlRenderer>, Comet::GraphicsError>;
        auto impl = std::make_unique<Impl>(renderer);
        if(auto initialized = impl->initialize(); !initialized)
            return Creation::failure(initialized.error());
        return Creation::success(std::unique_ptr<RmlRenderer>(new RmlRenderer(std::move(impl))));
    }

    RmlRenderer::RmlRenderer(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
    RmlRenderer::~RmlRenderer() = default;
    Rml::RenderInterface& RmlRenderer::interface() {
        return *m_impl;
    }

    Status RmlRenderer::validate(Rml::Context& context) {
        return m_impl->validate(context);
    }

    Status RmlRenderer::render(Comet::OverlayRecordContext& frame, Rml::Context& context) {
        return m_impl->render(frame, context, m_impl->m_swapchain_target, true, 0);
    }

    Status RmlRenderer::render_offscreen(
        Comet::OverlayRecordContext& frame, Rml::Context& context) {
        return m_impl->render_offscreen(frame, context);
    }

    Status RmlRenderer::render_to_target(
        Comet::OverlayRecordContext& frame, Rml::Context& context, const Target& target) {
        return m_impl->render(frame, context, target, false, frame.frame_slot());
    }

    std::optional<Comet::GraphicsError> RmlRenderer::take_error() {
        return m_impl->take_error();
    }

    void RmlRenderer::release_swapchain_resources() {
        m_impl->release_swapchain_resources();
    }

    Status RmlRenderer::rebuild_swapchain_resources(const Comet::SwapchainCompatibility&) {
        return m_impl->rebuild_swapchain_resources();
    }
}

#include "render/scene/scene_preview.h"
#include "common/scope_exit.h"
#include "diagnostics/logger.h"
#include "graphics/device.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "render/frame_scheduler.h"
#include "render/material/material_programs.h"
#include "render/passes/skinning_pass.h"
#include "render/render_context.h"
#include "render/render_graph.h"
#include "render/renderer.h"
#include "render/scene/scene_renderer.h"
#include "render/scene/scene_resolver.h"

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>

namespace Comet {
    struct ScenePreview::State {
        MaterialPrograms programs;
        std::unique_ptr<SceneRenderer> scene;
        FrameScheduler frames;
        std::unique_ptr<SkinningPass> skinning;
        RenderGeometry geometry;
        std::shared_ptr<ReadbackBuffer> readback;
        GpuCompletionPoint completion;
        std::shared_ptr<const RenderWorld> world;
        std::vector<RenderCamera> cameras;
        std::size_t face = 0;
        Output output = Output::Display;
        TextureData pixels;

        Math::Vec2u size;
        State(Device& device, Math::Vec2u dimensions) : frames(device, 1), size(dimensions) {
            frames.initialize_swapchain_images(1);
        }
        ~State() {
            if(completion.is_valid())
                frames.wait_for_all_slots();
        }

        Result<void, GraphicsError> begin(Renderer& renderer,
            std::shared_ptr<const RenderWorld> captured_world, std::vector<RenderCamera> views,
            Output captured_output, bool cubemap) {
            auto& device = renderer.get_render_context().get_device();
            if(programs.feedback()->builtin_revision
                != renderer.m_programs->feedback()->builtin_revision)
                scene.reset();
            // 共用宿主当前 Shader 语义；预览的发布和失败反馈留在自己的程序快照中。
            programs = *renderer.m_programs;
            world = std::move(captured_world);
            cameras = std::move(views);
            face = 0;
            output = captured_output;
            pixels = {.width = int(size.x),
                .height = int(size.y),
                .format = output == Output::LinearHdr ? Format::R16G16B16A16_SFLOAT
                                                      : Format::R8G8B8A8_SRGB,
                .cubemap = cubemap};
            const std::size_t face_bytes =
                std::size_t(size.x) * size.y * (output == Output::LinearHdr ? 8 : 4);
            pixels.pixels.resize(face_bytes * cameras.size());
            VulkanSettings vulkan;
            vulkan.surface_format = Format::R8G8B8A8_SRGB;
            vulkan.msaa_samples = SampleCount::Count1;
            RenderSettings settings;
            settings.max_frames_in_flight = 1;
            if(!scene) {
                auto created = SceneRenderer::create(
                    device, programs, renderer.get_render_resources(), vulkan, settings, size);
                if(!created)
                    return Result<void, GraphicsError>::failure(created.error());
                scene = std::move(created).value();
            }
            if(!readback || readback->get_size() != face_bytes) {
                auto buffer =
                    Buffer::try_create_readback_buffer(device, face_bytes, "scene capture");
                if(!buffer)
                    return Result<void, GraphicsError>::failure(buffer.error());
                readback = std::move(buffer).value();
            }
            if(auto prepared = scene->prepare_post_process(world->post_process); !prepared)
                return prepared;
            if(auto prepared = scene->prepare_material_programs(*world); !prepared)
                return prepared;
            return record(renderer);
        }

        Result<void, GraphicsError> record(Renderer& renderer) {
            auto& device = renderer.get_render_context().get_device();
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            auto& command = frames.get_current_command_buffer();
            command.begin();
            geometry.prepare(*world);
            const ScopeExit release([&] { geometry.clear_frame(); });
            std::vector<QueueSemaphoreSubmit> waits;
            skinning = std::make_unique<SkinningPass>(device, 1);
            if(auto prepared = skinning->prepare(*world, frames, geometry, waits); !prepared)
                return prepared;
            RenderCameraResolver camera_resolver;
            const RenderView view{.render_size = size,
                .camera_selection = RenderView::CameraSelection::Override,
                .camera_override = cameras[face]};
            const RenderSubmission submission{*world, camera_resolver.resolve(*world, view)};
            auto drawn = scene->render(frames, submission, geometry);
            if(!drawn)
                return Result<void, GraphicsError>::failure(drawn.error());
            for(const auto& wait : drawn.value())
                merge_semaphore_wait(waits, wait);

            const auto view_image = output == Output::LinearHdr
                                        ? scene->get_hdr_color_view(0)
                                        : scene->get_offscreen_color_view(0);
            const auto image = view_image->get_image();
            RenderGraph graph;
            auto state = *resolve_image_state(ResourceUsage::SampledRead,
                {.aspects = Flags<ImageAspect>(ImageAspect::Color)},
                Flags<PipelineStage>(PipelineStage::FragmentShader));
            if(output == Output::Display)
                state.resource = *resolve_resource_state(ResourceUsage::ColorAttachmentWrite);
            const auto source = graph.import_image("scene capture", state);
            const auto destination = graph.import_buffer("pixels", {{}, 0, readback->get_size()});
            graph.add_pass(
                {"read scene capture", {{source, ResourceUsage::TransferSource, {}},
                                           {destination, ResourceUsage::TransferDestination, {}}}});
            graph.export_resource({destination, ResourceUsage::HostRead, {}});
            graph.export_resource({source, ResourceUsage::SampledRead,
                Flags<PipelineStage>(PipelineStage::FragmentShader)});
            auto plan = graph.compile();
            if(!plan)
                return Result<void, GraphicsError>::failure({plan.error()});
            const std::vector<RenderGraph::Binding> bindings{
                image, std::static_pointer_cast<Buffer>(readback)};
            auto copied = plan.value().record(frames, bindings, [&](size_t, CommandBuffer& cmd) {
                vk::BufferImageCopy region;
                region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
                region.imageExtent = vk::Extent3D(size.x, size.y, 1);
                cmd.get().copyImageToBuffer(
                    image->get(), vk::ImageLayout::eTransferSrcOptimal, readback->get(), region);
                return Result<void, GraphicsError>::success();
            });
            if(!copied)
                return copied;
            command.end();
            auto submitted = frames.submit(waits, {});
            if(!submitted)
                return Result<void, GraphicsError>::failure(submitted.error());
            completion = std::move(submitted).value();
            frames.end_frame();
            return Result<void, GraphicsError>::success();
        }
    };

    ScenePreview::ScenePreview(Renderer& renderer, Math::Vec2u size)
        : m_renderer(renderer), m_size(size) {}
    bool ScenePreview::busy() const {
        return m_state && m_state->completion.is_valid();
    }
    ScenePreview::~ScenePreview() {
        if(!m_state)
            return;
        if(m_renderer.get_render_context().get_device().is_resource_owner_thread()) {
            m_state.reset();
            return;
        }
        const auto released = m_renderer.prepare_overlay_resources([&] {
            m_state.reset();
            return Result<void, GraphicsError>::success();
        });
        if(!released)
            LOG_ERROR("Cannot release thumbnail renderer: {}", released.error().message);
    }

    Result<void, GraphicsError> ScenePreview::submit(
        std::shared_ptr<const RenderWorld> world, RenderCamera camera, Output output) {
        return start(std::move(world), {camera}, output, false);
    }

    Result<void, GraphicsError> ScenePreview::submit_cubemap(
        std::shared_ptr<const RenderWorld> world, Math::Vec3 position, float near_clip,
        float far_clip) {
        if(m_size.x != m_size.y || m_size.x < 1 || m_size.x > 512 || !std::has_single_bit(m_size.x))
            return Result<void, GraphicsError>::failure(
                {"Cubemap capture requires square power-of-two faces between 1 and 512 pixels"});
        if(!world)
            return Result<void, GraphicsError>::failure({"Scene capture requires a render world"});
        if(!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return Result<void, GraphicsError>::failure({"Cubemap position must be finite"});
        const std::array<Math::Vec3, 6> directions{
            {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
        const std::array<Math::Vec3, 6> up{
            {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}}};
        std::vector<RenderCamera> cameras;
        for(std::size_t face = 0; face < directions.size(); ++face)
            cameras.push_back(
                {.view_matrix = Math::look_at(position, position + directions[face], up[face]),
                    .fov_degrees = 90,
                    .near_clip = near_clip,
                    .far_clip = far_clip});
        auto captured = std::make_shared<RenderWorld>(*world);
        captured->reflection_probes.clear();
        captured->particles.reset();
        captured->particle_textures.clear();
        captured->environment.fog_enabled = false;
        captured->post_process = {};
        return start(std::move(captured), std::move(cameras), Output::LinearHdr, true);
    }

    Result<void, GraphicsError> ScenePreview::start(std::shared_ptr<const RenderWorld> world,
        std::vector<RenderCamera> cameras, Output output, bool cubemap) {
        if(busy())
            return Result<void, GraphicsError>::failure(
                {"Scene capture is already being rendered"});
        if(m_size.x == 0 || m_size.y == 0 || m_size.x > 4096 || m_size.y > 4096)
            return Result<void, GraphicsError>::failure(
                {"Preview dimensions must be between 1 and 4096"});
        if(!world)
            return Result<void, GraphicsError>::failure({"Scene capture requires a render world"});
        for(const auto& camera : cameras)
            if(camera.projection_issue(float(m_size.x) / m_size.y))
                return Result<void, GraphicsError>::failure(
                    {"Scene capture requires a valid camera"});
        return m_renderer.prepare_overlay_resources([&] {
            if(!m_state)
                m_state =
                    std::make_unique<State>(m_renderer.get_render_context().get_device(), m_size);
            if(auto recorded = m_state->begin(
                   m_renderer, std::move(world), std::move(cameras), output, cubemap);
                !recorded) {
                m_state.reset();
                return recorded;
            }
            return Result<void, GraphicsError>::success();
        });
    }

    Result<std::optional<TextureData>, GraphicsError> ScenePreview::poll() {
        using Outcome = Result<std::optional<TextureData>, GraphicsError>;
        std::optional<TextureData> image;
        if(!m_state || !m_state->completion.is_valid())
            return Outcome::success({});
        auto checked = m_renderer.prepare_overlay_resources([&] {
            if(!m_state->completion.is_complete())
                return Result<void, GraphicsError>::success();
            auto& state = *m_state;
            const auto bytes = state.readback->get_size();
            auto read = state.readback->read(std::as_writable_bytes(
                std::span(state.pixels.pixels).subspan(state.face * bytes, bytes)));
            m_state->frames.wait_for_all_slots();
            m_state->completion = {};
            if(!read) {
                m_state.reset();
                return read;
            }
            if(state.pixels.cubemap) {
                // 场景使用负高度 viewport；cubemap 的 v 则从每个面的负轴向正轴增长。
                const auto row_bytes = std::size_t(m_size.x) * 8;
                auto first = state.pixels.pixels.begin() + state.face * bytes;
                for(std::size_t row = 0; row < m_size.y / 2; ++row)
                    std::swap_ranges(first + row * row_bytes, first + (row + 1) * row_bytes,
                        first + (m_size.y - row - 1) * row_bytes);
            }
            if(++state.face < state.cameras.size()) {
                auto recorded = state.record(m_renderer);
                if(!recorded)
                    m_state.reset();
                return recorded;
            }
            m_state->scene->remove_assets({.all = true});
            m_state->programs.remove_assets({.all = true});
            m_state->skinning.reset();
            m_state->geometry.reset();
            m_state->world.reset();
            image = std::move(m_state->pixels);
            m_state->cameras.clear();
            return Result<void, GraphicsError>::success();
        });
        if(!checked)
            return Outcome::failure(checked.error());
        return Outcome::success(std::move(image));
    }
}

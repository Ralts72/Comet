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

        Math::Vec2u size;
        State(Device& device, Math::Vec2u dimensions) : frames(device, 1), size(dimensions) {
            frames.initialize_swapchain_images(1);
        }
        ~State() {
            if(completion.is_valid())
                frames.wait_for_all_slots();
        }

        Result<void, GraphicsError> record(Renderer& renderer, const RenderCamera& camera) {
            auto& device = renderer.get_render_context().get_device();
            if(programs.feedback()->builtin_revision
                != renderer.m_programs->feedback()->builtin_revision)
                scene.reset();
            // 共用宿主当前 Shader 语义；预览的发布和失败反馈留在自己的程序快照中。
            programs = *renderer.m_programs;
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
                auto buffer = Buffer::try_create_readback_buffer(
                    device, std::size_t(size.x) * size.y * 4, "thumbnail");
                if(!buffer)
                    return Result<void, GraphicsError>::failure(buffer.error());
                readback = std::move(buffer).value();
            }
            if(auto prepared = scene->prepare_post_process(world->post_process); !prepared)
                return prepared;
            if(auto prepared = scene->prepare_material_programs(*world); !prepared)
                return prepared;
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
                .camera_override = camera};
            const RenderSubmission submission{*world, camera_resolver.resolve(*world, view)};
            auto drawn = scene->render(frames, submission, geometry);
            if(!drawn)
                return Result<void, GraphicsError>::failure(drawn.error());
            for(const auto& wait : drawn.value())
                merge_semaphore_wait(waits, wait);

            const auto image = scene->get_offscreen_color_view(0)->get_image();
            RenderGraph graph;
            auto state = *resolve_image_state(ResourceUsage::SampledRead,
                {.aspects = Flags<ImageAspect>(ImageAspect::Color)},
                Flags<PipelineStage>(PipelineStage::FragmentShader));
            state.resource = *resolve_resource_state(ResourceUsage::ColorAttachmentWrite);
            const auto source = graph.import_image("thumbnail", state);
            const auto destination = graph.import_buffer("pixels", {{}, 0, readback->get_size()});
            graph.add_pass(
                {"read thumbnail", {{source, ResourceUsage::TransferSource, {}},
                                       {destination, ResourceUsage::TransferDestination, {}}}});
            graph.export_resource({destination, ResourceUsage::HostRead, {}});
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
        std::shared_ptr<const RenderWorld> world, RenderCamera camera) {
        if(m_state && m_state->completion.is_valid())
            return Result<void, GraphicsError>::failure({"Thumbnail is already being rendered"});
        if(m_size.x == 0 || m_size.y == 0 || m_size.x > 4096 || m_size.y > 4096)
            return Result<void, GraphicsError>::failure(
                {"Preview dimensions must be between 1 and 4096"});
        if(!world)
            return Result<void, GraphicsError>::failure({"Thumbnail requires a render world"});
        return m_renderer.prepare_overlay_resources([&] {
            if(!m_state)
                m_state =
                    std::make_unique<State>(m_renderer.get_render_context().get_device(), m_size);
            m_state->world = std::move(world);
            if(auto recorded = m_state->record(m_renderer, camera); !recorded) {
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
            TextureData data{
                .width = int(m_size.x), .height = int(m_size.y), .format = Format::R8G8B8A8_SRGB};
            data.pixels.resize(std::size_t(data.width) * data.height * 4);
            auto read = m_state->readback->read(std::as_writable_bytes(std::span(data.pixels)));
            m_state->frames.wait_for_all_slots();
            m_state->scene->remove_assets({.all = true});
            m_state->programs.remove_assets({.all = true});
            m_state->skinning.reset();
            m_state->geometry.reset();
            m_state->world.reset();
            m_state->completion = {};
            if(!read)
                return read;
            image = std::move(data);
            return Result<void, GraphicsError>::success();
        });
        if(!checked)
            return Outcome::failure(checked.error());
        return Outcome::success(std::move(image));
    }
}

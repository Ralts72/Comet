#include "render/passes/shadow_pass.h"

#include "graphics/device.h"
#include "graphics/frame_buffer.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/render_pass.h"
#include "graphics/resource/image_view.h"
#include "render/frame_scheduler.h"
#include "render/render_target.h"
#include "render/resource/mesh.h"
#include "directional_vert.h"
#include "directional_frag.h"

namespace Comet {
    ShadowPass::ShadowPass(Device& device, const uint32_t frame_slots,
        std::shared_ptr<RenderPass> pass, std::shared_ptr<RenderTarget> target,
        std::shared_ptr<Pipeline> pipeline)
        : m_device(device), m_frame_slots(frame_slots), m_render_pass(std::move(pass)),
          m_target(std::move(target)), m_pipeline(std::move(pipeline)) {}

    Result<std::unique_ptr<ShadowPass>, GraphicsError> ShadowPass::create(
        Device& device, const uint32_t frame_slots) {
        using Creation = Result<std::unique_ptr<ShadowPass>, GraphicsError>;
        if(frame_slots == 0)
            return Creation::failure({"Shadow pass requires frame slots"});
        const auto features = device.get_capability()
                                  .physical_device.getFormatProperties(vk::Format::eD32Sfloat)
                                  .optimalTilingFeatures;
        const auto required = vk::FormatFeatureFlagBits::eDepthStencilAttachment
                              | vk::FormatFeatureFlagBits::eSampledImage;
        if((features & required) != required)
            return Creation::failure({"D32 shadow depth sampling is unsupported"});
        auto depth = Attachment::get_depth_attachment(Format::D32_SFLOAT);
        depth.description.initial_layout = ImageLayout::DepthStencilAttachmentOptimal;
        depth.description.store_op = AttachmentStoreOp::Store;
        depth.usage |= ImageUsage::Sampled;
        const RenderSubPass subpass{
            .depth_stencil_attachments = {SubpassDepthStencilAttachment(0)}};
        auto pass = RenderPass::create(device, {depth}, {subpass});
        if(!pass)
            return Creation::failure(pass.error());
        auto target = RenderTarget::try_create_multi_target(
            device, *pass.value(), {RESOLUTION, RESOLUTION}, frame_slots);
        if(!target)
            return Creation::failure(target.error());
        auto vertex = Shader::create(device, "directional shadow vertex", DIRECTIONAL_VERT);
        if(!vertex)
            return Creation::failure(vertex.error());
        auto fragment = Shader::create(device, "directional shadow fragment", DIRECTIONAL_FRAG);
        if(!fragment)
            return Creation::failure(fragment.error());
        ShaderLayout layout;
        layout.push_constants.push_back(
            std::make_shared<PushConstantRange>(ShaderStage::Vertex, 0, sizeof(Math::Mat4)));
        VertexInputDescription input;
        input.add_binding(0, sizeof(MeshVertex), VertexInputRate::Vertex);
        input.add_attribute(0, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, position));
        PipelineConfig config;
        config.set_vertex_input_state(input);
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        config.enable_depth_test();
        PipelineManager pipelines(device, *pass.value());
        auto pipeline = pipelines.create_pipeline(
            "directional shadow", layout, config, vertex.value(), fragment.value());
        if(!pipeline)
            return Creation::failure(pipeline.error());
        return Creation::success(std::unique_ptr<ShadowPass>(new ShadowPass(device, frame_slots,
            std::move(pass).value(), std::move(target).value(), std::move(pipeline).value())));
    }

    std::shared_ptr<ImageView> ShadowPass::get_depth_view(const uint32_t slot) const {
        return m_target->get_framebuffer(slot)->get_attachments().front();
    }

    LightingData ShadowPass::prepare(const RenderSubmission& submission) {
        auto lighting = LightingData::prepare(submission.lights);
        if(!submission.view_project_matrix)
            return lighting;
        std::optional<BoundingBox> bounds;
        for(const auto& item : submission.render_items) {
            if(!item.mesh)
                continue;
            const auto box = transform_box(item.mesh->get_local_bounds(), item.model_matrix);
            if(!box)
                continue;
            if(!bounds)
                bounds = box;
            else {
                bounds->include(box->minimum);
                bounds->include(box->maximum);
            }
        }
        if(bounds)
            lighting.prepare_shadow(*bounds, RESOLUTION);
        return lighting;
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> ShadowPass::render(
        FrameScheduler& frames, const LightingData& lighting,
        const std::span<const ResolvedRenderItem> items) {
        using Draw = Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>;
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= m_frame_slots)
            return Draw::failure({"Invalid shadow pass frame"});
        auto& command = frames.get_current_command_buffer();
        frames.retain_current_frame_resource(m_render_pass);
        frames.retain_current_frame_resource(m_target);
        frames.retain_current_frame_resource(m_pipeline);
        m_target->begin_render_target(command, frames.get_current_frame_slot_index());
        command.set_viewport(vk::Viewport(0, 0, RESOLUTION, RESOLUTION, 0, 1));
        command.set_scissor(vk::Rect2D({0, 0}, {RESOLUTION, RESOLUTION}));
        std::vector<QueueSemaphoreSubmit> waits;
        if(lighting.shadow_light_index >= 0) {
            command.bind_pipeline(*m_pipeline);
            for(const auto& item : items) {
                if(!item.mesh || !transform_box(item.mesh->get_local_bounds(), item.model_matrix))
                    continue;
                const auto mvp = lighting.shadow_view_projection * item.model_matrix;
                command.push_constants(*m_pipeline->get_layout(),
                    Flags<ShaderStage>(ShaderStage::Vertex), 0, &mvp, sizeof(mvp));
                frames.retain_current_frame_resource(item.mesh);
                const auto& ready = item.mesh->get_ready_completion();
                if(ready.is_valid() && !ready.is_complete())
                    merge_semaphore_wait(
                        waits, QueueSemaphoreSubmit(
                                   ready, Flags<PipelineStage>(PipelineStage::VertexInput)));
                item.mesh->draw(command);
            }
        }
        m_target->end_render_target(command);
        return Draw::success(std::move(waits));
    }
}

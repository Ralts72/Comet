#include "render/shadow_renderer.h"

#include "graphics/device.h"
#include "graphics/frame_buffer.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/pipeline/shader.h"
#include "graphics/render_pass.h"
#include "graphics/resource/image_view.h"
#include "render/frame_scheduler.h"
#include "render/render_target.h"
#include "render/resource/mesh.h"
#include "shadow_depth_vert.h"
#include "shadow_depth_frag.h"

namespace Comet {
    ShadowRenderer::ShadowRenderer(Device& device, const uint32_t frame_slots) {
        auto depth = Attachment::get_depth_attachment(Format::D32_SFLOAT);
        depth.description.initial_layout = ImageLayout::DepthStencilAttachmentOptimal;
        depth.description.store_op = AttachmentStoreOp::Store;
        depth.usage |= ImageUsage::Sampled;
        m_render_pass =
            std::make_shared<RenderPass>(device, std::vector<Attachment>{depth},
                std::vector<RenderSubPass>{
                    {{}, {}, {SubpassDepthStencilAttachment(0)}, SampleCount::Count1}});
        m_target = RenderTarget::create_multi_target(
            device, *m_render_pass, {RESOLUTION, RESOLUTION}, frame_slots);
        ShaderLayout layout;
        layout.push_constants.push_back(std::make_shared<PushConstantRange>(
            ShaderStage::Vertex, 0, sizeof(Math::Mat4)));
        VertexInputDescription input;
        input.add_binding(0, sizeof(MeshVertex), VertexInputRate::Vertex);
        input.add_attribute(
            0, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, position));
        PipelineConfig config;
        config.set_vertex_input_state(input);
        config.set_dynamic_state({DynamicState::Viewport, DynamicState::Scissor});
        config.enable_depth_test();
        PipelineManager pipelines(device, *m_render_pass);
        m_pipeline = pipelines.create_pipeline("directional shadow", layout, config,
            std::make_shared<Shader>(device, "shadow depth vertex", SHADOW_DEPTH_VERT),
            std::make_shared<Shader>(device, "shadow depth fragment", SHADOW_DEPTH_FRAG));
    }

    std::shared_ptr<ImageView> ShadowRenderer::get_depth_view(const uint32_t slot) const {
        return m_target->get_framebuffer(slot)->get_attachments().front();
    }

    LightingData ShadowRenderer::prepare(const RenderSubmission& submission) {
        auto lighting = LightingData::prepare(submission.lights);
        if(!submission.view_project_matrix)
            return lighting;
        std::optional<BoundingBox> bounds;
        for(const auto& item : submission.render_items) {
            if(!item.mesh)
                continue;
            const auto box =
                transform_box(item.mesh->get_local_bounds(), item.model_matrix);
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

    std::vector<QueueSemaphoreSubmit> ShadowRenderer::render(FrameScheduler& frames,
        const LightingData& lighting, const std::span<const ResolvedRenderItem> items) {
        auto& command = frames.get_current_command_buffer();
        frames.retain_current_frame_resource(m_render_pass);
        frames.retain_current_frame_resource(m_target);
        frames.retain_current_frame_resource(m_pipeline);
        m_target->begin_render_target(command, frames.get_current_frame_slot_index());
        command.set_viewport(vk::Viewport(0, 0, RESOLUTION, RESOLUTION, 0, 1));
        command.set_scissor(vk::Rect2D({0, 0}, {RESOLUTION, RESOLUTION}));
        std::vector<QueueSemaphoreSubmit> waits;
        if(lighting.shadow_parameters.x >= 0) {
            command.bind_pipeline(*m_pipeline);
            for(const auto& item : items) {
                if(!item.mesh
                    || !transform_box(item.mesh->get_local_bounds(), item.model_matrix))
                    continue;
                const auto mvp = lighting.shadow_view_projection * item.model_matrix;
                command.push_constants(*m_pipeline->get_layout(),
                    Flags<ShaderStage>(ShaderStage::Vertex), 0, &mvp, sizeof(mvp));
                frames.retain_current_frame_resource(item.mesh);
                const auto& ready = item.mesh->get_ready_completion();
                if(ready.is_valid() && !ready.is_complete())
                    merge_semaphore_wait(
                        waits, QueueSemaphoreSubmit(ready,
                                   Flags<PipelineStage>(PipelineStage::VertexInput)));
                item.mesh->draw(command);
            }
        }
        m_target->end_render_target(command);
        return waits;
    }
}

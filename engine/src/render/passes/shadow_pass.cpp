#include "render/passes/shadow_pass.h"
#include "graphics/device.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "render/frame_scheduler.h"
#include <algorithm>

namespace Comet {
    Result<std::unique_ptr<ShadowPass>, GraphicsError> ShadowPass::create(
        Device& device, uint32_t frame_slots) {
        using Creation = Result<std::unique_ptr<ShadowPass>, GraphicsError>;
        if(frame_slots == 0)
            return Creation::failure({"Shadow pass requires frame slots"});
        const auto support = device.query_format_support(Format::D32_SFLOAT);
        if(!support.depth_attachment || !support.sampled)
            return Creation::failure({"D32 shadow depth sampling is unsupported"});
        auto shadow = std::unique_ptr<ShadowPass>(new ShadowPass(device, frame_slots));
        for(uint32_t slot = 0; slot < frame_slots; ++slot) {
            auto image = Image::try_create(device,
                {.format = Format::D32_SFLOAT,
                    .extent = {1, 1, 1},
                    .usage = Flags<ImageUsage>(ImageUsage::Sampled) | ImageUsage::CopyDst},
                false, SampleCount::Count1, "unshadowed depth fallback");
            if(!image)
                return Creation::failure(image.error());
            auto view = ImageView::try_create(
                device, std::move(image).value(), Flags<ImageAspect>(ImageAspect::Depth));
            if(!view)
                return Creation::failure(view.error());
            shadow->m_fallback.push_back({std::move(view).value()});
        }
        return Creation::success(std::move(shadow));
    }

    bool ShadowPass::needs_shadow(const LightingData& lighting) {
        return std::ranges::any_of(
            lighting.shadows, [](const auto& shadow) { return shadow.light_index >= 0; });
    }

    Result<void, GraphicsError> ShadowPass::prepare_frame(
        FrameScheduler& frames, const LightingData& lighting) {
        if(!frames.is_recording_frame() || &frames.get_device() != &m_device
            || frames.get_current_frame_slot_index() >= m_frame_slots)
            return Result<void, GraphicsError>::failure({"Invalid shadow frame"});
        m_statistics = {};
        auto& fallback = m_fallback.at(frames.get_current_frame_slot_index());
        if(fallback.recorded_serial) {
            fallback.initialized =
                fallback.recorded_serial == frames.get_current_frame_slot().last_submission_serial;
            fallback.recorded_serial = 0;
        }
        if(needs_shadow(lighting)) {
            if(!m_depth) {
                auto depth = DepthPass::create(m_device, m_frame_slots,
                    {RESOLUTION / 2 * LightingData::SHADOW_ATLAS_COLUMNS,
                        RESOLUTION / 2 * LightingData::SHADOW_ATLAS_ROWS},
                    LightingData::MAX_SHADOW_VIEWS);
                if(!depth)
                    return Result<void, GraphicsError>::failure(depth.error());
                m_depth = std::move(depth).value();
            }
            m_statistics.atlas_texels = uint64_t(RESOLUTION / 2) * (RESOLUTION / 2)
                                        * LightingData::SHADOW_ATLAS_COLUMNS
                                        * LightingData::SHADOW_ATLAS_ROWS * m_frame_slots;
            return Result<void, GraphicsError>::success();
        }
        // 旧图集的附件、管线与实例缓冲已由实际录制它的帧保活。
        m_depth.reset();
        ++m_statistics.skipped_passes;
        const auto& view = fallback.view;
        frames.retain_current_frame_resource(view);
        if(fallback.initialized)
            return Result<void, GraphicsError>::success();
        const auto image = view->get_image()->get();
        const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);
        const vk::ImageMemoryBarrier before(vk::AccessFlagBits::eShaderRead,
            vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            image, range);
        auto command = frames.get_current_command_buffer().get();
        command.pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, before);
        command.clearDepthStencilImage(
            image, vk::ImageLayout::eTransferDstOptimal, vk::ClearDepthStencilValue(1, 0), range);
        const vk::ImageMemoryBarrier after(vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED, image, range);
        command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, after);
        fallback.recorded_serial = frames.get_current_frame_serial();
        return Result<void, GraphicsError>::success();
    }

    LightingData ShadowPass::prepare(
        const RenderSubmission& submission, const RenderGeometry& geometry) {
        auto lighting = LightingData::prepare(submission.world.lights);
        if(geometry.get_scene_bounds() && submission.view_project_matrix) {
            const auto& camera = *submission.view_project_matrix;
            lighting.prepare_shadow(
                *geometry.get_scene_bounds(), RESOLUTION, camera.view, camera.projection);
        }
        return lighting;
    }

    Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> ShadowPass::render(
        FrameScheduler& frames, const LightingData& lighting, const RenderGeometry& geometry) {
        if(!m_depth || !needs_shadow(lighting))
            return Result<std::vector<QueueSemaphoreSubmit>, GraphicsError>::failure(
                {"Shadow depth pass has not been prepared"});
        std::array<DepthPass::View, LightingData::MAX_SHADOW_VIEWS> views;
        for(size_t i = 0; i < views.size(); ++i) {
            const auto& shadow = lighting.shadows[i];
            views[i] = {shadow.view_projection, shadow.atlas_rect, shadow.light_index >= 0};
        }
        auto result =
            m_depth->render(frames, views, geometry, DepthPass::LodSelection::AlreadySelected);
        static_cast<DepthPass::Statistics&>(m_statistics) = m_depth->get_statistics();
        ++m_statistics.rendered_passes;
        m_statistics.cascades = uint32_t(lighting.shadow_cascade_count);
        m_statistics.spot_lights = uint32_t(lighting.spot_shadow_count);
        m_statistics.excess_spot_lights = uint32_t(lighting.excess_spot_shadows);
        m_statistics.point_lights = uint32_t(lighting.point_shadow_count);
        m_statistics.excess_point_lights = uint32_t(lighting.excess_point_shadows);
        return result;
    }
}

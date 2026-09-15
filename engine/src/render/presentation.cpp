#include "render/presentation.h"
#include "render/render_context.h"
#include "render/frame_scheduler.h"
#include "graphics/device.h"
#include "graphics/swapchain.h"
#include "diagnostics/profiler.h"
#include "diagnostics/logger.h"

#include <utility>

namespace Comet {
    Presentation::Presentation(RenderContext& context, FrameScheduler& frames, Dependent scene)
        : m_installed_config(context.get_swapchain().get_active_generation()->get_config()),
          m_context(context), m_frames(frames), m_scene(std::move(scene)) {}

    void Presentation::set_overlay(Dependent overlay) {
        m_overlay = std::move(overlay);
    }

    Result<bool, GraphicsError> Presentation::begin_frame() {
        PROFILE_SCOPE("Presentation::begin_frame");
        using Preparation = Result<bool, GraphicsError>;
        if(m_recovery != RecoveryStage::Ready) {
            if(m_retry_pending && !m_retry.consume(RetryBackoff::Clock::now()))
                return Preparation::success(false);
            m_retry_pending = false;
            if(auto recovery = recover(); !recovery)
                return Preparation::failure(recovery.error());
            if(m_recovery != RecoveryStage::Ready)
                return Preparation::success(false);
        }
        m_frames.wait_for_current_slot();
        m_context.get_device().set_allocator_frame_index(m_frames.get_current_frame_serial());
        auto& frame_slot = m_frames.get_current_frame_slot();
        auto acquisition =
            m_context.get_swapchain().acquire_next_image(frame_slot.image_available_semaphore);
        if(!acquisition) {
            m_recovery = RecoveryStage::Swapchain;
            if(auto failure = handle_failure(acquisition.error()); !failure)
                return Preparation::failure(failure.error());
            return Preparation::success(false);
        }
        if(!acquisition.value()) {
            m_recovery = RecoveryStage::Swapchain;
            return Preparation::success(false);
        }
        m_frames.begin_frame(*acquisition.value());
        m_frames.get_current_command_buffer().begin(
            Flags<CommandBuffer::Usage>(CommandBuffer::Usage::OneTimeSubmit));
        return Preparation::success(true);
    }

    Result<void, GraphicsError> Presentation::end_frame(
        const std::span<const QueueSemaphoreSubmit> resource_waits) {
        PROFILE_SCOPE("Presentation::end_frame");

        auto& device = m_context.get_device();
        auto& swapchain = m_context.get_swapchain();
        const uint32_t image_index = swapchain.get_current_index();
        auto& frame_slot = m_frames.get_current_frame_slot();
        auto& image_state = m_frames.get_swapchain_image_state(image_index);

        frame_slot.command_buffer.end();

        std::vector<QueueSemaphoreSubmit> waits;
        waits.reserve(1 + resource_waits.size());
        waits.emplace_back(QueueSemaphoreSubmit{frame_slot.image_available_semaphore,
            Flags<PipelineStage>(PipelineStage::ColorAttachmentOutput)});
        waits.insert(waits.end(), resource_waits.begin(), resource_waits.end());
        const QueueSemaphoreSubmit render_finished_signal{image_state.render_finished_semaphore,
            Flags<PipelineStage>(PipelineStage::AllCommands)};
        const auto submission = m_frames.submit(waits, std::span(&render_finished_signal, 1));
        if(!submission)
            return Result<void, GraphicsError>::failure(submission.error());

        auto& present_queue = device.get_present_queue(0);
        const auto result = present_queue.present(
            swapchain, std::span(&image_state.render_finished_semaphore, 1), image_index);
        m_frames.end_frame();
        if(!result) {
            m_recovery = RecoveryStage::Swapchain;
            return handle_failure(result.error());
        }
        if(result.value() == Queue::PresentStatus::RecreateRequired)
            m_recovery = RecoveryStage::Swapchain;
        return Result<void, GraphicsError>::success();
    }

    void Presentation::request_recreation() {
        m_retry.reset();
        m_retry_pending = false;
        if(m_recovery == RecoveryStage::Ready)
            m_recovery = RecoveryStage::Swapchain;
    }

    void Presentation::release_dependents() {
        if(m_overlay.release)
            m_overlay.release();
        if(m_scene.release)
            m_scene.release();
    }

    Result<void, GraphicsError> Presentation::handle_failure(const GraphicsError& error) {
        if(error.result == vk::Result::eErrorSurfaceLostKHR)
            m_recovery = RecoveryStage::Surface;
        const bool retryable =
            error.is_out_of_memory() || error.result == vk::Result::eErrorOutOfDateKHR
            || error.result == vk::Result::eErrorSurfaceLostKHR
            || error.result == vk::Result::eTimeout || error.result == vk::Result::eNotReady;
        if(!retryable || !m_retry.schedule(RetryBackoff::Clock::now()))
            return Result<void, GraphicsError>::failure(error);
        m_retry_pending = true;
        LOG_WARN("Presentation suspended; scheduled retry: {}", error.message);
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> Presentation::recover() {
        PROFILE_SCOPE("Presentation::recover");
        auto& swapchain = m_context.get_swapchain();
        m_frames.wait_for_all_slots();
        m_context.get_device().get_present_queue(0).wait_idle();
        release_dependents();
        if(m_recovery == RecoveryStage::Surface) {
            auto surface = swapchain.recreate_surface();
            if(!surface)
                return handle_failure(surface.error());
            m_recovery = RecoveryStage::Swapchain;
        }
        if(m_recovery == RecoveryStage::Swapchain) {
            const auto recreation = swapchain.recreate();
            if(!recreation)
                return handle_failure(recreation.error());
            if(recreation.value() == Swapchain::RecreateStatus::Deferred)
                return Result<void, GraphicsError>::success();
            m_recovery = RecoveryStage::Dependents;
            m_frames.initialize_swapchain_images(
                static_cast<uint32_t>(swapchain.get_images().size()));
        }
        const auto current = swapchain.get_active_generation()->get_config();
        const auto compatibility = compare_swapchain_configs(m_installed_config, current);
        for(const auto* dependent : {&m_scene, &m_overlay}) {
            if(dependent->rebuild) {
                auto result = dependent->rebuild(compatibility);
                if(!result)
                    return handle_failure(result.error());
            }
        }
        m_installed_config = current;
        m_recovery = RecoveryStage::Ready;
        m_retry.reset();
        m_retry_pending = false;
        return Result<void, GraphicsError>::success();
    }
}

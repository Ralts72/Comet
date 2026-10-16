#include "render/path_trace_denoise.h"
#include "render/image_denoiser.h"
#include "render/renderer.h"
#include "core/task_scheduler.h"

#include <chrono>
#include <future>

namespace CometEditor {
    using namespace Comet;
    struct PathTraceDenoise::Job {
        std::future<Result<TextureData>> result;
        uint64_t generation;
        uint32_t samples;
        uint64_t scene_lifetime;
        bool cancelled = false;
    };
    PathTraceDenoise::PathTraceDenoise(Renderer& renderer, TaskScheduler& scheduler)
        : m_renderer(renderer), m_scheduler(scheduler),
          m_denoiser(std::make_shared<ImageDenoiser>()) {}
    PathTraceDenoise::~PathTraceDenoise() = default;

    void PathTraceDenoise::update(bool enabled, uint32_t target_samples, uint64_t scene_lifetime) {
        const auto current = m_renderer.path_trace_statistics();
        if(!enabled || scene_lifetime != m_scene_lifetime || current.generation != m_generation
            || current.samples != m_samples) {
            m_image = {};
            m_status.clear();
            if(m_job)
                m_job->cancelled = true;
        }
        if(!enabled) {
            m_generation = 0;
            m_samples = 0;
        }
        m_enabled = enabled;
        m_scene_lifetime = scene_lifetime;
        if(m_job) {
            if(m_job->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            auto job = std::move(m_job);
            auto result = job->result.get();
            if(!job->cancelled && enabled && current.generation == job->generation
                && current.samples == job->samples && scene_lifetime == job->scene_lifetime) {
                if(!result) {
                    m_status = result.error();
                    return;
                }
                auto published = m_renderer.publish_path_trace_denoised(
                    result.value(), job->generation, job->samples);
                if(!published) {
                    m_status = published.error().message;
                    return;
                }
                if(published.value()) {
                    m_image = std::move(result).value();
                    m_status = "已降噪";
                }
            }
        }
        if(!enabled || !current.features || current.samples == 0 || current.samples < target_samples
            || !m_renderer.path_trace_error().empty()
            || (m_generation == current.generation && m_samples == current.samples))
            return;
        m_generation = current.generation;
        m_samples = current.samples;
        auto captured = m_renderer.capture_path_trace_inputs();
        if(!captured) {
            m_status = captured.error().message;
            return;
        }
        auto result = m_scheduler.try_submit_result(
            [denoiser = m_denoiser, input = std::move(captured).value()]() -> Result<TextureData> {
                try {
                    return denoiser->filter(input.color, input.albedo, input.normal);
                } catch(const std::exception& error) {
                    return Result<TextureData>::failure(error.what());
                }
            });
        if(!result) {
            m_status = "后台任务队列已满；重新开启降噪可重试";
            return;
        }
        m_job =
            std::make_unique<Job>(Job{std::move(*result), m_generation, m_samples, scene_lifetime});
        m_status = "正在降噪…";
    }
    const TextureData* PathTraceDenoise::image() const {
        const auto current = m_renderer.path_trace_statistics();
        if(!m_enabled || m_image.pixels.empty() || current.generation != m_generation
            || current.samples != m_samples)
            return nullptr;
        return &m_image;
    }
    bool PathTraceDenoise::busy() const {
        return bool(m_job);
    }
}

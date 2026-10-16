#pragma once

#include "asset/data/texture_data.h"

#include <memory>
#include <string>

namespace Comet {
    class Renderer;
    class TaskScheduler;
}
namespace CometEditor {
    class ImageDenoiser;
    class PathTraceDenoise {
    public:
        PathTraceDenoise(Comet::Renderer& renderer, Comet::TaskScheduler& scheduler);
        ~PathTraceDenoise();
        void update(bool enabled, uint32_t target_samples, uint64_t scene_lifetime);
        [[nodiscard]] const Comet::TextureData* image() const;
        [[nodiscard]] bool busy() const;
        [[nodiscard]] const std::string& status() const { return m_status; }

    private:
        struct Job;
        Comet::Renderer& m_renderer;
        Comet::TaskScheduler& m_scheduler;
        std::shared_ptr<ImageDenoiser> m_denoiser;
        std::unique_ptr<Job> m_job;
        Comet::TextureData m_image;
        uint64_t m_scene_lifetime = 0;
        uint64_t m_generation = 0;
        uint32_t m_samples = 0;
        bool m_enabled = false;
        std::string m_status;
    };
}

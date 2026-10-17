#pragma once

#include "render/scene/render_submission.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/queue.h"

namespace Comet {
    // 单个视图选中的具体资源与 UBO 数据。
    struct COMET_API ViewLightingData {
        LightingData lighting;
        std::shared_ptr<Environment> environment;
        std::array<std::shared_ptr<Environment>, LightingData::MAX_REFLECTION_PROBES> probes;
        std::array<std::shared_ptr<Irradiance>, LightingData::MAX_IRRADIANCE_VOLUMES> irradiance;
        uint32_t probe_count = 0;
        uint32_t excess_probes = 0;
        uint32_t irradiance_volume_count = 0;
        uint32_t excess_irradiance_volumes = 0;
        uint32_t irradiance_probes = 0;
        uint64_t irradiance_bytes = 0;

        [[nodiscard]] static ViewLightingData prepare(
            const RenderSubmission& submission, const LightingData& lighting);
    };
    class Device;
    class FrameScheduler;
    class RenderResources;
    class CPUBuffer;
    class DescriptorPool;
    class Sampler;
    class Texture;
    struct FogVolume;

    // 相机、光照与环境的逐槽绑定；材质、贴花和体积雾共用这一份视图输入。
    class COMET_API ViewBindings {
    public:
        struct Statistics {
            uint32_t frame_set_count = 0;
            uint32_t light_count = 0;
            uint32_t excess_lights = 0;
            uint32_t invalid_lights = 0;
            uint32_t reflection_probes = 0;
            uint32_t excess_reflection_probes = 0;
            uint32_t irradiance_probes = 0;
            uint32_t excess_irradiance_volumes = 0;
            uint64_t irradiance_bytes = 0;
        };
        Result<void, GraphicsError> initialize(Device& device, RenderResources& resources,
            uint32_t frame_slots, const std::shared_ptr<Texture>& fallback);
        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> prepare(
            FrameScheduler& frames, const RenderSubmission& submission,
            const LightingData& lighting, const std::shared_ptr<ImageView>& shadow_map,
            const std::shared_ptr<ImageView>& depth = {}, const FogVolume* fog = nullptr);
        [[nodiscard]] const std::shared_ptr<DescriptorSetLayout>& layout() const {
            return m_layout;
        }
        [[nodiscard]] DescriptorSet descriptor(uint32_t slot) const;
        [[nodiscard]] std::span<const std::shared_ptr<Irradiance>> irradiance(uint32_t slot) const;
        [[nodiscard]] const std::shared_ptr<Texture>& fallback_texture() const {
            return m_fallback;
        }
        [[nodiscard]] Statistics statistics() const;
        void reset_statistics() { m_statistics = {}; }

    private:
        struct Frame {
            std::shared_ptr<DescriptorSetLayout> layout;
            std::shared_ptr<DescriptorPool> pool;
            std::shared_ptr<CPUBuffer> buffer;
            std::shared_ptr<CPUBuffer> lighting;
            std::shared_ptr<Sampler> shadow_sampler;
            std::shared_ptr<ImageView> shadow_map;
            std::shared_ptr<ImageView> view_depth;
            std::shared_ptr<ImageView> fog;
            std::shared_ptr<Sampler> environment_sampler;
            std::shared_ptr<Environment> environment;
            std::array<std::shared_ptr<Irradiance>, LightingData::MAX_IRRADIANCE_VOLUMES>
                irradiance;
            std::array<std::shared_ptr<Environment>, LightingData::MAX_REFLECTION_PROBES>
                reflection_probes;
            std::optional<DescriptorSet> descriptor;
        };
        Device* m_device = nullptr;
        std::shared_ptr<Texture> m_fallback;
        std::shared_ptr<Environment> m_empty_environment;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::vector<std::shared_ptr<Frame>> m_frames;
        Statistics m_statistics;
    };
}

#pragma once

#include "graphics/queue.h"
#include "graphics/pipeline/descriptor_set.h"

#include <array>
#include <map>
#include "render/scene/render_submission.h"
#include "render/scene/render_geometry.h"
#include "render/resource/instance_buffer.h"

namespace Comet {
    class FrameScheduler;
    class ImageView;
    class Pipeline;
    class RenderPass;
    class RenderTarget;
    class Texture;
    class Sampler;

    // 每个 View 的方向光级联与聚光灯阴影；每个飞行帧槽位拥有独立深度图集。
    class COMET_API ShadowPass {
    public:
        struct Statistics {
            uint32_t cascades = 0;
            uint32_t spot_lights = 0;
            uint32_t excess_spot_lights = 0;
            uint32_t draw_calls = 0;
            uint32_t drawn_instances = 0;
            uint64_t drawn_triangles = 0;
            uint64_t instance_upload_bytes = 0;
        };
        static constexpr uint32_t RESOLUTION = 1024;
        static Result<std::unique_ptr<ShadowPass>, GraphicsError> create(
            Device& device, uint32_t frame_slots);
        ShadowPass(const ShadowPass&) = delete;
        ShadowPass& operator=(const ShadowPass&) = delete;
        [[nodiscard]] static LightingData prepare(
            const RenderSubmission& submission, const RenderGeometry& geometry);
        [[nodiscard]] std::shared_ptr<ImageView> get_depth_view(uint32_t slot) const;
        [[nodiscard]] Statistics get_statistics() const { return m_statistics; }
        void reset_statistics() { m_statistics = {}; }
        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render(
            FrameScheduler& frames, const LightingData& lighting, const RenderGeometry& geometry);

    private:
        ShadowPass(Device& device, uint32_t frame_slots, std::shared_ptr<RenderPass> pass,
            std::shared_ptr<RenderTarget> target, std::shared_ptr<Pipeline> pipeline);
        struct MaskBinding {
            std::shared_ptr<Texture> texture;
            std::shared_ptr<Sampler> sampler;
            std::shared_ptr<DescriptorSetLayout> layout;
            std::shared_ptr<DescriptorPool> pool;
            DescriptorSet descriptor;
        };
        struct CachedMask {
            std::shared_ptr<MaskBinding> binding;
            bool used = false;
        };
        struct DrawItem {
            const ResolvedRenderItem* item;
            std::shared_ptr<MaskBinding> mask;
            float alpha = 1;
            float cutoff = 0.5f;
            unsigned orientation = 0; // 双面位与镜像变换位。
            unsigned shadow_view = 0;
        };
        Result<std::shared_ptr<MaskBinding>, GraphicsError> prepare_mask(
            const std::shared_ptr<Texture>& texture);
        Result<void, GraphicsError> prepare_draws(
            const LightingData& lighting, const RenderGeometry& geometry);
        Device& m_device;
        uint32_t m_frame_slots;
        std::shared_ptr<RenderPass> m_render_pass;
        std::shared_ptr<RenderTarget> m_target;
        std::array<std::shared_ptr<Pipeline>, 4> m_pipelines;
        std::array<std::shared_ptr<Pipeline>, 4> m_mask_pipelines;
        std::shared_ptr<DescriptorSetLayout> m_mask_layout;
        std::shared_ptr<Sampler> m_mask_sampler;
        std::map<const Texture*, CachedMask> m_masks;
        std::vector<InstanceBuffer> m_instances;
        std::vector<DrawItem> m_draw_queue;
        std::vector<Math::Mat4> m_transforms;
        std::array<RenderGeometry, LightingData::MAX_SHADOW_VIEWS> m_view_geometry;
        Statistics m_statistics;
    };
}

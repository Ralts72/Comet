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

    // 单方向光深度通道；每个飞行帧槽位拥有独立深度图。
    class COMET_API ShadowPass {
    public:
        struct Statistics {
            uint32_t draw_calls = 0;
            uint32_t drawn_instances = 0;
            uint64_t instance_upload_bytes = 0;
        };
        static constexpr uint32_t RESOLUTION = 1024;
        static Result<std::unique_ptr<ShadowPass>, GraphicsError> create(
            Device& device, uint32_t frame_slots);
        ShadowPass(const ShadowPass&) = delete;
        ShadowPass& operator=(const ShadowPass&) = delete;
        [[nodiscard]] static LightingData prepare(
            const RenderWorld& world, const RenderGeometry& geometry);
        // 同一世界帧共享准备结果和深度图；后续 View 只采样已完成的通道。
        [[nodiscard]] const LightingData& prepare_frame(
            uint64_t serial, const RenderWorld& world, const RenderGeometry& geometry);
        [[nodiscard]] bool rendered_in_frame(uint64_t serial) const {
            return m_rendered_serial == serial;
        }
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
        Statistics m_statistics;
        LightingData m_lighting;
        uint64_t m_prepared_serial = 0;
        uint64_t m_rendered_serial = 0;
    };
}

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

    // 阴影与相机预深度共用的几何绘制；拥有目标、裁切绑定与在途帧资源。
    class COMET_API DepthPass {
    public:
        struct Statistics {
            uint32_t draw_calls = 0;
            uint32_t drawn_instances = 0;
            uint64_t drawn_triangles = 0;
            uint64_t instance_upload_bytes = 0;
        };
        struct View {
            Math::Mat4 view_projection{1};
            Math::Vec4 atlas_rect{1, 1, 0, 0};
            bool enabled = true;
        };
        static Result<std::unique_ptr<DepthPass>, GraphicsError> create(
            Device& device, uint32_t frame_slots, Math::Vec2u size, uint32_t view_count);
        DepthPass(const DepthPass&) = delete;
        DepthPass& operator=(const DepthPass&) = delete;
        Result<void, GraphicsError> resize(Math::Vec2u size);
        [[nodiscard]] std::shared_ptr<ImageView> get_depth_view(uint32_t slot) const;
        [[nodiscard]] Statistics get_statistics() const { return m_statistics; }
        void reset_statistics() { m_statistics = {}; }
        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render(
            FrameScheduler& frames, std::span<const View> views, const RenderGeometry& geometry);

    private:
        DepthPass(Device& device, uint32_t frame_slots, std::shared_ptr<RenderPass> pass,
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
            unsigned view = 0;
        };
        Result<std::shared_ptr<MaskBinding>, GraphicsError> prepare_mask(
            const std::shared_ptr<Texture>& texture);
        Result<void, GraphicsError> prepare_draws(
            std::span<const View> views, const RenderGeometry& geometry);
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
        std::vector<RenderGeometry> m_view_geometry;
        Statistics m_statistics;
    };
}

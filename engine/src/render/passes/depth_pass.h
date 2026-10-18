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
    class Buffer;

    // 阴影与相机预深度共用的几何绘制；拥有目标、裁切绑定与在途帧资源。
    class COMET_API DepthPass {
    public:
        struct Statistics {
            uint32_t draw_calls = 0;
            uint32_t drawn_instances = 0;
            uint64_t drawn_triangles = 0;
            uint64_t instance_upload_bytes = 0;
            uint32_t coarse_culled_items = 0;
            uint32_t lod_selections = 0;
        };
        struct View {
            Math::Mat4 view_projection{1};
            Math::Vec4 atlas_rect{1, 1, 0, 0};
            bool enabled = true;
            std::optional<Math::Mat4> previous_view_projection;
            Math::Vec4 previous_projection{};
        };
        enum class Output { Depth, Motion, Outline };
        static Result<std::unique_ptr<DepthPass>, GraphicsError> create(Device& device,
            uint32_t frame_slots, Math::Vec2u size, uint32_t view_count,
            Output output = Output::Depth);
        DepthPass(const DepthPass&) = delete;
        DepthPass& operator=(const DepthPass&) = delete;
        Result<void, GraphicsError> resize(Math::Vec2u size);
        [[nodiscard]] std::shared_ptr<ImageView> get_depth_view(uint32_t slot) const;
        [[nodiscard]] std::shared_ptr<ImageView> get_motion_view(uint32_t slot) const;
        [[nodiscard]] std::shared_ptr<ImageView> get_color_view(
            uint32_t slot, uint32_t index) const;
        [[nodiscard]] Statistics get_statistics() const { return m_statistics; }
        void reset_statistics() { m_statistics = {}; }
        enum class LodSelection { PerView, AlreadySelected };
        // 同一颜色视图的相机深度与阴影共用已选级几何，各深度 View 独立裁剪。
        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render(
            FrameScheduler& frames, std::span<const View> views, const RenderGeometry& geometry,
            LodSelection lod = LodSelection::PerView);

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
            std::span<const View> views, const RenderGeometry& geometry, LodSelection lod);
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
        std::vector<InstanceBuffer> m_previous_instances;
        std::vector<PushConstant> m_previous_transforms;
        struct MotionHistory {
            Math::Mat4 model{1};
            Math::Vec4 vertex_bend{0};
            AssetHandle handle;
            uint64_t revision = 0;
            std::shared_ptr<Mesh> mesh;
            std::shared_ptr<Buffer> vertices;
            std::shared_ptr<const AnimationPlayback> animation;
            bool used = false;
        };
        std::unordered_map<EntityId, MotionHistory> m_motion_history;
        bool m_motion_vectors = false;
        bool m_outline = false;
        std::vector<DrawItem> m_draw_queue;
        std::vector<PushConstant> m_transforms;
        std::vector<uint32_t> m_visible;
        std::vector<RenderGeometry> m_view_geometry;
        Statistics m_statistics;
    };
}

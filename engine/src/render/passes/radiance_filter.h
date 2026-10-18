#pragma once

#include "graphics/result.h"
#include "render/scene/render_types.h"
#include <array>
#include <memory>
#include <vector>

namespace Comet {
    class Device;
    class FrameScheduler;
    class ImageView;
    class RenderPass;
    class RenderTarget;
    class Pipeline;
    class DescriptorSetLayout;
    class Sampler;
    struct SampledImageBinding;

    // App／Editor 共用的 GPU 辐亮度过滤；输入／输出均处于 Fragment SampledRead。
    class RadianceFilter {
    public:
        static Result<std::unique_ptr<RadianceFilter>, GraphicsError> create(Device& device);
        Result<void, GraphicsError> render(FrameScheduler& frames,
            const std::shared_ptr<ImageView>& color, const std::shared_ptr<ImageView>& guide,
            const std::shared_ptr<ImageView>& reprojection, const ViewProjectMatrix& camera,
            const ViewProjectMatrix& previous, float history_weight, bool valid);
        [[nodiscard]] std::shared_ptr<ImageView> output() const;
        void reset();

    private:
        explicit RadianceFilter(Device& device) : m_device(device) {}
        Device& m_device;
        std::shared_ptr<RenderPass> m_pass;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_sampler;
        struct History;
        std::shared_ptr<History> m_history;
        uint32_t m_index = 0;
        uint64_t m_serial = 0;
    };
}

#pragma once

#include "common/export.h"

#include "graphics/enums.h"
#include "graphics/result.h"

#include <memory>
#include <optional>

namespace Rml {
    class Context;
    class RenderInterface;
}

namespace Comet {
    class OverlayRecordContext;
    class Renderer;
    class RenderPass;
    class RenderTarget;
    struct SwapchainCompatibility;
}

namespace Comet::Ui {
    class COMET_API RmlRenderer final {
    public:
        struct Target {
            std::shared_ptr<Comet::RenderPass> pass;
            std::shared_ptr<Comet::RenderTarget> target;
            Comet::Format format = Comet::Format::UNDEFINED;
            Comet::ImageColorSpace color_space = Comet::ImageColorSpace::SrgbNonlinearKHR;
        };

        [[nodiscard]] static Comet::Result<std::unique_ptr<RmlRenderer>, Comet::GraphicsError>
        create(Comet::Renderer& renderer);
        ~RmlRenderer();

        [[nodiscard]] Rml::RenderInterface& interface();
        // 只读 CPU 绘制数据独立于文档与 Handle，保活几何和纹理像素。
        struct DrawData;
        using DrawSnapshot = std::shared_ptr<const DrawData>;
        [[nodiscard]] Comet::Result<DrawSnapshot, Comet::GraphicsError> collect(
            Rml::Context& context);
        // 收集候选文档、准备绘制资源并验证能力；不录制绘制命令。
        [[nodiscard]] Comet::Result<DrawSnapshot, Comet::GraphicsError> validate(
            Rml::Context& context);
        // GPU 阶段只消费快照，不读取 RmlUi 上下文或回入 DOM。
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> render(
            Comet::OverlayRecordContext& frame, const DrawData& draws);
        // 在场景最终输出后合成 UI，保留本帧离屏图像供视口采样。
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> render_offscreen(
            Comet::OverlayRecordContext& frame, const DrawData& draws);
        // 离屏目标使用当前 frame slot；调用方负责目标的初始/最终图像状态。
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> render_to_target(
            Comet::OverlayRecordContext& frame, const DrawData& draws, const Target& target);
        // RmlUi 回调不能返回 GraphicsError；加载候选时也可立即检查错误。
        [[nodiscard]] std::optional<Comet::GraphicsError> take_error();
        void release_swapchain_resources();
        void shutdown_gpu();
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> rebuild_swapchain_resources(
            const Comet::SwapchainCompatibility& compatibility);

    private:
        class Impl;
        explicit RmlRenderer(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> m_impl;
    };
}

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
        // 收集候选文档、准备绘制资源并验证能力；不录制绘制命令。
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> validate(Rml::Context& context);
        // 当前在宿主线程串行执行：收集 CPU 数据、准备 GPU 资源，再录制，不在 pass 内回入 DOM。
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> render(
            Comet::OverlayRecordContext& frame, Rml::Context& context);
        // 在场景最终输出后合成 UI，保留本帧离屏图像供视口采样。
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> render_offscreen(
            Comet::OverlayRecordContext& frame, Rml::Context& context);
        // 离屏目标使用当前 frame slot；调用方负责目标的初始/最终图像状态。
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> render_to_target(
            Comet::OverlayRecordContext& frame, Rml::Context& context, const Target& target);
        // RmlUi 回调不能返回 GraphicsError；加载候选时也可立即检查错误。
        [[nodiscard]] std::optional<Comet::GraphicsError> take_error();
        void release_swapchain_resources();
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> rebuild_swapchain_resources(
            const Comet::SwapchainCompatibility& compatibility);

    private:
        class Impl;
        explicit RmlRenderer(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> m_impl;
    };
}

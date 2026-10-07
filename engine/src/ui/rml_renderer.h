#pragma once

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
    class RmlRenderer final {
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
        // 完整生成候选文档资源并验证绘制能力；不录制 GPU 命令。
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> validate(Rml::Context& context);
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> render(
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

#pragma once

#include "common/error.h"
#include "common/result.h"
#include "graphics/result.h"
#include "input/input.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Rml {
    class Context;
    class ElementDocument;
}

namespace Comet {
    class Window;
    class Renderer;
    class OverlayRecordContext;
    struct SwapchainCompatibility;
}

namespace Comet::Ui {
    // 可选 RmlUi 后端。页面、数据绑定和业务行为由调用方拥有。
    class RmlContext final {
    public:
        struct FontFace {
            std::filesystem::path file;
            std::string family;
            bool fallback = false;
        };
        struct Options {
            std::filesystem::path resource_root;
            // 空目录使用引擎共用字体；fonts 为空时加载共用默认字体。
            std::filesystem::path font_directory;
            std::vector<FontFace> fonts;
        };
        using PrepareDocument = std::function<Result<void>(Rml::ElementDocument&)>;

        static Result<std::unique_ptr<RmlContext>, Error> create(
            Window& window, Renderer& renderer, Options options);
        ~RmlContext();
        RmlContext(const RmlContext&) = delete;
        RmlContext& operator=(const RmlContext&) = delete;

        // 后端扩展入口；Scene/Input/Renderer 公共接口无需包含 RmlUi。
        [[nodiscard]] Rml::Context& context();
        void process_input(const Input::Frame& input, bool modal_open);
        void set_capture_active(bool active);
        void cancel_input();
        void stop_dispatch_preserve_focus();
        [[nodiscard]] bool text_input_active() const;
        [[nodiscard]] bool pointer_blocked() const;
        [[nodiscard]] Result<void> update();

        // prepare 校验和准备页面，finalize 恢复最终呈现；任一阶段失败均保留当前页面。
        [[nodiscard]] Result<Rml::ElementDocument*> replace_document(Rml::ElementDocument* current,
            const std::filesystem::path& file, const PrepareDocument& prepare = {},
            const PrepareDocument& finalize = {});
        // 候选页面会触发 RmlUi 事件；业务回调在发布前应忽略这些事件。
        [[nodiscard]] bool is_loading_document() const;

        [[nodiscard]] Result<void, GraphicsError> render(OverlayRecordContext& frame);
        void release_swapchain_resources();
        [[nodiscard]] Result<void, GraphicsError> rebuild_swapchain_resources(
            const SwapchainCompatibility& compatibility);

    private:
        class Impl;
        explicit RmlContext(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> m_impl;
    };
}

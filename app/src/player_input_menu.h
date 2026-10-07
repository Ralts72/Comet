#pragma once

#include "common/error.h"
#include "common/result.h"
#include "graphics/result.h"
#include "input/player_input_edit.h"
#include "ui/rml_context.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace Comet {
    class Window;
    class Renderer;
    class OverlayRecordContext;
    struct SwapchainCompatibility;
}

namespace CometApp {
    class PlayerInputMenu final {
    public:
        using Options = Comet::Ui::RmlContext::Options;
        struct HudStats {
            float fps = 0;
            bool menu_available = true;
        };

        // 示例 app 的 HUD 与改键菜单；通用平台和绘制由 Engine UI 提供。
        static Comet::Result<std::unique_ptr<PlayerInputMenu>, Comet::Error> create(
            Comet::Window& window, Comet::Renderer& renderer, Options options);
        ~PlayerInputMenu();
        PlayerInputMenu(const PlayerInputMenu&) = delete;
        PlayerInputMenu& operator=(const PlayerInputMenu&) = delete;

        // 必须传 Window 本次发布的物理帧；关闭菜单当帧也返回 true。
        [[nodiscard]] bool frame(const Comet::Input::Frame& input, const HudStats& hud);
        [[nodiscard]] bool is_open() const;
        [[nodiscard]] bool pointer_blocked() const;
        [[nodiscard]] bool take_open_request();
        void open(const Comet::InputActions& defaults, const Comet::InputOverrides& current,
            std::span<const Comet::Input::Key> reserved_keys = {});
        void close();
        void show_error(std::string error);
        [[nodiscard]] std::optional<Comet::InputOverrides> take_request();
        void complete(const Comet::Result<void>& result);
        // 候选模板通过解析和必需元素校验后才替换；失败保留当前文档。
        [[nodiscard]] Comet::Result<void> reload_documents();

        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> render(
            Comet::OverlayRecordContext& context);
        void release_swapchain_resources();
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> rebuild_swapchain_resources(
            const Comet::SwapchainCompatibility& compatibility);

    private:
        class Impl;
        explicit PlayerInputMenu(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> m_impl;
    };
}

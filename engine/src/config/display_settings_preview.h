#pragma once

#include "config/player_display_settings.h"

#include <chrono>
#include <optional>

namespace Comet {
    // 显示试用不写磁盘；宿主在更新阶段处理期限，项目 UI 决定如何呈现确认。
    class COMET_API DisplaySettingsPreview final {
    public:
        using Clock = std::chrono::steady_clock;
        using Apply = std::function<Result<void>(const DisplaySettings&)>;
        explicit DisplaySettingsPreview(PlayerDisplaySettings& player) : m_player(player) {}
        DisplaySettingsPreview(const DisplaySettingsPreview&) = delete;
        DisplaySettingsPreview& operator=(const DisplaySettingsPreview&) = delete;

        [[nodiscard]] const DisplaySettings& settings() const;
        [[nodiscard]] bool is_pending() const { return m_preview.has_value(); }
        [[nodiscard]] std::optional<float> remaining(Clock::time_point now = Clock::now()) const;
        [[nodiscard]] Result<void> apply(DisplaySettings candidate, DisplaySettings current,
            const Apply& apply, Clock::time_point now = Clock::now());
        [[nodiscard]] Result<void> confirm(Clock::time_point now = Clock::now());
        [[nodiscard]] Result<void> revert(const Apply& apply);
        [[nodiscard]] Result<void> expire(const Apply& apply, Clock::time_point now = Clock::now());

    private:
        struct Preview {
            DisplaySettings previous;
            DisplaySettings candidate;
            Clock::time_point deadline;
        };
        PlayerDisplaySettings& m_player;
        std::optional<Preview> m_preview;
    };
}

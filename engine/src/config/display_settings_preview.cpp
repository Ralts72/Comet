#include "config/display_settings_preview.h"

#include <algorithm>

namespace Comet {
    const DisplaySettings& DisplaySettingsPreview::settings() const {
        if(m_preview)
            return m_preview->candidate;
        return m_player.settings();
    }

    std::optional<float> DisplaySettingsPreview::remaining(Clock::time_point now) const {
        if(!m_preview)
            return {};
        const auto seconds = std::chrono::duration<double>(m_preview->deadline - now).count();
        return static_cast<float>(std::max(0.0, seconds));
    }

    Result<void> DisplaySettingsPreview::apply(DisplaySettings candidate, DisplaySettings current,
        const Apply& apply, Clock::time_point now) {
        if(m_preview)
            return Result<void>::failure("Confirm or revert the current display preview first");
        if(!apply)
            return Result<void>::failure("Display apply service is unavailable");
        if(auto valid = candidate.validate(); !valid)
            return valid;
        if(auto valid = current.validate(); !valid)
            return valid;
        const bool changed = candidate.width != current.width || candidate.height != current.height
                             || candidate.mode != current.mode
                             || candidate.output.mode != current.output.mode;
        if(!changed)
            return m_player.save_and_apply(candidate, apply);
        if(auto applied = apply(candidate); !applied)
            return applied;
        m_preview = Preview{current, candidate, now + std::chrono::seconds(15)};
        return Result<void>::success();
    }

    Result<void> DisplaySettingsPreview::confirm(Clock::time_point now) {
        if(!m_preview)
            return Result<void>::failure("No display preview is pending");
        if(now >= m_preview->deadline)
            return Result<void>::failure("Display preview has expired");
        if(auto saved = m_player.save(m_preview->candidate); !saved)
            return saved;
        m_preview.reset();
        return Result<void>::success();
    }

    Result<void> DisplaySettingsPreview::revert(const Apply& apply) {
        if(!m_preview)
            return Result<void>::success();
        if(!apply)
            return Result<void>::failure("Display apply service is unavailable");
        if(auto applied = apply(m_preview->previous); !applied)
            return applied;
        m_preview.reset();
        return Result<void>::success();
    }

    Result<void> DisplaySettingsPreview::expire(const Apply& apply, Clock::time_point now) {
        if(m_preview && now >= m_preview->deadline)
            return revert(apply);
        return Result<void>::success();
    }
}

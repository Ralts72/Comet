#pragma once

namespace Comet {
    enum class AudioCategory { Effects, Music };

    [[nodiscard]] constexpr bool valid_audio_category(AudioCategory category) {
        return category == AudioCategory::Effects || category == AudioCategory::Music;
    }
}

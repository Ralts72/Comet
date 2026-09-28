#pragma once

#include "asset/handle.h"
#include "audio/audio.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <utility>

namespace Comet::Tests {
    inline const auto cue_path =
        std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/audio/play_chime.wav";
    inline constexpr AssetHandle cue_handle{8247160951280394421ULL};

    inline std::shared_ptr<AudioClip> load_cue() {
        auto clip = AudioClip::load(cue_path);
        EXPECT_TRUE(clip) << clip.error().message;
        if(!clip)
            return nullptr;
        return std::move(clip).value();
    }
}

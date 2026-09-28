#include "support/audio_fixture.h"

#include <algorithm>
#include <array>
#include <limits>

namespace Comet::Tests {
    TEST(AudioTest, DecodesProjectWavAndRejectsMissingFile) {
        const auto clip = load_cue();
        ASSERT_NE(clip, nullptr);
        EXPECT_EQ(clip->channels(), 1u);
        EXPECT_EQ(clip->sample_rate(), 44100u);
        EXPECT_GT(clip->frame_count(), 0u);
        EXPECT_EQ(clip->samples().size(), clip->frame_count() * clip->channels());
        EXPECT_FALSE(AudioClip::load(cue_path.parent_path() / "missing.wav"));
    }

    TEST(AudioTest, OfflineVoiceOwnsClipAndPlaybackAfterOwnerIsDestroyed) {
        auto playback = AudioPlayback::create(AudioPlayback::Mode::Offline);
        ASSERT_TRUE(playback) << playback.error().message;
        auto clip = load_cue();
        ASSERT_NE(clip, nullptr);
        EXPECT_FALSE(playback.value()->create_voice(clip, 1.1f, false));
        auto voice = playback.value()->create_voice(clip, 0.5f, false);
        ASSERT_TRUE(voice) << voice.error().message;
        clip.reset();
        playback.value().reset();
        auto started = voice.value()->start();
        ASSERT_TRUE(started) << started.error().message;
        voice.value()->set_volume(0.25f);
        voice.value()->set_looping(true);
        voice.value()->stop();
    }

    TEST(AudioTest, PausedPlaybackOutputsSilenceAndResumesAtTheSameSample) {
        auto reference = AudioPlayback::create(AudioPlayback::Mode::Offline);
        auto playback = AudioPlayback::create(AudioPlayback::Mode::Offline);
        ASSERT_TRUE(reference);
        ASSERT_TRUE(playback);
        const auto clip = load_cue();
        auto expected_voice = reference.value()->create_voice(clip, 0.5f, false);
        auto voice = playback.value()->create_voice(clip, 0.5f, false);
        ASSERT_TRUE(expected_voice);
        ASSERT_TRUE(voice);
        ASSERT_TRUE(expected_voice.value()->start());
        ASSERT_TRUE(voice.value()->start());
        std::array<float, 2048> expected{};
        std::array<float, 2048> actual{};
        ASSERT_TRUE(reference.value()->read_frames(expected));
        ASSERT_TRUE(playback.value()->read_frames(actual));
        EXPECT_EQ(actual, expected);
        ASSERT_TRUE(std::ranges::any_of(actual, [](float sample) { return sample != 0; }));

        ASSERT_TRUE(playback.value()->set_paused(true));
        ASSERT_TRUE(playback.value()->set_paused(true));
        for(int block = 0; block < 4; ++block) {
            actual.fill(1);
            ASSERT_TRUE(playback.value()->read_frames(actual));
            EXPECT_TRUE(std::ranges::all_of(actual, [](float sample) { return sample == 0; }));
        }
        // 仅暂停不改变 Voice 的启停状态。
        EXPECT_TRUE(voice.value()->is_playing());
        ASSERT_TRUE(playback.value()->set_paused(false));
        ASSERT_TRUE(reference.value()->read_frames(expected));
        ASSERT_TRUE(playback.value()->read_frames(actual));
        EXPECT_EQ(actual, expected);
        EXPECT_TRUE(std::ranges::any_of(actual, [](float sample) { return sample != 0; }));
        std::array<float, 3> incomplete_frame{};
        EXPECT_FALSE(playback.value()->read_frames(incomplete_frame));
        EXPECT_TRUE(playback.value()->read_frames({}));
    }

    TEST(AudioTest, VoicesCreatedWhilePausedWaitAndExplicitlyStoppedVoicesStayStopped) {
        auto reference = AudioPlayback::create(AudioPlayback::Mode::Offline);
        auto playback = AudioPlayback::create(AudioPlayback::Mode::Offline);
        ASSERT_TRUE(reference);
        ASSERT_TRUE(playback);
        ASSERT_TRUE(playback.value()->set_paused(true));
        const auto clip = load_cue();
        auto expected_voice = reference.value()->create_voice(clip, 0.5f, true);
        auto voice = playback.value()->create_voice(clip, 0.5f, true);
        auto stopped = playback.value()->create_voice(clip, 0.5f, false);
        ASSERT_TRUE(expected_voice);
        ASSERT_TRUE(voice);
        ASSERT_TRUE(stopped);
        ASSERT_TRUE(voice.value()->start());
        ASSERT_TRUE(stopped.value()->start());
        stopped.value()->stop();
        std::array<float, 2048> expected{};
        std::array<float, 2048> actual{};
        ASSERT_TRUE(playback.value()->read_frames(actual));
        EXPECT_TRUE(std::ranges::all_of(actual, [](float sample) { return sample == 0; }));
        EXPECT_TRUE(voice.value()->is_playing());
        EXPECT_FALSE(stopped.value()->is_playing());
        ASSERT_TRUE(playback.value()->set_paused(false));
        ASSERT_TRUE(expected_voice.value()->start());
        ASSERT_TRUE(reference.value()->read_frames(expected));
        ASSERT_TRUE(playback.value()->read_frames(actual));
        EXPECT_EQ(actual, expected);
        EXPECT_FALSE(stopped.value()->is_playing());
        EXPECT_TRUE(std::ranges::any_of(actual, [](float sample) { return sample != 0; }));
    }

    TEST(AudioTest, ResumeDoesNotRestartACompletedOneShot) {
        auto playback = AudioPlayback::create(AudioPlayback::Mode::Offline);
        ASSERT_TRUE(playback);
        auto voice = playback.value()->create_voice(load_cue(), 0.5f, false);
        ASSERT_TRUE(voice);
        ASSERT_TRUE(voice.value()->start());
        std::array<float, 2048> output{};
        int blocks = 0;
        while(voice.value()->is_playing() && blocks++ < 1000)
            ASSERT_TRUE(playback.value()->read_frames(output));
        ASSERT_FALSE(voice.value()->is_playing());
        ASSERT_TRUE(playback.value()->set_paused(true));
        ASSERT_TRUE(playback.value()->set_paused(false));
        ASSERT_TRUE(playback.value()->read_frames(output));
        EXPECT_FALSE(voice.value()->is_playing());
        EXPECT_TRUE(std::ranges::all_of(output, [](float sample) { return sample == 0; }));
    }

    TEST(AudioTest, SilentStepsPreserveFractionalFramesAndResumeAtTheAdvancedSample) {
        auto reference = AudioPlayback::create(AudioPlayback::Mode::Offline);
        auto playback = AudioPlayback::create(AudioPlayback::Mode::Offline);
        ASSERT_TRUE(reference);
        ASSERT_TRUE(playback);
        const auto clip = load_cue();
        auto expected_voice = reference.value()->create_voice(clip, 0.5f, true);
        auto voice = playback.value()->create_voice(clip, 0.5f, true);
        ASSERT_TRUE(expected_voice);
        ASSERT_TRUE(voice);
        ASSERT_TRUE(expected_voice.value()->start());
        ASSERT_TRUE(voice.value()->start());
        std::array<float, 2048> expected{};
        std::array<float, 2048> actual{};
        EXPECT_FALSE(playback.value()->advance_silently(0.01));
        ASSERT_TRUE(reference.value()->read_frames(expected));
        ASSERT_TRUE(playback.value()->read_frames(actual));
        EXPECT_EQ(actual, expected);
        ASSERT_TRUE(playback.value()->set_paused(true));
        EXPECT_FALSE(playback.value()->advance_silently(-0.01));
        EXPECT_FALSE(playback.value()->advance_silently(2));
        EXPECT_FALSE(playback.value()->advance_silently(std::numeric_limits<double>::infinity()));
        EXPECT_FALSE(playback.value()->advance_silently(std::numeric_limits<double>::quiet_NaN()));
        ASSERT_TRUE(playback.value()->advance_silently(0));
        constexpr double step = 1.0 / 144;
        constexpr int steps = 31;
        for(int index = 0; index < steps; ++index) {
            ASSERT_TRUE(playback.value()->advance_silently(step));
            actual.fill(1);
            ASSERT_TRUE(playback.value()->read_frames(actual));
            EXPECT_TRUE(std::ranges::all_of(actual, [](float sample) { return sample == 0; }));
        }
        const auto frames = static_cast<size_t>(step * steps * 48000);
        std::vector<float> discarded(frames * 2);
        ASSERT_TRUE(reference.value()->read_frames(discarded));
        ASSERT_TRUE(playback.value()->set_paused(false));
        ASSERT_TRUE(reference.value()->read_frames(expected));
        ASSERT_TRUE(playback.value()->read_frames(actual));
        EXPECT_EQ(actual, expected);
        EXPECT_TRUE(std::ranges::any_of(actual, [](float sample) { return sample != 0; }));
    }

    TEST(AudioTest, SilentAdvanceFinishesANewOneShotWithoutReplayingItOnResume) {
        auto playback = AudioPlayback::create(AudioPlayback::Mode::Offline, true);
        ASSERT_TRUE(playback);
        auto voice = playback.value()->create_voice(load_cue(), 0.5f, false);
        ASSERT_TRUE(voice);
        ASSERT_TRUE(voice.value()->start());
        ASSERT_TRUE(playback.value()->advance_silently(1));
        ASSERT_FALSE(voice.value()->is_playing());
        ASSERT_TRUE(playback.value()->set_paused(false));
        std::array<float, 2048> output{};
        ASSERT_TRUE(playback.value()->read_frames(output));
        EXPECT_FALSE(voice.value()->is_playing());
        EXPECT_TRUE(std::ranges::all_of(output, [](float sample) { return sample == 0; }));
    }

}

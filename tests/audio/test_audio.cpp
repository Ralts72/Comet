#include "support/audio_fixture.h"
#include "support/temporary_directory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>

namespace Comet::Tests {
    namespace {
        void write_test_wave(const std::filesystem::path& path, std::uint32_t frames) {
            std::ofstream output(path, std::ios::binary);
            const auto number = [&](std::uint32_t value, unsigned bytes) {
                for(unsigned byte = 0; byte < bytes; ++byte)
                    output.put(static_cast<char>(value >> (byte * 8)));
            };
            output.write("RIFF", 4);
            number(36 + frames * 2, 4);
            output.write("WAVEfmt ", 8);
            number(16, 4);
            number(1, 2);
            number(1, 2);
            number(48000, 4);
            number(96000, 4);
            number(2, 2);
            number(16, 2);
            output.write("data", 4);
            number(frames * 2, 4);
            if(frames < 48000) {
                for(std::uint32_t frame = 0; frame < frames; ++frame)
                    number(static_cast<std::uint16_t>(static_cast<std::int16_t>(
                               4000 * std::sin(2 * Math::PI * 480 * frame / 48000))),
                        2);
            }
            output.close();
            std::filesystem::resize_file(path, 44 + std::uint64_t(frames) * 2);
        }

        double energy(std::span<const float> samples, std::size_t channel) {
            double result = 0;
            for(std::size_t index = channel; index < samples.size(); index += 2)
                result += double(samples[index]) * samples[index];
            return result;
        }
    }

    TEST(AudioTest, StreamingKeepsMetadataAndMatchesDecodedPlaybackAcrossPauseAndLoop) {
        TemporaryDirectory directory;
        const auto path = directory.path() / "tone.wav";
        write_test_wave(path, 4800);
        auto pcm = AudioClip::load(path).value();
        auto clip = AudioClip::load(path, true);
        ASSERT_TRUE(clip);
        EXPECT_TRUE(clip.value()->is_streamed());
        EXPECT_TRUE(clip.value()->samples().empty());
        EXPECT_EQ(clip.value()->frame_count(), pcm->frame_count());
        auto streamed = AudioPlayback::create(AudioPlayback::Mode::Offline);
        auto decoded = AudioPlayback::create(AudioPlayback::Mode::Offline);
        ASSERT_TRUE(streamed);
        ASSERT_TRUE(decoded);
        auto stream_voice =
            streamed.value()->create_voice(clip.value(), 0.5f, true, AudioCategory::Music);
        auto pcm_voice = decoded.value()->create_voice(pcm, 0.5f, true, AudioCategory::Music);
        ASSERT_TRUE(stream_voice) << stream_voice.error().message;
        ASSERT_TRUE(pcm_voice);
        ASSERT_TRUE(stream_voice.value()->start());
        ASSERT_TRUE(pcm_voice.value()->start());
        std::array<float, 960> actual{}, expected{};
        for(int block = 0; block < 40; ++block) {
            ASSERT_TRUE(streamed.value()->read_frames(actual));
            ASSERT_TRUE(decoded.value()->read_frames(expected));
            float difference = 0;
            for(std::size_t index = 0; index < actual.size(); ++index)
                difference = std::max(difference, std::abs(actual[index] - expected[index]));
            EXPECT_LE(difference, 1e-6f) << block;
        }
        ASSERT_TRUE(streamed.value()->set_paused(true));
        ASSERT_TRUE(streamed.value()->read_frames(actual));
        EXPECT_EQ(energy(actual, 0), 0);
        ASSERT_TRUE(streamed.value()->set_paused(false));
        ASSERT_TRUE(streamed.value()->read_frames(actual));
        ASSERT_TRUE(decoded.value()->read_frames(expected));
        EXPECT_EQ(actual, expected);
        stream_voice.value()->stop();
        EXPECT_FALSE(stream_voice.value()->is_playing());
    }

    TEST(AudioTest, StreamedLongMusicDoesNotRequireAllocatingTheWholeDecodedClip) {
        TemporaryDirectory directory;
        const auto path = directory.path() / "long.wav";
        const std::uint32_t frames = 16'000'001;
        write_test_wave(path, frames);
        EXPECT_FALSE(AudioClip::load(path));
        auto streamed = AudioClip::load(path, true);
        ASSERT_TRUE(streamed) << streamed.error().message;
        EXPECT_EQ(streamed.value()->frame_count(), frames);
        EXPECT_TRUE(streamed.value()->samples().empty());
    }

    TEST(AudioTest, SpatialAudioChangesStereoDirectionAndAttenuatesWithDistance) {
        const auto sample = [&](Math::Vec3 position, AudioListenerPose listener = {}) {
            auto playback = AudioPlayback::create(AudioPlayback::Mode::Offline).value();
            EXPECT_TRUE(playback->set_listener(listener));
            auto voice = playback
                             ->create_voice(load_cue(), 1, true, AudioCategory::Effects,
                                 {.enabled = true,
                                     .position = position,
                                     .min_distance = 1,
                                     .max_distance = 10})
                             .value();
            EXPECT_TRUE(voice->start());
            std::array<float, 2048> samples{};
            EXPECT_TRUE(playback->read_frames(samples));
            return std::pair{energy(samples, 0), energy(samples, 1)};
        };
        const auto left = sample({-1, 0, -1});
        const auto right = sample({1, 0, -1});
        EXPECT_GT(left.first, left.second);
        EXPECT_GT(right.second, right.first);
        EXPECT_NEAR(left.first, right.second, 1e-6);
        const auto near = sample({0, 0, -1});
        const auto far = sample({0, 0, -7});
        EXPECT_GT(near.first + near.second, far.first + far.second);
        const auto beyond = sample({0, 0, -20});
        EXPECT_NEAR(beyond.first + beyond.second, 0, 1e-8);
        const auto rotated = sample({1, 0, -1}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 0}});
        EXPECT_GT(rotated.first, rotated.second);
        EXPECT_FALSE((AudioSpatialSettings{.min_distance = 5, .max_distance = 2}.validate()));
        auto playback = AudioPlayback::create(AudioPlayback::Mode::Offline).value();
        EXPECT_FALSE(playback->set_listener({{0, 0, 0}, {0, 0, 0}, {0, 1, 0}}));
    }

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

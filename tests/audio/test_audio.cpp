#include "asset/registry.h"
#include "audio/audio.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/systems/audio_system.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>

namespace Comet::Tests {
    namespace {
        const auto cue_path =
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/audio/play_chime.wav";
        constexpr AssetHandle cue_handle{8247160951280394421ULL};

        std::shared_ptr<AudioClip> load_cue() {
            auto clip = AudioClip::load(cue_path);
            EXPECT_TRUE(clip) << clip.error().message;
            return clip ? std::move(clip).value() : nullptr;
        }

        class RequestOneShot final: public System {
        public:
            explicit RequestOneShot(Entity source) : m_source(source) {}

            Result<void, Error> on_start(Scene&) override {
                m_requested = false;
                return Result<void, Error>::success();
            }

            Result<void, Error> update(Scene& scene, const Context&) override {
                if(m_requested)
                    return Result<void, Error>::success();
                m_requested = true;
                if(!scene.request_play_one_shot(m_source)
                    || !scene.request_destroy_entity(m_source))
                    return Result<void, Error>::failure({"Cannot request one-shot cue"});
                return Result<void, Error>::success();
            }

        private:
            Entity m_source;
            bool m_requested = false;
        };
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

    TEST(AudioSystemTest, SceneSourceStartsStopsAndCanBeReplaced) {
        AssetRegistry assets;
        ASSERT_TRUE(assets.register_asset(cue_handle, load_cue()));
        Scene scene;
        auto entity = scene.create_entity("Sound");
        auto& source = entity.add_component<AudioSourceComponent>();
        source.clip = cue_handle;
        source.volume = 0.5f;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AudioSystem>(assets, AudioPlayback::Mode::Offline)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        source.loop = true;
        source.volume = 0.25f;
        ASSERT_TRUE(runtime.advance(0));
        entity.remove_component<AudioSourceComponent>();
        entity.add_component<AudioSourceComponent>().clip = cue_handle;
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(AudioSystemTest, MissingAssetFailsStartWithoutLeavingRuntimeActive) {
        AssetRegistry assets;
        Scene scene;
        scene.create_entity().add_component<AudioSourceComponent>().clip = cue_handle;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AudioSystem>(assets, AudioPlayback::Mode::Offline)));
        EXPECT_FALSE(runtime.start(scene));
        EXPECT_FALSE(runtime.is_active());
        ASSERT_TRUE(assets.register_asset(cue_handle, load_cue()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(AudioSystemTest, ProcessesOneShotBeforeSourceDestruction) {
        AssetRegistry assets;
        Scene scene;
        auto entity = scene.create_entity("Cue");
        auto& source = entity.add_component<AudioSourceComponent>();
        source.clip = cue_handle;
        source.play_on_start = false;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<RequestOneShot>(entity)));
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AudioSystem>(assets, AudioPlayback::Mode::Offline)));

        ASSERT_TRUE(runtime.start(scene));
        const auto missing = runtime.advance(0);
        ASSERT_FALSE(missing);
        EXPECT_NE(missing.error().message.find("Audio clip is unavailable"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_TRUE(entity);

        ASSERT_TRUE(assets.register_asset(cue_handle, load_cue()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FALSE(entity);
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(AudioSystemTest, FirstSoundCanBeCreatedDuringPausedStepAndStoppedBeforeResume) {
        AssetRegistry assets;
        ASSERT_TRUE(assets.register_asset(cue_handle, load_cue()));
        Scene scene;
        auto entity = scene.create_entity("Cue");
        auto& source = entity.add_component<AudioSourceComponent>();
        source.clip = cue_handle;
        source.play_on_start = false;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(std::make_unique<RequestOneShot>(entity)));
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AudioSystem>(assets, AudioPlayback::Mode::Offline)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_TRUE(entity);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FALSE(entity);
        EXPECT_EQ(runtime.get_state(), SceneRuntime::State::Paused);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(AudioSystemTest, SingleStepsReleaseExpiredOneShotsBeforeResuming) {
        AssetRegistry assets;
        auto clip = load_cue();
        ASSERT_NE(clip, nullptr);
        const std::weak_ptr<AudioClip> observed_clip = clip;
        ASSERT_TRUE(assets.register_asset(cue_handle, std::move(clip)));
        auto reference = AudioPlayback::create(AudioPlayback::Mode::Offline);
        ASSERT_TRUE(reference);
        auto reference_voice = reference.value()->create_voice(load_cue(), 0.5f, false);
        ASSERT_TRUE(reference_voice);
        ASSERT_TRUE(reference_voice.value()->start());
        Scene scene;
        auto entity = scene.create_entity("Cue");
        auto& source = entity.add_component<AudioSourceComponent>();
        source.clip = cue_handle;
        source.play_on_start = false;
        SceneRuntime runtime;
        constexpr double step = 0.01;
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = step}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<RequestOneShot>(entity)));
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AudioSystem>(assets, AudioPlayback::Mode::Offline)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FALSE(entity);
        ASSERT_TRUE(assets.unregister_asset(cue_handle));
        // 场景与缓存均不再保活片段，只有本步末新建的 Voice 持有它。
        EXPECT_FALSE(observed_clip.expired());
        ASSERT_TRUE(runtime.advance(5));
        EXPECT_FALSE(observed_clip.expired());
        // 与相同步长的实际混音比较，包含重采样器的尾部缓冲。
        std::array<float, 960> output{};
        for(int index = 0; index < 200 && reference_voice.value()->is_playing(); ++index) {
            ASSERT_TRUE(reference.value()->read_frames(output));
            ASSERT_TRUE(runtime.request_step());
            ASSERT_TRUE(runtime.advance(0));
            EXPECT_EQ(observed_clip.expired(), !reference_voice.value()->is_playing());
        }
        EXPECT_FALSE(reference_voice.value()->is_playing());
        EXPECT_TRUE(observed_clip.expired());
        EXPECT_EQ(runtime.get_state(), SceneRuntime::State::Paused);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        ASSERT_TRUE(runtime.advance(step));
        EXPECT_TRUE(observed_clip.expired());
        ASSERT_TRUE(runtime.stop());
    }

    TEST(AudioSystemTest, DemoSceneSerializesAudioReference) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto loaded = serializer.load(
            (std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/scenes/default.scene")
                .string());
        ASSERT_TRUE(loaded) << loaded.error();
        ASSERT_EQ(loaded.value()->component_count<AudioSourceComponent>(), 1u);
        const auto goal_uuid = EntityUuid::parse("672cd0cc-501f-419e-af5e-a883a0cd3d07");
        ASSERT_TRUE(goal_uuid);
        const auto goal = loaded.value()->find_entity(*goal_uuid);
        ASSERT_TRUE(goal);
        EXPECT_FALSE(goal.get_component<AudioSourceComponent>().play_on_start);
        auto clone = serializer.clone(*loaded.value());
        ASSERT_TRUE(clone) << clone.error();
        EXPECT_EQ(clone.value()->component_count<AudioSourceComponent>(), 1u);
        EXPECT_FALSE(clone.value()
                ->find_entity(goal.get_uuid())
                .get_component<AudioSourceComponent>()
                .play_on_start);
    }
}

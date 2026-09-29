#include "asset/registry.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/systems/audio_system.h"
#include "support/audio_fixture.h"

#include <array>

namespace Comet::Tests {
    namespace {
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
        EXPECT_FALSE(runtime.start(scene, SceneRuntime::State::Paused));
        EXPECT_FALSE(runtime.is_active());
        ASSERT_TRUE(assets.register_asset(cue_handle, load_cue()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.stop());
    }

    TEST(AudioSystemTest, PausedStartKeepsAutomaticPlaybackReadyForSilentSteps) {
        AssetRegistry assets;
        ASSERT_TRUE(assets.register_asset(cue_handle, load_cue()));
        Scene scene;
        scene.create_entity("Sound").add_component<AudioSourceComponent>().clip = cue_handle;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AudioSystem>(assets, AudioPlayback::Mode::Offline)));
        ASSERT_TRUE(runtime.start(scene, SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(10));
        EXPECT_EQ(runtime.get_timing().frame_index, 0);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(runtime.get_state(), SceneRuntime::State::Paused);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        ASSERT_TRUE(runtime.advance(0.01));
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

}

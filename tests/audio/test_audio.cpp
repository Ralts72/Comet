#include "asset/registry.h"
#include "audio/audio.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/systems/audio_system.h"

#include <gtest/gtest.h>

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
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
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
        EXPECT_FALSE(clone.value()->find_entity(goal.get_uuid())
                         .get_component<AudioSourceComponent>().play_on_start);
    }
}

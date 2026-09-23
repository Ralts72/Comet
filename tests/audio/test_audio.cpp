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

    TEST(AudioSystemTest, DemoSceneSerializesAudioReference) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto loaded = serializer.load(
            (std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/scenes/default.scene")
                .string());
        ASSERT_TRUE(loaded) << loaded.error();
        ASSERT_EQ(loaded.value()->component_count<AudioSourceComponent>(), 1u);
        auto clone = serializer.clone(*loaded.value());
        ASSERT_TRUE(clone) << clone.error();
        EXPECT_EQ(clone.value()->component_count<AudioSourceComponent>(), 1u);
    }
}

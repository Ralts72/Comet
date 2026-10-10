#include "scripting/script_instance.h"
#include "audio/audio_service.h"
#include "asset/registry.h"
#include "common/scope_exit.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/script_component.h"
#include "scene/systems/audio_system.h"
#include "scene/systems/script_system.h"
#include "support/audio_fixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace Comet::Tests {
    namespace {
        using Samples = std::array<float, 960>;

        void expect_silence(const Samples& samples) {
            EXPECT_TRUE(std::ranges::all_of(samples, [](float value) { return value == 0; }));
        }

        void expect_sound(const Samples& samples) {
            EXPECT_TRUE(
                std::ranges::any_of(samples, [](float value) { return std::abs(value) > 1e-6f; }));
        }

        class FailAfterRequest final: public System {
        public:
            Result<void, Error> on_start(
                Scene&, RuntimeSession&, const RuntimeServices& services) override {
                if(!std::exchange(m_fail, false))
                    return Result<void, Error>::success();
                EXPECT_TRUE(services.audio->request_one_shot(cue_handle, 1));
                return Result<void, Error>::failure({"Startup failed after audio request"});
            }

        private:
            bool m_fail = true;
        };
    }

    class AudioServiceTest: public testing::Test {
    protected:
        AssetRegistry assets;
        Scene scene;
        AudioService audio{assets, AudioPlayback::Mode::Offline};
        SceneRuntime runtime;

        void SetUp() override {
            ASSERT_TRUE(assets.register_asset(cue_handle, load_cue()));
            ASSERT_TRUE(runtime.set_services({.audio = &audio}));
        }

        void add_audio_system() {
            ASSERT_TRUE(runtime.add_system(std::make_unique<AudioSystem>(audio)));
        }
        void expect_cue_volume(const float volume) {
            auto reference = AudioPlayback::create(AudioPlayback::Mode::Offline);
            ASSERT_TRUE(reference);
            auto voice = reference.value()->create_voice(load_cue(), volume, false);
            ASSERT_TRUE(voice);
            ASSERT_TRUE(voice.value()->start());
            Samples expected{}, actual{};
            ASSERT_TRUE(reference.value()->read_frames(expected));
            ASSERT_TRUE(audio.read_frames(actual));
            expect_sound(actual);
            for(std::size_t index = 0; index < actual.size(); ++index)
                EXPECT_NEAR(actual[index], expected[index], 1e-6f) << index;
        }
    };

    TEST_F(AudioServiceTest, CategoryGainsAffectLiveVoicesAndOneShotsWithoutResettingPosition) {
        auto entity = scene.create_entity("Music");
        auto& source = entity.add_component<AudioSourceComponent>();
        source.clip = cue_handle;
        source.volume = 0.4f;
        source.loop = true;
        source.category = AudioCategory::Music;
        ASSERT_TRUE(audio.apply_settings({0.5f, 0, 0.5f}));
        add_audio_system();
        ASSERT_TRUE(runtime.start(scene));
        auto reference = AudioPlayback::create(AudioPlayback::Mode::Offline);
        ASSERT_TRUE(reference);
        auto voice = reference.value()->create_voice(load_cue(), 0.4f, true);
        ASSERT_TRUE(voice);
        ASSERT_TRUE(voice.value()->start());
        Samples expected{}, actual{};
        const auto compare = [&](float gain) {
            ASSERT_TRUE(reference.value()->read_frames(expected));
            ASSERT_TRUE(audio.read_frames(actual));
            for(std::size_t index = 0; index < actual.size(); ++index)
                EXPECT_NEAR(actual[index], expected[index] * gain, 1e-6f) << index;
        };
        compare(0.25f);
        ASSERT_TRUE(audio.request_one_shot(cue_handle, 1)); // Effects 通道已静音。
        ASSERT_TRUE(runtime.advance(0));
        compare(0.25f);
        ASSERT_TRUE(audio.apply_settings({0, 1, 1}));
        compare(0);
        ASSERT_TRUE(audio.apply_settings({1, 0, 1}));
        compare(1); // 恢复音量继续同一播放位置。
        source.category = AudioCategory::Effects;
        ASSERT_TRUE(runtime.advance(0));
        compare(0);
        source.category = AudioCategory::Music;
        ASSERT_TRUE(runtime.advance(0));
        compare(1);
        EXPECT_FALSE(audio.apply_settings({1, -1, 1}));
        EXPECT_EQ(audio.settings(), (AudioSettings{1, 0, 1}));
        EXPECT_FALSE(audio.request_one_shot(cue_handle, 1, static_cast<AudioCategory>(99)));
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(audio.settings(), (AudioSettings{1, 0, 1}));
    }

    TEST_F(AudioServiceTest, LuaCanPlayDuringStartupBeforeAudioSystemStartsAndEntityIsDestroyed) {
        auto script = Script::create(R"(
            return {on_start = function()
                comet.play_one_shot()
                comet.destroy_entity(comet.self_entity())
            end}
        )");
        ASSERT_TRUE(script) << script.error().message;
        constexpr AssetHandle script_handle{42};
        ASSERT_TRUE(assets.register_asset(script_handle, script.value()));
        auto entity = scene.create_entity("Startup cue");
        entity.add_component<ScriptComponent>().asset = script_handle;
        auto& source = entity.add_component<AudioSourceComponent>();
        source.clip = cue_handle;
        source.volume = 0.25f;
        source.category = AudioCategory::Music;
        source.play_on_start = false;
        ASSERT_TRUE(audio.apply_settings({0.5f, 0, 0.5f}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        add_audio_system();
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(entity);
        expect_cue_volume(0.0625f);
    }

    TEST_F(AudioServiceTest, RequestsKeepTheSubmittedClipAndVolumeSnapshot) {
        auto entity = scene.create_entity("Cue");
        auto& source = entity.add_component<AudioSourceComponent>();
        source.clip = cue_handle;
        source.volume = 0.25f;
        source.play_on_start = false;
        add_audio_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(audio.request_one_shot(source.clip, source.volume));
        source.clip = AssetHandle{999};
        source.volume = 1;
        ASSERT_TRUE(scene.request_destroy_entity(entity));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FALSE(entity);
        expect_cue_volume(0.25f);
    }

    TEST_F(AudioServiceTest, FailedStartupBeforeAudioSystemDoesNotReplayQueuedSounds) {
        ASSERT_TRUE(runtime.add_system(std::make_unique<FailAfterRequest>()));
        add_audio_system();
        EXPECT_FALSE(runtime.start(scene));
        EXPECT_FALSE(audio.is_bound_to(scene));
        EXPECT_FALSE(audio.request_one_shot(cue_handle, 1));
        ASSERT_TRUE(runtime.start(scene));
        Samples actual{};
        ASSERT_TRUE(audio.read_frames(actual));
        expect_silence(actual);
        ASSERT_TRUE(audio.request_one_shot(cue_handle, 1));
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(audio.read_frames(actual));
        expect_sound(actual);
    }

    TEST_F(AudioServiceTest, RuntimeDomainsMixAndStopIndependently) {
        Scene other_scene;
        AudioService other_audio(assets, AudioPlayback::Mode::Offline);
        SceneRuntime other_runtime;
        ASSERT_TRUE(other_runtime.set_services({.audio = &other_audio}));
        ASSERT_TRUE(other_runtime.add_system(std::make_unique<AudioSystem>(other_audio)));
        add_audio_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(other_runtime.start(other_scene));
        ASSERT_TRUE(audio.request_one_shot(cue_handle, 1));
        ASSERT_TRUE(other_audio.request_one_shot(cue_handle, 0.5f));
        ASSERT_TRUE(runtime.advance(0));
        Samples first{}, second{};
        ASSERT_TRUE(other_audio.read_frames(second));
        expect_silence(second);
        ASSERT_TRUE(other_runtime.advance(0));
        ASSERT_TRUE(audio.read_frames(first));
        ASSERT_TRUE(other_audio.read_frames(second));
        expect_sound(first);
        for(std::size_t index = 0; index < first.size(); ++index)
            EXPECT_NEAR(second[index], first[index] * 0.5f, 1e-6f) << index;
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(audio.is_bound_to(scene));
        EXPECT_TRUE(other_audio.is_bound_to(other_scene));
        ASSERT_TRUE(audio.read_frames(first));
        expect_silence(first);
        ASSERT_TRUE(other_audio.read_frames(second));
        expect_sound(second);
    }

    TEST_F(AudioServiceTest, SharingAnActiveServiceFailsWithoutInterruptingItsOwner) {
        add_audio_system();
        ASSERT_TRUE(runtime.start(scene));
        {
            Scene other_scene;
            SceneRuntime other_runtime;
            ASSERT_TRUE(other_runtime.set_services({.audio = &audio}));
            EXPECT_FALSE(other_runtime.start(other_scene, SceneRuntime::State::Paused));
            EXPECT_FALSE(other_runtime.is_active());
            EXPECT_TRUE(audio.is_bound_to(scene));
            ASSERT_TRUE(other_runtime.stop());
        }
        EXPECT_TRUE(audio.is_bound_to(scene));
        EXPECT_FALSE(runtime.set_services({}));
        ASSERT_TRUE(audio.request_one_shot(cue_handle, 1));
        ASSERT_TRUE(runtime.advance(0));
        Samples actual{};
        ASSERT_TRUE(audio.read_frames(actual));
        expect_sound(actual);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.set_services({}));
    }

    TEST_F(AudioServiceTest, InvalidRequestsAndQueueOverflowCannotLeakIntoTheNextRun) {
        add_audio_system();
        EXPECT_FALSE(audio.request_one_shot(cue_handle, 1));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(audio.request_one_shot({}, 1));
        for(const auto volume : {-1.0f, 1.1f, std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::quiet_NaN()})
            EXPECT_FALSE(audio.request_one_shot(cue_handle, volume));
        for(int index = 0; index < 128; ++index)
            ASSERT_TRUE(audio.request_one_shot(cue_handle, 1));
        EXPECT_FALSE(audio.request_one_shot(cue_handle, 1));
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        Samples actual{};
        ASSERT_TRUE(audio.read_frames(actual));
        expect_silence(actual);
        ASSERT_TRUE(audio.request_one_shot(cue_handle, 1));
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(audio.read_frames(actual));
        expect_sound(actual);
    }

    TEST_F(AudioServiceTest, AudioSystemMustUseTheServiceInjectedIntoItsRuntime) {
        AudioService other_audio(assets, AudioPlayback::Mode::Offline);
        ASSERT_TRUE(runtime.add_system(std::make_unique<AudioSystem>(other_audio)));
        const auto result = runtime.start(scene);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("RuntimeServices"), std::string::npos);
        EXPECT_FALSE(audio.is_bound_to(scene));
        EXPECT_FALSE(other_audio.is_bound_to(scene));
    }

    TEST_F(AudioServiceTest, LuaAudioCapabilityIsBoundToItsSceneAndCurrentInvocation) {
        auto script = Script::create(R"(
            return {
                update = function()
                    comet.play_one_shot()
                    comet.translate(1, 0, 0)
                end,
                on_stop = function() comet.play_one_shot() end
            }
        )");
        ASSERT_TRUE(script);
        auto instance = ScriptInstance::create(*script.value());
        ASSERT_TRUE(instance);
        auto entity = scene.create_entity("Cue");
        auto& source = entity.add_component<AudioSourceComponent>();
        source.clip = cue_handle;
        source.play_on_start = false;
        add_audio_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(instance.value()->invoke(
            ScriptInstance::Phase::Update, entity, {}, {.scene = &scene, .audio = &audio}));
        const auto missing =
            instance.value()->invoke(ScriptInstance::Phase::Update, entity, {}, {.scene = &scene});
        ASSERT_FALSE(missing);
        EXPECT_NE(missing.error().message.find("Audio service is unavailable"), std::string::npos);
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().translation.x, 1);
        Scene other_scene;
        auto other_entity = other_scene.create_entity("Other");
        other_entity.add_component<AudioSourceComponent>().clip = cue_handle;
        const auto wrong_scene = instance.value()->invoke(ScriptInstance::Phase::Update,
            other_entity, {}, {.scene = &other_scene, .audio = &audio});
        ASSERT_FALSE(wrong_scene);
        EXPECT_NE(wrong_scene.error().message.find("another scene"), std::string::npos);
        EXPECT_FLOAT_EQ(other_entity.get_component<TransformComponent>().translation.x, 0);
        const auto stopped = instance.value()->invoke(
            ScriptInstance::Phase::Stop, entity, {}, {.scene = &scene, .audio = &audio});
        ASSERT_FALSE(stopped);
        EXPECT_NE(stopped.error().message.find("valid Audio Source"), std::string::npos);
        ASSERT_TRUE(runtime.stop());
        const auto inactive = instance.value()->invoke(
            ScriptInstance::Phase::Update, entity, {}, {.scene = &scene, .audio = &audio});
        ASSERT_FALSE(inactive);
        EXPECT_NE(inactive.error().message.find("inactive"), std::string::npos);
    }

    TEST_F(AudioServiceTest, ScriptSystemReportsMissingAudioServiceWithAnAuthoredSource) {
        auto script = Script::create("return {update = function() comet.play_one_shot() end}");
        ASSERT_TRUE(script);
        constexpr AssetHandle script_handle{42};
        ASSERT_TRUE(assets.register_asset(script_handle, script.value()));
        auto entity = scene.create_entity("Cue");
        entity.add_component<ScriptComponent>().asset = script_handle;
        entity.add_component<AudioSourceComponent>().clip = cue_handle;
        ASSERT_TRUE(runtime.set_services({}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        ASSERT_TRUE(runtime.start(scene));
        const auto result = runtime.advance(0);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("Audio service is unavailable"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
    }

    TEST_F(AudioServiceTest, RealtimeOutputFailureIsLazyAndRetriedOnlyForANewRun) {
        int attempts = 0;
        AudioService unavailable(
            assets, AudioPlayback::Mode::Realtime, [&](AudioPlayback::Mode mode, bool) {
                EXPECT_EQ(mode, AudioPlayback::Mode::Realtime);
                ++attempts;
                return Result<std::unique_ptr<AudioPlayback>, Error>::failure({"No audio device"});
            });
        const ScopeExit stop([&] { EXPECT_TRUE(runtime.stop()); });
        ASSERT_TRUE(runtime.set_services({.audio = &unavailable}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<AudioSystem>(unavailable)));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(attempts, 0);
        for(int index = 0; index < 3; ++index) {
            ASSERT_TRUE(unavailable.request_one_shot(cue_handle, 1));
            ASSERT_TRUE(runtime.advance(0));
        }
        EXPECT_EQ(attempts, 1);
        EXPECT_TRUE(runtime.is_active());
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(unavailable.request_one_shot(cue_handle, 1));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(attempts, 2);
    }

    TEST_F(AudioServiceTest, OfflineOutputFailureStopsAndClearsTheRuntime) {
        int attempts = 0;
        AudioService unavailable(
            assets, AudioPlayback::Mode::Offline, [&](AudioPlayback::Mode, bool) {
                ++attempts;
                return Result<std::unique_ptr<AudioPlayback>, Error>::failure(
                    {"Offline mixer failed"});
            });
        const ScopeExit stop([&] { EXPECT_TRUE(runtime.stop()); });
        ASSERT_TRUE(runtime.set_services({.audio = &unavailable}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<AudioSystem>(unavailable)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(unavailable.request_one_shot(cue_handle, 1));
        const auto result = runtime.advance(0);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, "Offline mixer failed");
        EXPECT_EQ(attempts, 1);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_FALSE(unavailable.is_bound_to(scene));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(attempts, 1);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(attempts, 1);
    }
}

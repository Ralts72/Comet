#include "scripting/script.h"
#include "scene/script_component.h"
#include "scene/scene.h"
#include "scene/systems/script_system.h"
#include "scene/scene_runtime.h"
#include "asset/registry.h"
#include "common/scope_exit.h"
#include "diagnostics/logger.h"
#include <spdlog/sinks/callback_sink.h>

#include <gtest/gtest.h>

namespace Comet::Tests {
    class ScriptSystemTest: public testing::Test {
    protected:
        const AssetHandle handle{42};
        AssetRegistry assets;
        Scene scene;
        SceneRuntime runtime;
        void SetUp() override {
            ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
            ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        }
        void source(std::string code) {
            auto script = Script::create(std::move(code), "test.lua");
            ASSERT_TRUE(script) << script.error().message;
            if(assets.contains(handle))
                ASSERT_TRUE(assets.replace_asset(handle, script.value()));
            else
                ASSERT_TRUE(assets.register_asset(handle, script.value()));
        }
        Entity actor() {
            auto entity = scene.create_entity();
            entity.add_component<ScriptComponent>().asset = handle;
            return entity;
        }
    };

    TEST_F(ScriptSystemTest, LifecycleHasIsolatedStateAndSharedFixedTiming) {
        source(R"(
            return {
                on_start = function(self) self.steps = 0; comet.translate(1, 0, 0) end,
                fixed_update = function(self, dt)
                    self.steps = self.steps + 1
                    comet.translate(0, dt, 0)
                end,
                update = function(self) comet.translate(0, 0, self.steps) end
            }
        )");
        auto a = actor();
        auto b = actor();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.02));
        EXPECT_EQ(a.get_component<TransformComponent>().translation,
            b.get_component<TransformComponent>().translation);
        EXPECT_NEAR(a.get_component<TransformComponent>().translation.y, 0.02f, 1e-6f);
        EXPECT_FLOAT_EQ(a.get_component<TransformComponent>().translation.z, 2);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, ComponentRemovalReplacementAndFieldEditsHaveDifferentLifetimes) {
        source(R"(return {properties = {speed = 2},
            on_start = function(self) comet.translate(1, 0, 0) end,
            update = function(self) comet.translate(0, self.parameters.speed, 0) end})");
        auto entity = actor();
        ASSERT_TRUE(runtime.start(scene));
        entity.get_component<ScriptComponent>().parameters["speed"] = 3.0f;
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().translation.x, 1);
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().translation.y, 3);
        entity.remove_component<ScriptComponent>();
        entity.add_component<ScriptComponent>().asset = handle;
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().translation.x, 2);
        scene.destroy_entity(entity);
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, SourceReplacementOnlyAffectsNewInstances) {
        source("return {update = function(self) comet.translate(1,0,0) end}");
        auto entity = actor();
        ASSERT_TRUE(runtime.start(scene));
        source("return {update = function(self) comet.translate(10,0,0) end}");
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().translation.x, 1);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().translation.x, 11);
    }

    TEST_F(ScriptSystemTest, CleanupRunsOnceInReverseActualStartOrderEvenAfterFailure) {
        Config::Log config;
        config.enable_file_logging = false;
        Logger::init(config);
        const auto logger = Logger::get_console_logger();
        std::vector<std::string> messages;
        const auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [&](const spdlog::details::log_msg& message) {
                messages.emplace_back(message.payload.data(), message.payload.size());
            });
        logger->sinks().push_back(sink);
        const ScopeExit remove_sink([&] { std::erase(logger->sinks(), sink); });
        source(
            "return {properties = {label = 'first'}, on_stop = function(self) error(self.parameters.label) end}");
        auto first = actor();
        ASSERT_TRUE(runtime.start(scene));
        auto second = actor();
        second.get_component<ScriptComponent>().parameters["label"] = std::string("second");
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.stop());
        ASSERT_EQ(messages.size(), 2u);
        EXPECT_NE(messages[0].find("second"), std::string::npos);
        EXPECT_NE(messages[1].find("first"), std::string::npos);
        EXPECT_FALSE(first.get_component<ScriptComponent>().running_script());
        EXPECT_FALSE(second.get_component<ScriptComponent>().running_script());
    }

    TEST_F(ScriptSystemTest, RuntimeFailureStopsAndAllowsExplicitRestart) {
        source("return {on_start = function(self) error('start failed') end}");
        actor();
        auto result = runtime.start(scene);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().message.find("start failed"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        source("return {update = function(self) while true do end end}");
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(runtime.advance(0));
        EXPECT_FALSE(runtime.is_active());
        source("return {}");
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, MissingAssetAndMismatchedParametersAreReported) {
        auto entity = actor();
        EXPECT_FALSE(runtime.start(scene));
        source("return {properties = {speed = 1}}");
        entity.get_component<ScriptComponent>().parameters["speed"] = true;
        EXPECT_FALSE(runtime.start(scene));
        entity.get_component<ScriptComponent>().parameters = {{"renamed", 2.0f}};
        EXPECT_FALSE(runtime.start(scene));
        entity.get_component<ScriptComponent>().parameters.clear();
        ASSERT_TRUE(runtime.start(scene));
    }

    TEST_F(ScriptSystemTest, InputUsesAuthorizedFrameAndFixedStepDelta) {
        source(
            "return {update = function(self, dt) if comet.key_down('W') then comet.translate(0, 0, dt) end end}");
        auto entity = actor();
        ASSERT_TRUE(runtime.start(scene));
        Input::Frame input{.serial = 1, .focused = true};
        input.keys[static_cast<size_t>(Input::Key::W)].down = true;
        ASSERT_TRUE(runtime.advance(0.1, &input));
        EXPECT_NEAR(entity.get_component<TransformComponent>().translation.z, 0.1f, 1e-6f);
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_NEAR(entity.get_component<TransformComponent>().translation.z, 0.1f, 1e-6f);
    }

}

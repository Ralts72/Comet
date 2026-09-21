#include "scripting/script.h"
#include "scene/script_component.h"
#include "scene/systems/script_system.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/command_history.h"
#include "scene/scene_commands.h"
#include "asset/asset_manager.h"
#include "asset/registry.h"
#include "asset/serialization/metadata_serializer.h"
#include "core/task_scheduler.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"
#include "support/render_resource_factory.h"
#include "common/scope_exit.h"
#include "diagnostics/logger.h"
#include <spdlog/sinks/callback_sink.h>

#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    class ScriptTest: public testing::Test {
    protected:
        const AssetHandle handle{42};
        AssetRegistry assets;
        Scene scene;
        ComponentRegistry components = create_scene_component_registry();
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

    TEST_F(ScriptTest, LifecycleHasIsolatedStateAndSharedFixedTiming) {
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

    TEST_F(ScriptTest, ParametersUsePropertiesUndoSerializationAndPlayClone) {
        source(R"(return {
            properties = {speed = 100, enabled = true, label = "spin", axis = {0, 1, 0}},
            fixed_update = function(self, dt)
                if self.parameters.enabled then comet.rotate(0, self.parameters.speed * dt, 0) end
            end
        })");
        auto entity = actor();
        const auto lifetime = entity.get_component<ScriptComponent>().lifetime();
        CometEditor::CommandHistory history;
        history.bind_scene(&scene);
        CometEditor::PropertyEditTransaction edit(history, components);
        ParameterMap values{{"speed", 90.0f}, {"enabled", true}, {"axis", Math::Vec3{0, 1, 0}},
            {"label", std::string("test")}};
        ASSERT_TRUE(edit.apply({entity.get_uuid(), "script", "parameters"}, values));
        EXPECT_EQ(entity.get_component<ScriptComponent>().lifetime(), lifetime);
        ASSERT_TRUE(history.undo());
        EXPECT_TRUE(entity.get_component<ScriptComponent>().parameters.empty());
        ASSERT_TRUE(history.redo());
        const SceneSerializer serializer(components);
        auto encoded = serializer.serialize(scene);
        ASSERT_TRUE(encoded);
        EXPECT_EQ(encoded.value().find("lifetime"), std::string::npos);
        auto play = serializer.deserialize(encoded.value());
        ASSERT_TRUE(play) << play.error();
        auto clone = play.value()->find_entity(entity.get_uuid());
        EXPECT_NE(clone.get_component<ScriptComponent>().lifetime(), lifetime);
        EXPECT_EQ(clone.get_component<ScriptComponent>().parameters, values);
        ASSERT_TRUE(runtime.start(*play.value()));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_FLOAT_EQ(clone.get_component<TransformComponent>().rotation.y, 0);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_NEAR(clone.get_component<TransformComponent>().rotation.y, 0.9f, 1e-5f);
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().rotation.y, 0);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptTest, ComponentRemovalReplacementAndFieldEditsHaveDifferentLifetimes) {
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

    TEST_F(ScriptTest, SourceReplacementOnlyAffectsNewInstances) {
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

    TEST(ScriptSourceTest, InvalidCodeSchemaAndUnsafeLibrariesFailWithoutProcessTermination) {
        EXPECT_FALSE(Script::create("return {"));
        EXPECT_FALSE(Script::create("return 42"));
        EXPECT_FALSE(Script::create("return {update = true}"));
        EXPECT_FALSE(Script::create("return {properties = {value = function() end}}"));
        EXPECT_FALSE(Script::create("return {properties = {value = math.huge}}"));
        EXPECT_FALSE(Script::create("while true do end"));
        EXPECT_FALSE(
            Script::create("return {properties = {value = string.rep('x', 32*1024*1024)}}"));
        EXPECT_TRUE(Script::create(
            "assert(io == nil and os == nil and package == nil and debug == nil and load == nil and pcall == nil); return {}"));
        EXPECT_FALSE(Script::create("comet.rotate(0, 1, 0); return {}"));
    }

    TEST(ScriptInvocationTest, CachedParametersAreReadOnlyAndStopErrorsAreObservable) {
        const auto script = Script::create(R"(return {
            properties = {speed = 1, direction = {1, 2, 3}},
            on_start = function(self) self.config = self.parameters; self.steps = 0 end,
            update = function(self)
                assert(self.parameters == self.config)
                assert(#self.parameters.direction == 3)
                local count = 0
                for k, v in pairs(self.parameters) do count = count + 1 end
                assert(count == 2)
                self.steps = self.steps + 1
            end,
            on_stop = function(self)
                assert(self.steps == 2)
                self.parameters.direction[1] = 0
            end
        })");
        ASSERT_TRUE(script);
        auto instance = script.value()->instantiate();
        ASSERT_TRUE(instance);
        const auto& parameters = script.value()->defaults();
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Start, {}, parameters));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, {}, parameters));
        ASSERT_TRUE(instance.value()->invoke(Script::Phase::Update, {}, parameters));
        auto stopped = instance.value()->invoke(Script::Phase::Stop, {}, parameters);
        ASSERT_FALSE(stopped);
        EXPECT_NE(stopped.error().message.find("read-only"), std::string::npos);
    }

    TEST_F(ScriptTest, CleanupRunsOnceInReverseActualStartOrderEvenAfterFailure) {
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

    TEST_F(ScriptTest, RuntimeFailureStopsAndAllowsExplicitRestart) {
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

    TEST_F(ScriptTest, MissingAssetAndMismatchedParametersAreReported) {
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

    TEST_F(ScriptTest, CopyAndStructuralUndoCreateFreshLifetimeButMovePreservesIt) {
        auto entity = actor();
        const auto original = entity.get_component<ScriptComponent>().lifetime();
        ScriptComponent copy = entity.get_component<ScriptComponent>();
        EXPECT_NE(copy.lifetime(), original);
        const auto copied = copy.lifetime();
        ScriptComponent moved = std::move(copy);
        EXPECT_EQ(moved.lifetime(), copied);
        const auto* descriptor = components.find_component("script");
        const auto snapshot = descriptor->capture_component(entity);
        ASSERT_TRUE(descriptor->remove_component(entity));
        ASSERT_TRUE(descriptor->restore_component(entity, snapshot));
        EXPECT_NE(entity.get_component<ScriptComponent>().lifetime(), original);
        EXPECT_EQ(entity.get_component<ScriptComponent>().asset, handle);
    }

    TEST_F(ScriptTest, InputUsesAuthorizedFrameAndFixedStepDelta) {
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

    TEST_F(ScriptTest, MetadataAndAssetManagerLoadLuaWithoutProjectCompilation) {
        TemporaryDirectory directory;
        ProjectPaths paths(directory.path());
        std::filesystem::create_directories(paths.assets());
        ASSERT_TRUE(write_text_file_atomic(
            paths.assets() / "spin.lua", "return {properties = {speed = 2}}"));
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = handle, .type = AssetType::Script}, paths.assets() / "spin.lua.meta"));
        TaskScheduler scheduler(1);
        FakeRenderResourceFactory factory;
        AssetManager manager(paths, assets, factory, scheduler);
        ASSERT_TRUE(manager.scan().succeeded());
        auto loaded = manager.load_script(handle);
        ASSERT_TRUE(loaded) << loaded.error().message;
        EXPECT_EQ(std::get<float>(loaded.value()->defaults().at("speed")), 2);
        EXPECT_EQ(manager.load_script(handle).value(), loaded.value());
        ASSERT_TRUE(write_text_file_atomic(
            paths.assets() / "spin.lua", "return {properties = {speed = 200}}"));
        ASSERT_TRUE(manager.scan().succeeded());
        auto updated = manager.load_script(handle);
        ASSERT_TRUE(updated);
        EXPECT_NE(updated.value(), loaded.value());
        EXPECT_EQ(std::get<float>(updated.value()->defaults().at("speed")), 200);
    }
}

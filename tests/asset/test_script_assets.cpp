#include "asset/asset_manager.h"
#include "asset/registry.h"
#include "asset/serialization/metadata_serializer.h"
#include "common/file_io.h"
#include "core/task_scheduler.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/script_component.h"
#include "scene/systems/script_system.h"
#include "scripting/script.h"
#include "support/render_resource_factory.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <array>

namespace Comet::Tests {
    class ScriptGroupTest: public testing::Test {
    protected:
        static constexpr AssetHandle first{11}, second{22}, independent{33};
        static constexpr auto SHARED_SCRIPT = R"(
            local shared = require('scripts.shared')
            return {
                properties = {value = shared.value},
                on_start = function() comet.translate(0, 1, 0) end,
                update = function() comet.translate(shared.value, 0, 0) end
            }
        )";
        TemporaryDirectory directory;
        ProjectPaths paths{directory.path()};
        AssetDatabase database{paths};
        AssetRegistry registry;
        FakeRenderResourceFactory factory;
        TaskScheduler scheduler{1};
        AssetManager manager{database, registry, factory, scheduler};
        Scene scene;
        SceneRuntime runtime;
        ScriptSystem* script_system = nullptr;

        void SetUp() override {
            std::filesystem::create_directories(paths.assets() / "scripts");
            auto candidate_system = std::make_unique<ScriptSystem>(ScriptAssets{registry});
            script_system = candidate_system.get();
            ASSERT_TRUE(runtime.add_system(std::move(candidate_system)));
        }
        void write(const std::filesystem::path& path, const std::string_view source) {
            ASSERT_TRUE(write_text_file_atomic(paths.assets() / path, source));
        }
        void script(const AssetHandle handle, const std::string_view source = SHARED_SCRIPT) {
            const auto path = "scripts/" + std::to_string(handle.value()) + ".lua";
            write(path, source);
            ASSERT_TRUE(MetadataSerializer{}.save({.handle = handle, .type = AssetType::Script},
                metadata_path(paths.assets() / path)));
        }
        std::shared_ptr<Script> load(const AssetHandle handle) {
            auto loaded = manager.load_script(handle);
            if(!loaded) {
                ADD_FAILURE() << loaded.error().message;
                return nullptr;
            }
            return std::move(loaded).value();
        }
        Entity actor(const AssetHandle handle) {
            auto entity = scene.create_entity();
            entity.add_component<ScriptComponent>().asset = handle;
            return entity;
        }
    };

    TEST_F(ScriptGroupTest, LoadsNewConsumerWithoutRestartingUnchangedPeerAndReloadsSharedModule) {
        write("scripts/shared.module.lua", "return {value = 1}");
        script(first);
        script(second);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto old_first = load(first);
        ASSERT_TRUE(old_first);
        const auto left = actor(first);
        ASSERT_TRUE(runtime.start(scene));
        const auto old_second = load(second);
        ASSERT_TRUE(old_second);
        EXPECT_EQ(registry.resolve<Script>(first), old_first);
        const auto right = actor(second);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(left.get_component<TransformComponent>().translation, Math::Vec3(1, 1, 0));
        EXPECT_EQ(right.get_component<TransformComponent>().translation, Math::Vec3(1, 1, 0));

        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        write("scripts/shared.module.lua", "return {value = 20}");
        const std::array<std::filesystem::path, 1> changed{"scripts/shared.module.lua"};
        auto report = database.scan_changed_sources(changed);
        ASSERT_TRUE(report);
        EXPECT_EQ(report->modified_assets, (std::vector{first, second}));
        manager.accept_scan_report(*report);
        EXPECT_NE(registry.resolve<Script>(first), old_first);
        EXPECT_NE(registry.resolve<Script>(second), old_second);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(script_system->running_script(left), old_first);
        EXPECT_EQ(script_system->running_script(right), old_second);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(left.get_component<TransformComponent>().translation, Math::Vec3(21, 2, 0));
        EXPECT_EQ(right.get_component<TransformComponent>().translation, Math::Vec3(21, 2, 0));
        EXPECT_EQ(script_system->running_script(left), registry.resolve<Script>(first));
        EXPECT_EQ(script_system->running_script(right), registry.resolve<Script>(second));
        EXPECT_FALSE(std::filesystem::exists(paths.assets() / "scripts/shared.module.lua.meta"));
    }

    TEST_F(ScriptGroupTest, NewConsumerCannotPublishPastAnInvalidLoadedPeer) {
        write("scripts/shared.module.lua", "return {value = 1}");
        script(first);
        script(second);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto previous = load(first);
        ASSERT_TRUE(previous);
        write("scripts/shared.module.lua", "return {value = 2}");
        script(first, "return {");
        EXPECT_FALSE(manager.load_script(second));
        EXPECT_EQ(registry.resolve<Script>(first), previous);
        EXPECT_FALSE(registry.contains(second));
        script(first);
        ASSERT_TRUE(load(second));
        EXPECT_NE(registry.resolve<Script>(first), previous);
        EXPECT_FLOAT_EQ(
            std::get<float>(
                registry.resolve<Script>(first)->properties().at("value").default_value),
            2);
    }

    TEST_F(ScriptGroupTest, MissingModuleAndSyntaxFailureKeepWholeGroupUntilRepaired) {
        write("scripts/shared.module.lua", "return {value = 1}");
        script(first);
        script(second);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto left = load(first);
        const auto right = load(second);
        ASSERT_TRUE(left);
        ASSERT_TRUE(right);
        std::filesystem::remove(paths.assets() / "scripts/shared.module.lua");
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_EQ(registry.resolve<Script>(first), left);
        EXPECT_EQ(registry.resolve<Script>(second), right);
        write("scripts/shared.module.lua", "return {");
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_EQ(registry.resolve<Script>(first), left);
        EXPECT_EQ(registry.resolve<Script>(second), right);
        write("scripts/shared.module.lua", "return {value = 9}");
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_NE(registry.resolve<Script>(first), left);
        EXPECT_NE(registry.resolve<Script>(second), right);
    }

    TEST_F(ScriptGroupTest, FailedFirstLoadTracksMissingModuleForReferenceRecovery) {
        script(first);
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_FALSE(manager.load_script(first));
        const auto dependents = database.get_import_dependents("scripts/shared.module.lua");
        EXPECT_EQ(std::vector(dependents.begin(), dependents.end()), std::vector{first});
        write("scripts/shared.module.lua", "return {value = 7}");
        const auto report = manager.scan();
        ASSERT_TRUE(report.succeeded());
        EXPECT_NE(std::ranges::find(report.modified_assets, first), report.modified_assets.end());
        EXPECT_FALSE(registry.contains(first));
        EXPECT_TRUE(load(first));
    }

    TEST_F(ScriptGroupTest, NewDependencyMergesGroupsWithoutBlockingIndependentScripts) {
        write("scripts/shared.module.lua", "return {value = 1}");
        write("scripts/other.module.lua", "return {value = 2}");
        script(first);
        script(second, "local m = require('scripts.other'); return {properties={value=m.value}}");
        script(independent, "return {properties={value=3}}");
        ASSERT_TRUE(manager.scan().succeeded());
        const auto left = load(first);
        const auto right = load(second);
        const auto separate = load(independent);
        ASSERT_TRUE(left);
        ASSERT_TRUE(right);
        ASSERT_TRUE(separate);
        script(first, "local m = require('scripts.other'); return {properties={value=m.value}}");
        write("scripts/other.module.lua", "return {value = 12}");
        script(second, "return {");
        script(independent, "return {properties={value=30}}");
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_EQ(registry.resolve<Script>(first), left);
        EXPECT_EQ(registry.resolve<Script>(second), right);
        EXPECT_NE(registry.resolve<Script>(independent), separate);
        script(second, "local m = require('scripts.other'); return {properties={value=m.value}}");
        ASSERT_TRUE(manager.scan().succeeded());
        EXPECT_NE(registry.resolve<Script>(first), left);
        EXPECT_NE(registry.resolve<Script>(second), right);
        const auto watched = database.get_import_dependencies(first);
        EXPECT_EQ(std::vector(watched.begin(), watched.end()),
            (std::vector<std::filesystem::path>{"scripts/other.module.lua"}));
    }

    TEST_F(ScriptGroupTest, UnavailableRunningPeerDefersNewInstancesAndRecoversAsAGroup) {
        write("scripts/shared.module.lua", "return {value = 1}");
        script(first);
        script(second);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto old_first = load(first);
        const auto old_second = load(second);
        ASSERT_TRUE(old_first);
        ASSERT_TRUE(old_second);
        const auto left = actor(first);
        const auto right = actor(second);
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(registry.unregister_asset(second));
        write("scripts/shared.module.lua", "return {value = 10}");
        script(first, R"(return {
            on_start = function() comet.translate(0, 1, 0) end,
            update = function() comet.translate(10, 0, 0) end
        })");
        ASSERT_TRUE(manager.scan().succeeded());
        const auto pending = actor(first);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(script_system->running_script(left), old_first);
        EXPECT_EQ(script_system->running_script(right), old_second);
        EXPECT_FALSE(script_system->running_script(pending));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(left.get_component<TransformComponent>().translation, Math::Vec3(2, 1, 0));
        EXPECT_EQ(right.get_component<TransformComponent>().translation, Math::Vec3(2, 1, 0));
        ASSERT_TRUE(load(second));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(left.get_component<TransformComponent>().translation, Math::Vec3(12, 2, 0));
        EXPECT_EQ(right.get_component<TransformComponent>().translation, Math::Vec3(12, 2, 0));
        EXPECT_EQ(pending.get_component<TransformComponent>().translation, Math::Vec3(10, 1, 0));
    }

    TEST_F(ScriptGroupTest, RemovingUnavailablePeerAllowsTheSameCandidateToRetry) {
        write("scripts/shared.module.lua", "return {value = 1}");
        script(first);
        script(second);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto previous = load(first);
        ASSERT_TRUE(previous);
        ASSERT_TRUE(load(second));
        const auto left = actor(first);
        const auto right = actor(second);
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(registry.unregister_asset(second));
        write("scripts/shared.module.lua", "return {value = 10}");
        ASSERT_TRUE(manager.scan().succeeded());
        const auto candidate = registry.resolve<Script>(first);
        ASSERT_NE(candidate, previous);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(script_system->running_script(left), previous);
        scene.destroy_entity(right);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(script_system->running_script(left), candidate);
        EXPECT_EQ(left.get_component<TransformComponent>().translation, Math::Vec3(11, 2, 0));
    }

    TEST_F(ScriptGroupTest, MissingPeerStillBlocksNewVersionAfterChangedInstanceIsRemoved) {
        write("scripts/shared.module.lua", "return {value = 1}");
        script(first);
        script(second);
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(load(first));
        const auto old_second = load(second);
        ASSERT_TRUE(old_second);
        const auto left = actor(first);
        const auto right = actor(second);
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(registry.unregister_asset(second));
        write("scripts/shared.module.lua", "return {value = 10}");
        ASSERT_TRUE(manager.scan().succeeded());
        const auto pending = actor(first);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FALSE(script_system->running_script(pending));

        scene.destroy_entity(left);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(script_system->running_script(right), old_second);
        EXPECT_FALSE(script_system->running_script(pending));
        EXPECT_EQ(right.get_component<TransformComponent>().translation, Math::Vec3(2, 1, 0));
        EXPECT_EQ(pending.get_component<TransformComponent>().translation, Math::Vec3(0));

        ASSERT_TRUE(load(second));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(script_system->running_script(right), registry.resolve<Script>(second));
        EXPECT_EQ(script_system->running_script(pending), registry.resolve<Script>(first));
        EXPECT_EQ(right.get_component<TransformComponent>().translation, Math::Vec3(12, 2, 0));
        EXPECT_EQ(pending.get_component<TransformComponent>().translation, Math::Vec3(10, 1, 0));
    }

    TEST_F(ScriptGroupTest, InvalidNewInstanceKeepsOldGroupAndParameterRepairRetries) {
        write("scripts/shared.module.lua", "return {value = 1}");
        script(first);
        script(second);
        ASSERT_TRUE(manager.scan().succeeded());
        const auto old_first = load(first);
        const auto old_second = load(second);
        ASSERT_TRUE(old_first);
        ASSERT_TRUE(old_second);
        const auto left = actor(first);
        const auto right = actor(second);
        ASSERT_TRUE(runtime.start(scene));
        auto pending = actor(first);
        pending.get_component<ScriptComponent>().parameters["value"] = std::string("wrong type");
        write("scripts/shared.module.lua", "return {value = 10}");
        ASSERT_TRUE(manager.scan().succeeded());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_TRUE(runtime.is_active());
        EXPECT_EQ(script_system->running_script(left), old_first);
        EXPECT_EQ(script_system->running_script(right), old_second);
        EXPECT_FALSE(script_system->running_script(pending));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(left.get_component<TransformComponent>().translation, Math::Vec3(2, 1, 0));
        pending.get_component<ScriptComponent>().parameters["value"] = 5.0f;
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(script_system->running_script(left), registry.resolve<Script>(first));
        EXPECT_EQ(script_system->running_script(right), registry.resolve<Script>(second));
        EXPECT_EQ(script_system->running_script(pending), registry.resolve<Script>(first));
        EXPECT_EQ(left.get_component<TransformComponent>().translation, Math::Vec3(12, 2, 0));
        EXPECT_EQ(right.get_component<TransformComponent>().translation, Math::Vec3(12, 2, 0));
        EXPECT_EQ(pending.get_component<TransformComponent>().translation, Math::Vec3(10, 1, 0));
    }
}

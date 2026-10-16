#include "gameplay/navigation_system.h"
#include "physics/physics_service.h"
#include "scene/scene.h"
#include "scene/script_component.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/component_registry.h"
#include "scene/systems/physics_system.h"
#include "scene/systems/script_system.h"
#include "asset/registry.h"
#include "asset/script.h"
#include "core/project.h"
#include "asset/data/navigation_data.h"
#include "asset/database.h"
#include "support/temporary_directory.h"
#include <gtest/gtest.h>
#include <cmath>
#include <limits>

namespace Comet::Tests {
    namespace {
        using Gameplay::NavigationSystem;
        class Navigate final: public System {
        public:
            Entity actor;
            Math::Vec3 target{6, 0, 0};
            NavigationSystem::Status status = NavigationSystem::Status::NoPath;
            bool enabled = true;
            Result<void, Error> fixed_update(Scene& scene, const Context& c) override {
                if(!enabled)
                    return Result<void, Error>::success();
                auto result =
                    c.services.navigation->move_to(scene, actor, *c.services.physics, target);
                if(!result)
                    return Result<void, Error>::failure({result.error()});
                status = result.value();
                return Result<void, Error>::success();
            }
        };
        class NavigationTest: public testing::Test {
        protected:
            Scene scene;
            PhysicsService physics;
            SceneRuntime runtime;
            NavigationSystem* navigation;
            Navigate* controller;
            uint64_t geometry_requests = 0;
            std::shared_ptr<const NavigationData> navigation_product;
            Entity actor;
            Entity box(Math::Vec3 position, Math::Vec3 half_extents,
                BodyMotion motion = BodyMotion::Static) {
                auto entity = scene.create_entity("box");
                entity.edit_transform([&](auto& transform) { transform.translation = position; });
                entity.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = motion});
                entity.add_component<ColliderComponent>(
                    ColliderComponent{.half_extents = half_extents});
                return entity;
            }
            void SetUp() override {
                auto nav = std::make_unique<NavigationSystem>(
                    [&](Math::Vec3 a, Math::Vec3 b) {
                        ++geometry_requests;
                        EXPECT_GT(physics.get_statistics().bodies, 0u);
                        return physics.static_triangles(a, b);
                    },
                    [&](AssetHandle) { return navigation_product; });
                navigation = nav.get();
                ASSERT_TRUE(runtime.set_services({.physics = &physics, .navigation = navigation}));
                ASSERT_TRUE(runtime.set_settings({.fixed_delta = 1.0 / 60}));
                ASSERT_TRUE(runtime.add_system(std::move(nav)));
                auto controls = std::make_unique<Navigate>();
                controller = controls.get();
                ASSERT_TRUE(runtime.add_system(std::move(controls)));
                ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
                box({0, -0.5f, 0}, {10, 0.5f, 10});
                auto region = scene.create_entity("region");
                region.add_component<NavigationRegionComponent>(
                    NavigationRegionComponent{.half_extents = {10, 4, 10}});
                actor = scene.create_entity("NPC");
                actor.edit_transform([](auto& t) { t.translation = {-6, 0, 0}; });
                actor.add_component<CharacterControllerComponent>();
                actor.add_component<NavigationAgentComponent>();
                controller->actor = actor;
            }
            bool step(int count = 1) {
                for(int i = 0; i < count; ++i) {
                    auto result = runtime.advance(1.0 / 60);
                    if(!result) {
                        ADD_FAILURE() << result.error().message;
                        return false;
                    }
                }
                return true;
            }
            Math::Vec3 position() { return Math::Vec3(scene.get_world_matrix(actor)[3]); }
        };
    }
    TEST_F(NavigationTest, OfflineProductPreservesQueriesAndOwnsItsTileAfterTheSourceWorldStops) {
        box({0, 1.5f, 0}, {0.6f, 1.5f, 3});
        ASSERT_TRUE(runtime.start(scene));
        NavigationRegionComponent settings{.half_extents = {10, 4, 10}};
        auto geometry = physics.static_triangles(-settings.half_extents, settings.half_extents);
        ASSERT_TRUE(geometry);
        const auto fingerprint =
            Gameplay::NavigationMesh::input_fingerprint(geometry.value(), {}, settings);
        ASSERT_TRUE(fingerprint);
        const auto expected = navigation->mesh().find_path({-6, 0, 0}, {6, 0, 0});
        ASSERT_TRUE(expected);
        ASSERT_EQ(expected.value().status, Gameplay::NavigationMesh::Status::Complete);
        auto product = navigation->mesh().product(fingerprint.value());
        ASSERT_TRUE(product) << product.error();
        const TemporaryDirectory output;
        const auto path = output.path() / "ground.nav";
        ASSERT_TRUE(product.value().save(path));
        auto restored = NavigationData::load(path);
        ASSERT_TRUE(restored) << restored.error();
        EXPECT_EQ(restored.value().input_fingerprint, fingerprint.value());
        Gameplay::NavigationMesh loaded;
        ASSERT_TRUE(loaded.load(restored.value()));
        restored.value().tile.clear();
        ASSERT_TRUE(runtime.stop());
        const auto actual = loaded.find_path({-6, 0, 0}, {6, 0, 0});
        ASSERT_TRUE(actual);
        EXPECT_EQ(actual.value().status, expected.value().status);
        EXPECT_EQ(actual.value().points, expected.value().points);
        EXPECT_TRUE(loaded.surface_step({-6, 0, 0}, {-5, 0, 0}));
        auto reordered = geometry.value();
        std::rotate(reordered.begin(), reordered.begin() + 3, reordered.end());
        EXPECT_EQ(Gameplay::NavigationMesh::input_fingerprint(reordered, {}, settings).value(),
            fingerprint.value());
        std::swap(reordered[0], reordered[1]);
        EXPECT_NE(Gameplay::NavigationMesh::input_fingerprint(reordered, {}, settings).value(),
            fingerprint.value());
        settings.agent_radius += 0.1f;
        EXPECT_NE(
            Gameplay::NavigationMesh::input_fingerprint(geometry.value(), {}, settings).value(),
            fingerprint.value());

        const auto encoded = product.value().encode();
        ASSERT_TRUE(encoded);
        auto corrupted = encoded.value();
        corrupted.back() ^= std::byte{1};
        EXPECT_FALSE(NavigationData::decode(corrupted));
        EXPECT_FALSE(NavigationData::decode(std::span(encoded.value()).first(48)));
        for(const int kind : {0, 1, 2, 3, 4}) {
            auto invalid = product.value();
            if(kind == 0)
                invalid.tile.resize(invalid.tile.size() - 1);
            else if(kind == 1)
                ++invalid.library_version;
            else if(kind == 2)
                invalid.reference_bytes = invalid.reference_bytes == 4 ? 8 : 4;
            else if(kind == 3)
                invalid.byte_order = invalid.byte_order == 1 ? 2 : 1;
            else {
                // Detour tile 头的第九个 32 位字段为 maxLinkCount。
                std::fill_n(
                    invalid.tile.begin() + 8 * sizeof(uint32_t), sizeof(uint32_t), std::byte{0});
            }
            EXPECT_FALSE(loaded.load(invalid));
            EXPECT_EQ(
                loaded.find_path({-6, 0, 0}, {6, 0, 0}).value().points, expected.value().points);
        }
    }

    TEST_F(NavigationTest, PreparedProductSkipsRecastAndStaleOrMissingProductsFailExplicitly) {
        auto wall = box({0, 1.5f, 0}, {0.6f, 1.5f, 3});
        ASSERT_TRUE(runtime.start(scene));
        NavigationRegionComponent settings{.half_extents = {10, 4, 10}};
        const auto triangles =
            physics.static_triangles(-settings.half_extents, settings.half_extents);
        ASSERT_TRUE(triangles);
        const auto fingerprint =
            Gameplay::NavigationMesh::input_fingerprint(triangles.value(), {}, settings);
        ASSERT_TRUE(fingerprint);
        const auto product = navigation->mesh().product(fingerprint.value());
        ASSERT_TRUE(product);
        navigation_product = std::make_shared<const NavigationData>(product.value());
        ASSERT_EQ(navigation->get_statistics().builds, 1u);
        ASSERT_TRUE(runtime.stop());
        scene.each<NavigationRegionComponent>(
            [](Entity, auto& region) { region.data = AssetHandle{91}; });
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(navigation->get_statistics().builds, 0u);
        EXPECT_EQ(navigation->get_statistics().product_loads, 1u);
        EXPECT_GE(navigation->get_statistics().prepare_milliseconds,
            navigation->get_statistics().product_load_milliseconds);
        ASSERT_TRUE(step(10));
        EXPECT_GT(navigation->get_statistics().path_queries, 0u);
        ASSERT_TRUE(runtime.stop());
        wall.edit_transform([](auto& value) { value.translation.x = 2; });
        const auto stale = runtime.start(scene);
        ASSERT_FALSE(stale);
        EXPECT_NE(stale.error().message.find("stale"), std::string::npos);
        EXPECT_EQ(navigation->get_statistics().builds, 0u);
        navigation_product.reset();
        const auto missing = runtime.start(scene);
        ASSERT_FALSE(missing);
        EXPECT_NE(missing.error().message.find("not prepared"), std::string::npos);
        EXPECT_EQ(navigation->get_statistics().builds, 0u);
    }

    TEST_F(NavigationTest, RoutesAroundWallAndPhysicsReachesGoal) {
        box({0, 1.5f, 0}, {0.6f, 1.5f, 3});
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step());
        auto path = navigation->mesh().find_path({-6, 0, 0}, {6, 0, 0});
        ASSERT_TRUE(path);
        ASSERT_EQ(path.value().status, Gameplay::NavigationMesh::Status::Complete);
        EXPECT_GT(path.value().points.size(), 2u);
        float furthest_z = 0;
        for(int i = 0; i < 480; ++i) {
            ASSERT_TRUE(step());
            furthest_z = std::max(furthest_z, std::abs(position().z));
        }
        EXPECT_GT(furthest_z, 3);
        EXPECT_EQ(controller->status, NavigationSystem::Status::Arrived);
        EXPECT_NEAR(position().x, 6, 0.4f);
        EXPECT_NEAR(position().z, 0, 0.4f);
    }
    TEST_F(NavigationTest, PausedStartupBuildsAfterPhysicsBeforeAnyFixedStepOrQuery) {
        ASSERT_TRUE(runtime.start(scene, SceneRuntime::State::Paused));
        ASSERT_TRUE(navigation->mesh().ready());
        EXPECT_EQ(geometry_requests, 1u);
        const auto prepared = navigation->get_statistics();
        EXPECT_EQ(prepared.builds, 1u);
        EXPECT_GE(prepared.build_milliseconds, 0);
        EXPECT_TRUE(std::isfinite(prepared.build_milliseconds));
        EXPECT_EQ(prepared.path_queries, 0u);
        EXPECT_EQ(prepared.surface_queries, 0u);
        EXPECT_EQ(prepared.obstacle_queries, 0u);
        EXPECT_EQ(runtime.get_timing().fixed_index, 0u);
        EXPECT_FALSE(navigation->move_to(scene, actor, physics, {6, 0, 0}));
        ASSERT_TRUE(step());
        EXPECT_EQ(geometry_requests, 1u);
        EXPECT_EQ(navigation->get_statistics().path_queries, 0u);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(step());
        const auto simulated = navigation->get_statistics();
        EXPECT_EQ(geometry_requests, 1u);
        EXPECT_EQ(simulated.builds, prepared.builds);
        EXPECT_EQ(simulated.build_milliseconds, prepared.build_milliseconds);
        EXPECT_EQ(simulated.path_queries, 1u);
        EXPECT_GT(simulated.surface_queries, 0u);
        EXPECT_GT(simulated.obstacle_queries, 0u);
    }
    TEST_F(NavigationTest, FailedStartupBuildCleansEveryRuntimeServiceAndCanRestart) {
        Entity region;
        scene.each<const NavigationRegionComponent>(
            [&](Entity entity, const auto&) { region = entity; });
        region.get_component<NavigationRegionComponent>().cell_size = 0;
        const auto initial = position();
        const auto started = runtime.start(scene);
        ASSERT_FALSE(started);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_FALSE(physics.is_bound_to(scene));
        EXPECT_EQ(physics.get_statistics().bodies, 0u);
        EXPECT_FALSE(navigation->mesh().ready());
        EXPECT_EQ(navigation->get_statistics().builds, 1u);
        EXPECT_EQ(navigation->get_statistics().path_queries, 0u);
        EXPECT_EQ(geometry_requests, 1u);
        EXPECT_EQ(position(), initial);
        EXPECT_EQ(runtime.get_timing().fixed_index, 0u);
        region.get_component<NavigationRegionComponent>().cell_size = 0.15f;
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_TRUE(navigation->mesh().ready());
        EXPECT_EQ(navigation->get_statistics().builds, 1u);
        EXPECT_EQ(navigation->get_statistics().path_queries, 0u);
        EXPECT_EQ(geometry_requests, 2u);
    }
    TEST_F(NavigationTest, ScenesWithoutRegionsDoNotBakeAtStartupOrOnFirstStep) {
        controller->enabled = false;
        Entity region;
        scene.each<const NavigationRegionComponent>(
            [&](Entity entity, const auto&) { region = entity; });
        region.remove_component<NavigationRegionComponent>();
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(navigation->mesh().ready());
        EXPECT_EQ(geometry_requests, 0u);
        EXPECT_EQ(navigation->get_statistics().builds, 0u);
        ASSERT_TRUE(step());
        EXPECT_EQ(geometry_requests, 0u);
        EXPECT_EQ(navigation->get_statistics().builds, 0u);
    }
    TEST_F(NavigationTest, DisconnectedAndOutsideTargetsStopInsteadOfFollowingPartialPath) {
        box({0, 2, 0}, {0.6f, 2, 10});
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step(30));
        EXPECT_EQ(controller->status, NavigationSystem::Status::NoPath);
        EXPECT_NEAR(position().x, -6, 0.01f);
        controller->target = {-20, 0, 0};
        ASSERT_TRUE(step());
        EXPECT_EQ(controller->status, NavigationSystem::Status::NoPath);
    }
    TEST_F(NavigationTest, DynamicObstacleAvoidanceAndResume) {
        auto obstacle = box({-3, 1, 0}, {0.6f, 1, 0.7f}, BodyMotion::Kinematic);
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step());
        // 动态物体未烘入静态网格；局部避障仍能绕行。
        auto path = navigation->mesh().find_path({-6, 0, 0}, {6, 0, 0});
        ASSERT_TRUE(path);
        EXPECT_EQ(path.value().points.size(), 2u);
        ASSERT_TRUE(step(480));
        EXPECT_EQ(controller->status, NavigationSystem::Status::Arrived);
        EXPECT_GT(position().x, 5.5f);
    }
    TEST_F(NavigationTest, WalkableStepsUseCharacterClimbing) {
        box({0, 0.1f, 0}, {1, 0.1f, 10});
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step(400));
        EXPECT_EQ(controller->status, NavigationSystem::Status::Arrived);
        EXPECT_GT(position().x, 5.5f);
    }
    TEST_F(NavigationTest, DynamicFullBlockStopsAndResumesWhenRemoved) {
        auto wall = box({0, 1.5f, 0}, {0.6f, 1.5f, 10}, BodyMotion::Kinematic);
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step(240));
        EXPECT_LT(position().x, -0.8f);
        EXPECT_NE(controller->status, NavigationSystem::Status::Arrived);
        scene.destroy_entity(wall);
        ASSERT_TRUE(step(240));
        EXPECT_EQ(controller->status, NavigationSystem::Status::Arrived);
    }
    TEST_F(NavigationTest, PauseStepStopAndRestartOwnNavigationLifetime) {
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step(20));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        const auto paused = position();
        ASSERT_TRUE(step(60));
        EXPECT_EQ(position(), paused);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(step());
        EXPECT_GT(position().x, paused.x);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(navigation->mesh().ready());
        EXPECT_FALSE(navigation->move_to(scene, actor, physics, {6, 0, 0}));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step());
        EXPECT_TRUE(navigation->mesh().ready());
    }
    TEST_F(NavigationTest, InvalidRegionAndInvalidTargetReportErrors) {
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step());
        EXPECT_FALSE(navigation->mesh().find_path(
            {0, 0, 0}, {std::numeric_limits<float>::infinity(), 0, 0}));
        Gameplay::NavigationMesh mesh;
        NavigationRegionComponent settings;
        settings.cell_size = 0;
        const Math::Vec3 vertices[] = {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}};
        EXPECT_FALSE(mesh.build(vertices, {}, settings));
        settings.cell_size = 0.05f;
        settings.half_extents = {10000, 5, 10000};
        EXPECT_FALSE(mesh.build(vertices, {}, settings));
    }
    TEST_F(NavigationTest, QueryBudgetDefersAdditionalAgentsWithoutMovingThem) {
        controller->enabled = false;
        std::vector<Entity> actors;
        for(int i = 0; i < 10; ++i) {
            auto npc = scene.create_entity("NPC");
            npc.edit_transform([&](auto& t) { t.translation = {-6, 0, float(i) - 5}; });
            npc.add_component<CharacterControllerComponent>();
            npc.add_component<NavigationAgentComponent>();
            actors.push_back(npc);
        }
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(step());
        for(int i = 0; i < 10; ++i) {
            auto result = navigation->move_to(scene, actors[i], physics, {6, 0, float(i) - 5});
            ASSERT_TRUE(result);
            if(i >= 8)
                EXPECT_EQ(result.value(), NavigationSystem::Status::BudgetExceeded);
        }
        EXPECT_EQ(navigation->get_statistics().path_queries, 8u);
        EXPECT_EQ(navigation->get_statistics().step_path_queries, 8u);
        EXPECT_EQ(navigation->get_statistics().builds, 1u);
        ASSERT_TRUE(step());
        auto retry = navigation->move_to(scene, actors.back(), physics, {6, 0, 4});
        ASSERT_TRUE(retry);
        EXPECT_EQ(retry.value(), NavigationSystem::Status::Moving);
        EXPECT_EQ(navigation->get_statistics().path_queries, 9u);
        EXPECT_EQ(navigation->get_statistics().step_path_queries, 1u);
    }
    TEST(NavigationStartupTest, MissingPreparedCollisionWorldFailsBeforeSimulation) {
        Scene scene;
        scene.create_entity("region").add_component<NavigationRegionComponent>();
        PhysicsService physics;
        SceneRuntime runtime;
        auto nav = std::make_unique<NavigationSystem>(
            [&](Math::Vec3 a, Math::Vec3 b) { return physics.static_triangles(a, b); });
        const auto* navigation = nav.get();
        ASSERT_TRUE(runtime.set_services({.physics = &physics, .navigation = nav.get()}));
        ASSERT_TRUE(runtime.add_system(std::move(nav)));
        const auto started = runtime.start(scene);
        ASSERT_FALSE(started);
        EXPECT_NE(started.error().message.find("active world"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_FALSE(physics.is_bound_to(scene));
        EXPECT_FALSE(navigation->mesh().ready());
        EXPECT_EQ(navigation->get_statistics().builds, 0u);
        EXPECT_GT(navigation->get_statistics().prepare_milliseconds, 0);
        EXPECT_EQ(navigation->get_statistics().path_queries, 0u);
        EXPECT_EQ(runtime.get_timing().fixed_index, 0u);
    }
    TEST(NavigationDemoTest, SerializedScenePatrolsChasesReturnsAndClones) {
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project) << project.error();
        const auto components = create_scene_component_registry();
        const SceneSerializer serializer(components);
        auto loaded = serializer.load(
            (project.value().paths().assets() / "scenes/navigation.scene").string());
        ASSERT_TRUE(loaded) << loaded.error();
        auto& scene = *loaded.value();
        Entity player, npc;
        for(auto entity : scene.get_entities()) {
            const auto& name = entity.get_component<NameComponent>().name;
            if(name == "角色")
                player = entity;
            if(name == "巡逻 NPC")
                npc = entity;
            if(entity.has_component<ScriptComponent>()) {
                const auto handle = entity.get_component<ScriptComponent>().asset.value();
                if(handle != 6729301842000000021ULL && handle != 14309634625000312006ULL
                    && handle != 6729301842000000023ULL)
                    entity.remove_component<ScriptComponent>();
            }
        }
        ASSERT_TRUE(player);
        ASSERT_TRUE(npc);
        auto cloned = serializer.clone(scene);
        ASSERT_TRUE(cloned) << cloned.error();
        const auto clone_npc = cloned.value()->find_entity(npc.get_uuid());
        EXPECT_EQ(clone_npc.get_component<NavigationAgentComponent>(),
            npc.get_component<NavigationAgentComponent>());
        EXPECT_EQ(cloned.value()->component_count<NavigationRegionComponent>(), 1u);
        AssetRegistry assets;
        AssetDatabase database(project.value().paths());
        ASSERT_TRUE(database.scan().succeeded());
        scene.each<NavigationRegionComponent>([&](Entity, const auto& region) {
            const auto* record = database.find(region.data);
            ASSERT_TRUE(record);
            const auto data = NavigationData::load(database.paths().assets() / record->path);
            ASSERT_TRUE(data) << data.error();
            ASSERT_TRUE(
                assets.register_asset(region.data, std::make_shared<NavigationData>(data.value())));
        });
        for(const auto& [id, file] : {std::pair{6729301842000000021ULL, "scripts/npc_patrol.lua"},
                std::pair{14309634625000312006ULL, "scripts/moving_platform.lua"},
                std::pair{6729301842000000023ULL, "scripts/character_controls.lua"}}) {
            auto script = Script::load(project.value().paths().assets() / file);
            ASSERT_TRUE(script) << script.error().message;
            ASSERT_TRUE(assets.register_asset(AssetHandle(id), script.value()));
        }
        PhysicsService physics;
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_input_actions(project.value().input_actions()));
        auto nav = std::make_unique<NavigationSystem>(
            [&](Math::Vec3 a, Math::Vec3 b) { return physics.static_triangles(a, b); },
            [&](AssetHandle handle) { return assets.resolve<NavigationData>(handle); });
        ASSERT_TRUE(runtime.set_services({.physics = &physics, .navigation = nav.get()}));
        ASSERT_TRUE(runtime.add_system(std::move(nav)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        const auto location = [&] { return Math::Vec3(scene.get_world_matrix(npc)[3]); };
        const auto tick = [&] {
            const auto result = runtime.advance(1.0 / 60);
            if(!result)
                ADD_FAILURE() << result.error().message;
            return bool(result);
        };
        player.edit_transform([](auto& t) { t.translation = {0, 0, 11}; });
        ASSERT_TRUE(runtime.start(scene));
        float furthest_x = -6;
        for(int i = 0; i < 600; ++i) {
            ASSERT_TRUE(tick());
            furthest_x = std::max(furthest_x, location().x);
        }
        EXPECT_GT(furthest_x, 4.5f);
        const auto target = location() + Math::Vec3(0, 0, 4);
        player.edit_transform([&](auto& t) { t.translation = target; });
        for(int i = 0; i < 180; ++i)
            ASSERT_TRUE(tick());
        EXPECT_LT(glm::distance(location(), target), 1.2f);
        player.edit_transform([](auto& t) { t.translation = {100, 0, 100}; });
        float home_distance = 100;
        for(int i = 0; i < 900; ++i) {
            ASSERT_TRUE(tick());
            home_distance =
                std::min(home_distance, glm::distance(location(), Math::Vec3(-6, 0, 0)));
        }
        EXPECT_LT(home_distance, 1);
        ASSERT_TRUE(runtime.stop());
    }

}

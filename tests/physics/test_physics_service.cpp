#include "scripting/script_instance.h"
#include "physics/physics_service.h"
#include "audio/audio_service.h"
#include "asset/registry.h"
#include "scene/component_registry.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/scene_serializer.h"
#include "scene/script_component.h"
#include "scene/systems/physics_system.h"
#include "scene/systems/script_system.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <cmath>
#include <utility>

namespace Comet::Tests {
    namespace {
        void configure_body(Entity entity) {
            entity.add_component<RigidBodyComponent>();
            entity.add_component<ColliderComponent>();
        }

        class FailAfterCommands final: public System {
        public:
            explicit FailAfterCommands(Entity entity) : m_entity(entity) {}

            Result<void, Error> on_start(
                Scene&, RuntimeSession&, const RuntimeServices& services) override {
                if(!std::exchange(m_fail, false))
                    return Result<void, Error>::success();
                EXPECT_TRUE(services.physics->request_impulse(m_entity, {100, 0, 0}));
                EXPECT_TRUE(services.physics->request_force(m_entity, {100, 0, 0}));
                EXPECT_TRUE(services.physics->request_torque(m_entity, {0, 0, 10}));
                EXPECT_TRUE(services.physics->request_impulse_at(m_entity, {0, 1, 0}, {1, 0, 0}));
                EXPECT_TRUE(services.physics->request_force_at(m_entity, {0, 1, 0}, {1, 0, 0}));
                return Result<void, Error>::failure({"Startup failed after physics commands"});
            }

            void on_stop(
                Scene& scene, RuntimeSession&, const RuntimeServices& services) noexcept override {
                EXPECT_TRUE(services.physics->is_bound_to(scene));
            }

        private:
            Entity m_entity;
            bool m_fail = true;
        };
    }

    class MeshColliderTest: public testing::Test {
    protected:
        static constexpr AssetHandle ground_mesh{501}, box_mesh{502};
        std::unordered_map<AssetHandle, MeshData> meshes;
        AssetRevision revision = 1;
        int reads = 0;
        bool fail_read = false;
        PhysicsService physics{{.read = [this](AssetHandle handle) -> Result<MeshData, Error> {
                                    ++reads;
                                    if(fail_read || !meshes.contains(handle))
                                        return Result<MeshData, Error>::failure(
                                            {"Missing collision geometry"});
                                    return Result<MeshData, Error>::success(meshes.at(handle));
                                },
            .revision = [this](AssetHandle) { return revision; }}};
        Scene scene;
        SceneRuntime runtime;
        Entity floor, body;

        void SetUp() override {
            auto& ground = meshes[ground_mesh];
            for(const auto position : {Math::Vec3(-5, 0, -5), Math::Vec3(-5, 0, 5),
                    Math::Vec3(5, 0, 5), Math::Vec3(5, 0, -5)})
                ground.vertices.push_back({.position = position});
            ground.indices = {0, 1, 2, 0, 2, 3};
            auto& cube = meshes[box_mesh];
            for(float x : {-0.5f, 0.5f})
                for(float y : {-0.5f, 0.5f})
                    for(float z : {-0.5f, 0.5f})
                        cube.vertices.push_back({.position = {x, y, z}});
            cube.indices = {0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5, 0, 4, 5, 0, 5, 1, 2, 3, 7, 2, 7, 6,
                0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3};
            floor = scene.create_entity("Mesh floor");
            floor.add_component<RigidBodyComponent>(
                RigidBodyComponent{.motion = BodyMotion::Static});
            floor.add_component<ColliderComponent>(
                ColliderComponent{.shape = ColliderShape::TriangleMesh, .mesh = ground_mesh});
            body = scene.create_entity("Convex body");
            body.add_component<RigidBodyComponent>();
            body.add_component<ColliderComponent>(
                ColliderComponent{.shape = ColliderShape::ConvexHull, .mesh = box_mesh});
            body.edit_transform([](auto& pose) { pose.translation.y = 3; });
            ASSERT_TRUE(runtime.set_services({.physics = &physics}));
            ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        }
    };

    TEST_F(MeshColliderTest, MeshOutlinesUseCookedFacesAndPreserveScaledMeshOrigin) {
        auto data = meshes.at(box_mesh);
        data.vertices.push_back({.position = {0.1f, 0.1f, 0.1f}});
        data.vertices.push_back({.position = {0.2f, 0.1f, 0.1f}});
        data.vertices.push_back({.position = {0.1f, 0.2f, 0.1f}});
        data.indices.insert(data.indices.end(), {8, 9, 10});
        for(auto& vertex : data.vertices)
            vertex.position.x += 2;
        const Math::Vec3 scale(2, 3, 4);
        const auto hull =
            PhysicsService::mesh_collider_edges(ColliderShape::ConvexHull, data, scale);
        ASSERT_TRUE(hull) << hull.error().message;
        ASSERT_FALSE(hull.value().empty());
        auto bounds = BoundingBox::from_point(hull.value().front().first);
        for(const auto& [first, second] : hull.value()) {
            bounds.include(first);
            bounds.include(second);
            EXPECT_TRUE(first.x == 3 || first.x == 5);
            EXPECT_TRUE(second.x == 3 || second.x == 5);
        }
        EXPECT_LT(Math::length(bounds.minimum - Math::Vec3(3, -1.5f, -2)), 1e-5f);
        EXPECT_LT(Math::length(bounds.maximum - Math::Vec3(5, 1.5f, 2)), 1e-5f);
        const auto triangles =
            PhysicsService::mesh_collider_edges(ColliderShape::TriangleMesh, data, scale);
        ASSERT_TRUE(triangles) << triangles.error().message;
        EXPECT_TRUE(std::ranges::any_of(triangles.value(),
            [](const auto& edge) { return edge.first.x > 3 && edge.first.x < 5; }));
        const auto floor_edges = PhysicsService::mesh_collider_edges(
            ColliderShape::TriangleMesh, meshes.at(ground_mesh), Math::Vec3(1));
        ASSERT_TRUE(floor_edges);
        EXPECT_EQ(floor_edges.value().size(), 5u);
        EXPECT_FALSE(
            PhysicsService::mesh_collider_edges(ColliderShape::ConvexHull, data, Math::Vec3(-1)));
        EXPECT_FALSE(PhysicsService::mesh_collider_edges(ColliderShape::Box, data, Math::Vec3(1)));
        // 无世界预览不得影响后续建世界，也不得提前释放仍在运行的 Jolt 状态。
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_TRUE(PhysicsService::mesh_collider_edges(ColliderShape::ConvexHull, data, scale));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_TRUE(physics.get_velocity(body));
    }

    TEST_F(MeshColliderTest, ConvexBodiesLandOnTriangleMeshAndShareCookedGeometry) {
        auto second = scene.create_entity("Shared hull");
        second.add_component<RigidBodyComponent>();
        second.add_component<ColliderComponent>(body.get_component<ColliderComponent>());
        second.edit_transform([](auto& pose) { pose.translation = {2, 4, 0}; });
        auto started = runtime.start(scene);
        ASSERT_TRUE(started) << started.error().message;
        EXPECT_EQ(reads, 2);
        for(int frame = 0; frame < 200; ++frame)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_NEAR(body.get_component<TransformComponent>().translation.y, 0.5f, 0.05f);
        EXPECT_NEAR(second.get_component<TransformComponent>().translation.y, 0.5f, 0.05f);
        EXPECT_EQ(reads, 2);
        const auto ray = physics.raycast({4, 3, 0}, {0, -6, 0}, {});
        ASSERT_TRUE(ray);
        ASSERT_TRUE(ray.value());
        EXPECT_EQ(ray.value()->entity, floor.get_uuid());
        EXPECT_NEAR(ray.value()->fraction, 0.5f, 0.001f);
        EXPECT_NEAR(ray.value()->normal.y, 1, 0.001f);
        TransformComponent probe;
        probe.translation = {4, 3, 0};
        const auto cast =
            physics.shape_cast(ColliderComponent{.shape = ColliderShape::Sphere, .radius = 0.25f},
                probe, {0, -6, 0}, {});
        ASSERT_TRUE(cast);
        ASSERT_TRUE(cast.value());
        EXPECT_EQ(cast.value()->entity, floor.get_uuid());
        EXPECT_NEAR(cast.value()->fraction, 2.75f / 6, 0.01f);
    }

    TEST_F(MeshColliderTest, CharacterControllerStandsOnTheTriangleSurface) {
        auto character = scene.create_entity("Character");
        character.add_component<CharacterControllerComponent>();
        character.edit_transform([](auto& transform) { transform.translation = {-2, 3, 0}; });
        ASSERT_TRUE(runtime.start(scene));
        for(int frame = 0; frame < 200; ++frame)
            ASSERT_TRUE(runtime.advance(0.01));
        const auto state = physics.get_character_state(character);
        ASSERT_TRUE(state);
        EXPECT_TRUE(state->grounded);
        EXPECT_NEAR(character.get_component<TransformComponent>().translation.y, 0, 0.05f);
    }

    TEST_F(MeshColliderTest, GeometryRevisionRebuildsAtFixedBoundaryAndKeepsBodyVelocity) {
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_impulse(body, {2, 0, 0}));
        ASSERT_TRUE(runtime.advance(0.01));
        for(auto& vertex : meshes[ground_mesh].vertices)
            vertex.position.y = 1;
        ++revision;
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(reads, 4);
        const auto ray = physics.raycast({4, 4, 0}, {0, -6, 0}, {});
        ASSERT_TRUE(ray);
        ASSERT_TRUE(ray.value());
        EXPECT_NEAR(ray.value()->position.y, 1, 0.001f);
        const auto velocity = physics.get_velocity(body);
        ASSERT_TRUE(velocity);
        EXPECT_NEAR(velocity->linear.x, 2, 0.01f);
        EXPECT_GT(body.get_component<TransformComponent>().translation.x, 0.03f);
        fail_read = true;
        ++revision;
        EXPECT_FALSE(runtime.advance(0.01));
        EXPECT_FALSE(physics.is_bound_to(scene));
    }

    TEST_F(MeshColliderTest, OffsetHullCastAndBodyUseMeshOriginInsteadOfCenterOfMass) {
        for(auto& vertex : meshes[box_mesh].vertices)
            vertex.position.x += 2;
        body.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        body.edit_transform([](auto& pose) { pose.translation = {10, 2, 0}; });
        ASSERT_TRUE(runtime.start(scene));
        const auto ray = physics.raycast({12, 5, 0}, {0, -6, 0}, {});
        ASSERT_TRUE(ray);
        ASSERT_TRUE(ray.value());
        EXPECT_EQ(ray.value()->entity, body.get_uuid());
        EXPECT_NEAR(ray.value()->position.y, 2.5f, 0.01f);
        TransformComponent pose;
        pose.translation = {4, 2, 0};
        const auto cast =
            physics.shape_cast(body.get_component<ColliderComponent>(), pose, {10, 0, 0}, {});
        ASSERT_TRUE(cast) << cast.error();
        ASSERT_TRUE(cast.value());
        EXPECT_EQ(cast.value()->entity, body.get_uuid());
        EXPECT_NEAR(cast.value()->fraction, 0.5f, 0.01f);
        EXPECT_FALSE(
            physics.shape_cast(floor.get_component<ColliderComponent>(), pose, {10, 0, 0}, {}));
    }

    TEST_F(MeshColliderTest, PositiveNonuniformScaleAndRotationApplyToTriangleGeometry) {
        body.edit_transform([](auto& pose) { pose.translation = {20, 3, 0}; });
        floor.edit_transform([](auto& pose) {
            pose.translation = {10, 2, 0};
            pose.rotation = {0, 0, 30};
            pose.scale = {2, 1, 3};
        });
        ASSERT_TRUE(runtime.start(scene));
        const auto ray = physics.raycast({10, 5, 0}, {0, -6, 0}, {});
        ASSERT_TRUE(ray);
        ASSERT_TRUE(ray.value());
        EXPECT_EQ(ray.value()->entity, floor.get_uuid());
        EXPECT_NEAR(ray.value()->position.y, 2, 0.001f);
        EXPECT_NEAR(ray.value()->normal.x, -0.5f, 0.001f);
        EXPECT_NEAR(ray.value()->normal.y, std::sqrt(0.75f), 0.001f);
    }

    TEST_F(MeshColliderTest, RejectsMovingTriangleMeshesAndMalformedOrFlatHulls) {
        for(const auto motion : {BodyMotion::Dynamic, BodyMotion::Kinematic}) {
            floor.edit_component<RigidBodyComponent>([&](auto& rigid) { rigid.motion = motion; });
            auto started = runtime.start(scene);
            ASSERT_FALSE(started);
            EXPECT_NE(started.error().message.find("static rigid body"), std::string::npos);
        }
        floor.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        const auto valid = meshes[box_mesh];
        for(int malformed = 0; malformed < 5; ++malformed) {
            auto& mesh = meshes[box_mesh];
            mesh = valid;
            if(malformed == 0)
                mesh.indices.clear();
            if(malformed == 1)
                mesh.indices[0] = 999;
            if(malformed == 2)
                mesh.vertices[0].position.x = std::numeric_limits<float>::infinity();
            if(malformed == 3)
                for(auto& vertex : mesh.vertices)
                    vertex.position.y = 0;
            if(malformed == 4)
                for(auto& vertex : mesh.vertices)
                    vertex.position = Math::Vec3(0);
            EXPECT_FALSE(runtime.start(scene)) << malformed;
        }
        meshes[box_mesh] = valid;
        meshes[ground_mesh].indices = {0, 0, 0};
        EXPECT_FALSE(runtime.start(scene));
        meshes[ground_mesh].indices = {0, 1, 2, 0, 2, 3};
        body.edit_component<ColliderComponent>([](auto& collider) { collider.mesh = {}; });
        EXPECT_FALSE(runtime.start(scene));
    }

    class PhysicsServiceTest: public testing::Test {
    protected:
        AssetRegistry assets;
        Scene scene;
        PhysicsService physics;
        SceneRuntime runtime;
        Entity body;

        void SetUp() override {
            body = scene.create_entity("Body");
            configure_body(body);
            ASSERT_TRUE(runtime.set_services({.physics = &physics}));
            ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        }

        void add_physics_system() {
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        }

        float position_x() const { return body.get_component<TransformComponent>().translation.x; }
    };

    TEST_F(PhysicsServiceTest, CollisionResponseRequiresBothMasksAndUpdatesDuringPlay) {
        auto floor = scene.create_entity("Floor");
        configure_body(floor);
        floor.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        floor.edit_component<ColliderComponent>([](auto& collider) {
            collider.half_extents = {10, 0.5f, 10};
            collider.collision_layer = 2;
            collider.collision_mask = 1;
        });
        floor.edit_transform([](auto& transform) { transform.translation.y = -0.5f; });
        body.edit_component<ColliderComponent>([](auto& collider) { collider.collision_mask = 2; });
        body.edit_transform([](auto& transform) { transform.translation.y = 2; });
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        for(int frame = 0; frame < 150; ++frame)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_NEAR(body.get_component<TransformComponent>().translation.y, 0.5f, 0.04f);
        // 仅接收方关闭掩码，也必须解除碰撞；下一固定步生效。
        floor.edit_component<ColliderComponent>(
            [](auto& collider) { collider.collision_mask = 0; });
        for(int frame = 0; frame < 100; ++frame)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_LT(body.get_component<TransformComponent>().translation.y, -2);
        ASSERT_TRUE(runtime.stop());
        floor.edit_component<ColliderComponent>(
            [](auto& collider) { collider.collision_mask = 1; });
        body.edit_component<ColliderComponent>([](auto& collider) { collider.collision_mask = 1; });
        body.edit_transform([](auto& transform) { transform.translation.y = 2; });
        ASSERT_TRUE(runtime.start(scene));
        for(int frame = 0; frame < 150; ++frame)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_LT(body.get_component<TransformComponent>().translation.y, -2);
    }

    TEST_F(PhysicsServiceTest, QueryLayerMasksRetainHighestBitAndApplyToAllQueryKinds) {
        body.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        body.edit_component<ColliderComponent>([](auto& collider) {
            collider.collision_layer = 0x80000000u;
            collider.collision_mask = 0;
        });
        auto observer = scene.create_entity("Observer");
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        const ColliderComponent sphere{.shape = ColliderShape::Sphere, .radius = 0.25f};
        for(const uint32_t layers : {uint32_t{0}, uint32_t{1}, uint32_t{0x80000000}, UINT32_MAX}) {
            const PhysicsAccess::QueryFilter filter{.layers = layers};
            const bool matches = (layers & 0x80000000u) != 0;
            auto ray = physics.raycast({-2, 0, 0}, {4, 0, 0}, filter);
            ASSERT_TRUE(ray);
            EXPECT_EQ(ray.value().has_value(), matches);
            auto shape = physics.shape_cast(sphere, {.translation = {-2, 0, 0}}, {4, 0, 0}, filter);
            ASSERT_TRUE(shape);
            EXPECT_EQ(shape.value().has_value(), matches);
            auto overlap = physics.overlap_sphere({}, 1, filter);
            ASSERT_TRUE(overlap);
            EXPECT_EQ(overlap.value().size(), matches ? 1u : 0u);
        }
        const auto script = Script::create(R"(return {update=function()
            assert(comet.raycast({-2,0,0},{4,0,0},{layers=0x80000000}) ~= nil)
            assert(comet.raycast({-2,0,0},{4,0,0},{layers=1}) == nil)
            assert(#comet.overlap_sphere({0,0,0},1,{layers=0xffffffff}) == 1)
            assert(comet.shape_cast({shape='sphere'}, {-2,0,0}, {4,0,0}, {layers=1}) == nil)
        end})");
        ASSERT_TRUE(script);
        const auto instance = ScriptInstance::create(*script.value());
        ASSERT_TRUE(instance);
        const auto called = instance.value()->invoke(ScriptInstance::Phase::Update, observer, {},
            {.scene = &scene, .session = &runtime.get_session(), .physics = &physics});
        ASSERT_TRUE(called) << called.error().message;
    }

    TEST_F(PhysicsServiceTest, TriggerEventsRespectMasksAndReportExitWhenFilteringChanges) {
        auto trigger = scene.create_entity("Trigger");
        configure_body(trigger);
        trigger.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        trigger.edit_component<ColliderComponent>([](auto& collider) {
            collider.is_trigger = true;
            collider.half_extents = Math::Vec3(3);
            collider.collision_layer = 2;
            collider.collision_mask = 1;
        });
        body.edit_component<ColliderComponent>([](auto& collider) { collider.collision_mask = 1; });
        const auto script = Script::create(R"(return {
            on_trigger_enter=function() comet.session_set('entered', true) end,
            on_trigger_exit=function() comet.session_set('exited', true) end
        })");
        ASSERT_TRUE(script);
        ASSERT_TRUE(assets.register_asset(AssetHandle{91}, script.value()));
        body.add_component<ScriptComponent>().asset = AssetHandle{91};
        add_physics_system();
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FALSE(runtime.get_session().get_value("entered"));
        body.edit_component<ColliderComponent>([](auto& collider) { collider.collision_mask = 2; });
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(runtime.get_session().get_value("entered"), std::optional<ParameterValue>(true));
        trigger.edit_component<ColliderComponent>(
            [](auto& collider) { collider.collision_mask = 0; });
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(runtime.get_session().get_value("exited"), std::optional<ParameterValue>(true));
    }

    TEST_F(PhysicsServiceTest, QueriesReturnNearestGeometryAndFilterTriggersAndIgnoredEntity) {
        body.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        body.edit_component<ColliderComponent>([](auto& collider) {
            collider.shape = ColliderShape::Capsule;
            collider.radius = 0.5f;
            collider.height = 4;
        });
        auto further = scene.create_entity("Further");
        configure_body(further);
        further.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        further.edit_transform([](auto& transform) { transform.translation = {4, 1, 0}; });
        auto trigger = scene.create_entity("Trigger");
        configure_body(trigger);
        trigger.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        trigger.edit_component<ColliderComponent>(
            [](auto& collider) { collider.is_trigger = true; });
        trigger.edit_transform([](auto& transform) { transform.translation = {-2, 1, 0}; });
        EXPECT_FALSE(physics.raycast({-5, 1, 0}, {10, 0, 0}, {}));
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        auto ray = physics.raycast({-5, 1, 0}, {10, 0, 0}, {});
        ASSERT_TRUE(ray);
        ASSERT_TRUE(ray.value());
        EXPECT_EQ(ray.value()->entity, body.get_uuid());
        EXPECT_NEAR(ray.value()->fraction, 0.45f, 1e-5f);
        EXPECT_LT(glm::length(ray.value()->position - Math::Vec3(-0.5f, 1, 0)), 1e-5f);
        EXPECT_LT(glm::length(ray.value()->normal - Math::Vec3(-1, 0, 0)), 1e-5f);
        ray = physics.raycast({-5, 1, 0}, {10, 0, 0}, {.include_triggers = true});
        ASSERT_TRUE(ray && ray.value());
        EXPECT_EQ(ray.value()->entity, trigger.get_uuid());
        ray = physics.raycast({-5, 1, 0}, {10, 0, 0}, {.ignored = body.get_uuid()});
        ASSERT_TRUE(ray && ray.value());
        EXPECT_EQ(ray.value()->entity, further.get_uuid());
        ray = physics.raycast({0, 0, 0}, {0, 3, 0}, {});
        ASSERT_TRUE(ray && ray.value());
        EXPECT_FLOAT_EQ(ray.value()->fraction, 0);
        EXPECT_EQ(ray.value()->normal, Math::Vec3(0, -1, 0));
        ray = physics.raycast({-5, 10, 0}, {10, 0, 0}, {});
        ASSERT_TRUE(ray);
        EXPECT_FALSE(ray.value());
        auto overlap = physics.overlap_sphere({0, 1, 0}, 0.8f, {});
        ASSERT_TRUE(overlap);
        EXPECT_EQ(overlap.value(), (std::vector<EntityUuid>{body.get_uuid()}));
        EXPECT_TRUE(
            physics.overlap_sphere({0, 1, 0}, 0.8f, {.ignored = body.get_uuid()}).value().empty());
        overlap = physics.overlap_sphere({0, 1, 0}, 5, {.include_triggers = true});
        ASSERT_TRUE(overlap);
        std::vector<EntityUuid> expected{body.get_uuid(), trigger.get_uuid(), further.get_uuid()};
        std::ranges::sort(expected);
        EXPECT_EQ(overlap.value(), expected);
        EXPECT_EQ(physics.overlap_sphere({0, 1, 0}, 5, {}).value().size(), 2);
        // 同步后的形状修改必须立刻反映到查询中。
        body.edit_transform([](auto& transform) { transform.translation = {0, 10, 0}; });
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_TRUE(physics.overlap_sphere({0, 1, 0}, 0.8f, {}).value().empty());
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(physics.overlap_sphere({0, 1, 0}, 1, {}));
    }

    TEST_F(PhysicsServiceTest, ShapeCastUsesTargetContactPointsAndSupportsRotationAndFiltering) {
        body.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        body.edit_transform([](auto& transform) { transform.translation = {103, 4, -9}; });
        auto sensor = scene.create_entity("Sensor");
        configure_body(sensor);
        sensor.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        sensor.edit_component<ColliderComponent>([](auto& collider) {
            collider.half_extents = Math::Vec3(0.2f);
            collider.is_trigger = true;
        });
        sensor.edit_transform([](auto& transform) { transform.translation = {101, 4, -9}; });
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        TransformComponent start{.translation = {100, 4, -9}};
        ColliderComponent shape{.shape = ColliderShape::Sphere, .radius = 0.5f};
        auto cast = physics.shape_cast(shape, start, {5, 0, 0}, {});
        ASSERT_TRUE(cast && cast.value());
        EXPECT_EQ(cast.value()->entity, body.get_uuid());
        EXPECT_NEAR(cast.value()->fraction, 0.4f, 0.002f);
        EXPECT_NEAR(cast.value()->position.x, 102.5f, 0.002f);
        EXPECT_NEAR(cast.value()->position.y, 4, 0.002f);
        EXPECT_NEAR(cast.value()->position.z, -9, 0.002f);
        EXPECT_LT(glm::length(cast.value()->normal - Math::Vec3(-1, 0, 0)), 0.002f);
        EXPECT_TRUE(
            physics.shape_cast(shape, start, {5, 0, 0}, {.ignored = body.get_uuid()}).value()
            == std::nullopt);
        cast = physics.shape_cast(shape, start, {5, 0, 0}, {.include_triggers = true});
        ASSERT_TRUE(cast && cast.value());
        EXPECT_EQ(cast.value()->entity, sensor.get_uuid());
        EXPECT_NEAR(cast.value()->fraction, 0.06f, 0.002f);
        shape.shape = ColliderShape::Box;
        shape.half_extents = {0.25f, 0.75f, 0.25f};
        cast = physics.shape_cast(shape, start, {5, 0, 0}, {});
        ASSERT_TRUE(cast && cast.value());
        EXPECT_NEAR(cast.value()->fraction, 0.45f, 0.002f);
        start.rotation.z = 90;
        cast = physics.shape_cast(shape, start, {5, 0, 0}, {});
        ASSERT_TRUE(cast && cast.value());
        EXPECT_NEAR(cast.value()->fraction, 0.35f, 0.002f);
        shape.shape = ColliderShape::Capsule;
        shape.radius = 0.25f;
        shape.height = 2;
        cast = physics.shape_cast(shape, start, {5, 0, 0}, {});
        ASSERT_TRUE(cast && cast.value());
        EXPECT_NEAR(cast.value()->fraction, 0.3f, 0.002f);
        start.rotation = {};
        cast = physics.shape_cast(shape, start, {5, 0, 0}, {});
        ASSERT_TRUE(cast && cast.value());
        EXPECT_NEAR(cast.value()->fraction, 0.45f, 0.002f);
        cast = physics.shape_cast(shape, {.translation = {103, 4, -9}}, {}, {});
        ASSERT_TRUE(cast && cast.value());
        EXPECT_FLOAT_EQ(cast.value()->fraction, 0);
        EXPECT_TRUE(Math::is_finite(cast.value()->position));
        EXPECT_TRUE(Math::is_finite(cast.value()->normal));
        EXPECT_FALSE(physics.shape_cast(shape, {.scale = {1, 2, 1}}, {1, 0, 0}, {}));
        shape.height = shape.radius * 2;
        EXPECT_FALSE(physics.shape_cast(shape, start, {5, 0, 0}, {}));
        shape.shape = static_cast<ColliderShape>(-1);
        EXPECT_FALSE(physics.shape_cast(shape, start, {5, 0, 0}, {}));
    }

    TEST_F(PhysicsServiceTest, RepeatedSweepsReuseGeometryAndChangedSizesStillHitCorrectly) {
        body.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        const TransformComponent start{.translation = {-5, 0, 0}};
        ColliderComponent sphere{.shape = ColliderShape::Sphere, .radius = .25f};
        for(int query = 0; query < 100; ++query) {
            auto cast = physics.shape_cast(sphere, start, {10, 0, 0}, {});
            ASSERT_TRUE(cast && cast.value());
            EXPECT_NEAR(cast.value()->fraction, .425f, .002f);
        }
        const auto repeated = physics.get_statistics();
        EXPECT_EQ(repeated.query_shape_builds, 1u);
        EXPECT_EQ(repeated.query_shape_reuses, 99u);
        // 连续改变尺寸覆盖缓存容量，再查询最初尺寸，命中位置仍应由当前几何决定。
        for(int size = 1; size <= 24; ++size) {
            sphere.radius = .25f + size * .01f;
            auto cast = physics.shape_cast(sphere, start, {10, 0, 0}, {});
            ASSERT_TRUE(cast && cast.value());
            EXPECT_NEAR(cast.value()->fraction, (4.5f - sphere.radius) / 10, .002f);
        }
        sphere.radius = .25f;
        auto cast = physics.shape_cast(sphere, start, {10, 0, 0}, {});
        ASSERT_TRUE(cast && cast.value());
        EXPECT_NEAR(cast.value()->fraction, .425f, .002f);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(physics.get_statistics().query_shape_builds, 0u);
    }

    TEST_F(PhysicsServiceTest, EditingCapsuleHeightRebuildsItsCollisionShapeAtTheNextFixedStep) {
        body.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        body.edit_component<ColliderComponent>([](auto& collider) {
            collider.shape = ColliderShape::Capsule;
            collider.radius = 0.25f;
            collider.height = 1;
        });
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        auto ray = physics.raycast({-2, 1, 0}, {4, 0, 0}, {});
        ASSERT_TRUE(ray);
        EXPECT_FALSE(ray.value());
        body.edit_component<ColliderComponent>([](auto& collider) { collider.height = 4; });
        ASSERT_TRUE(runtime.advance(0.01));
        ray = physics.raycast({-2, 1, 0}, {4, 0, 0}, {});
        ASSERT_TRUE(ray && ray.value());
        EXPECT_EQ(ray.value()->entity, body.get_uuid());
        EXPECT_NEAR(ray.value()->position.x, -0.25f, 0.001f);
    }

    TEST_F(PhysicsServiceTest, QueriesRejectInvalidGeometryAndIncludeCharacterBodies) {
        auto character = scene.create_entity("Character");
        character.add_component<CharacterControllerComponent>();
        character.edit_transform([](auto& transform) { transform.translation = {3, 0, 0}; });
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        const float infinite = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(physics.raycast({}, {}, {}));
        EXPECT_FALSE(physics.raycast({infinite, 0, 0}, {1, 0, 0}, {}));
        EXPECT_FALSE(physics.raycast({}, {infinite, 0, 0}, {}));
        for(const auto radius : {0.f, -1.f, infinite})
            EXPECT_FALSE(physics.overlap_sphere({}, radius, {}));
        EXPECT_FALSE(physics.overlap_sphere({infinite, 0, 0}, 1, {}));
        const auto ray = physics.raycast({1, 1, 0}, {4, 0, 0}, {});
        ASSERT_TRUE(ray && ray.value());
        EXPECT_EQ(ray.value()->entity, character.get_uuid());
        EXPECT_NEAR(ray.value()->position.x, 2.7f, 0.01f);
        const auto overlap = physics.overlap_sphere({3, 1, 0}, 0.2f, {});
        ASSERT_TRUE(overlap);
        EXPECT_EQ(overlap.value(), (std::vector<EntityUuid>{character.get_uuid()}));
        EXPECT_TRUE(physics.overlap_sphere({3, 1, 0}, 0.2f, {.ignored = character.get_uuid()})
                .value()
                .empty());
    }

    TEST_F(PhysicsServiceTest, CapsuleRestsOnItsEndsAndSideAndRejectsInvalidDimensions) {
        auto floor = scene.create_entity("Floor");
        configure_body(floor);
        floor.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        floor.edit_component<ColliderComponent>(
            [](auto& collider) { collider.half_extents = {20, 0.5f, 20}; });
        floor.edit_transform([](auto& transform) { transform.translation.y = -0.5f; });
        body.edit_component<ColliderComponent>([](auto& collider) {
            collider.shape = ColliderShape::Capsule;
            collider.radius = 0.5f;
            collider.height = 3;
        });
        add_physics_system();
        for(const float rotation : {0.f, 90.f}) {
            body.edit_transform([&](auto& transform) {
                transform.translation = {0, 4, 0};
                transform.rotation = {0, 0, rotation};
            });
            ASSERT_TRUE(runtime.start(scene));
            for(int step = 0; step < 250; ++step)
                ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_NEAR(body.get_component<TransformComponent>().translation.y,
                rotation == 0 ? 1.5f : 0.5f, 0.04f);
            ASSERT_TRUE(runtime.stop());
        }
        for(const float height : {0.f, 1.f, std::numeric_limits<float>::infinity()}) {
            body.edit_component<ColliderComponent>(
                [&](auto& collider) { collider.height = height; });
            EXPECT_FALSE(runtime.start(scene));
            EXPECT_FALSE(runtime.is_active());
        }
        body.edit_component<ColliderComponent>([](auto& collider) { collider.height = 3; });
        body.edit_transform([](auto& transform) { transform.scale = {1, 2, 1}; });
        EXPECT_FALSE(runtime.start(scene));
    }

    TEST_F(PhysicsServiceTest, LuaQueriesReturnEntityReferencesAndValidateFilters) {
        body.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        auto observer = scene.create_entity("Observer");
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        const auto script = Script::create(R"(return {update=function()
            local hit = comet.raycast({-2,0,0}, {4,0,0})
            assert(hit and hit.entity:is_valid())
            assert(math.abs(hit.fraction - 0.375) < 0.0001)
            assert(math.abs(hit.position[1] + 0.5) < 0.0001)
            assert(hit.normal[1] == -1)
            local swept = comet.shape_cast({shape='sphere', radius=0.25}, {-2,0,0}, {4,0,0})
            assert(swept and swept.entity == hit.entity)
            assert(math.abs(swept.fraction - 0.3125) < 0.002)
            assert(math.abs(swept.position[1] + 0.5) < 0.002)
            assert(comet.shape_cast({shape='capsule',radius=0.2,height=1,rotation={0,0,90}},
                {-2,0,0}, {4,0,0}, {ignore=hit.entity}) == nil)
            assert(comet.raycast({-2,0,0}, {4,0,0}, {ignore=hit.entity}) == nil)
            local entities = comet.overlap_sphere({0,0,0}, 1)
            assert(#entities == 1 and entities[1] == hit.entity)
            assert(#comet.overlap_sphere({0,0,0}, 1, {ignore=hit.entity}) == 0)
        end})");
        ASSERT_TRUE(script);
        const auto instance = ScriptInstance::create(*script.value());
        ASSERT_TRUE(instance);
        auto invoked = instance.value()->invoke(ScriptInstance::Phase::Update, observer, {},
            {.scene = &scene, .session = &runtime.get_session(), .physics = &physics});
        ASSERT_TRUE(invoked) << invoked.error().message;
        for(const auto call : {"comet.raycast({0,0,0},{0,0,0})", "comet.raycast({0,0},{1,0,0})",
                "comet.raycast({0,0,0},{1,0,0},{include_triggers=1})",
                "comet.overlap_sphere({0,0,0},-1)", "comet.overlap_sphere({0,0,0},1,{layers=-1})",
                "comet.overlap_sphere({0,0,0},1,{layers=1.5})",
                "comet.overlap_sphere({0,0,0},1,{layers=0x100000000})",
                "comet.overlap_sphere({0,0,0},1,{unknown=true})",
                "comet.shape_cast({shape='unknown'}, {0,0,0}, {1,0,0})",
                "comet.shape_cast({shape='box',half_extents={0,1,1}}, {0,0,0}, {1,0,0})",
                "comet.shape_cast({shape='capsule',height=0.5}, {0,0,0}, {1,0,0})",
                "comet.shape_cast({shape='sphere'}, {0,0,0}, {1,0,0}, {include_triggers=1})"}) {
            const auto invalid =
                Script::create(std::string("return {update=function() ") + call + " end}");
            ASSERT_TRUE(invalid);
            const auto invalid_instance = ScriptInstance::create(*invalid.value());
            ASSERT_TRUE(invalid_instance);
            EXPECT_FALSE(invalid_instance.value()->invoke(ScriptInstance::Phase::Update, observer,
                {}, {.scene = &scene, .session = &runtime.get_session(), .physics = &physics}))
                << call;
        }
        const auto self = Script::create(R"(return {update=function()
            assert(comet.raycast({-2,0,0}, {4,0,0}) == nil)
            assert(comet.raycast({-2,0,0}, {4,0,0}, {ignore=false}).entity == comet.self_entity())
        end})");
        ASSERT_TRUE(self);
        const auto self_instance = ScriptInstance::create(*self.value());
        ASSERT_TRUE(self_instance);
        invoked = self_instance.value()->invoke(ScriptInstance::Phase::Update, body, {},
            {.scene = &scene, .session = &runtime.get_session(), .physics = &physics});
        ASSERT_TRUE(invoked) << invoked.error().message;
    }

    TEST_F(PhysicsServiceTest, RuntimeDomainsKeepBodiesImpulsesAndJoltLifetimeIndependent) {
        Scene other_scene;
        PhysicsService other_physics;
        SceneRuntime other_runtime;
        const auto other_body = other_scene.create_entity_with_uuid(body.get_uuid());
        ASSERT_TRUE(other_body);
        configure_body(other_body);
        EXPECT_EQ(body.get_id(), other_body.get_id());
        ASSERT_TRUE(other_runtime.set_services({.physics = &other_physics}));
        ASSERT_TRUE(other_runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(other_runtime.add_system(std::make_unique<PhysicsSystem>(other_physics)));
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(other_runtime.start(other_scene));
        EXPECT_FALSE(physics.request_impulse(other_body, {100, 0, 0}));
        EXPECT_FALSE(other_physics.request_impulse(body, {100, 0, 0}));
        ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_FLOAT_EQ(position_x(), 0);
        ASSERT_TRUE(other_runtime.advance(0.01));
        EXPECT_FLOAT_EQ(other_body.get_component<TransformComponent>().translation.x, 0);
        EXPECT_LT(other_body.get_component<TransformComponent>().translation.y, 0);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_GT(position_x(), 0);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(other_physics.request_impulse(other_body, {100, 0, 0}));
        ASSERT_TRUE(other_runtime.advance(0.01));
        EXPECT_GT(other_body.get_component<TransformComponent>().translation.x, 0);
        ASSERT_TRUE(runtime.start(scene));
        const auto stopped_x = position_x();
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FLOAT_EQ(position_x(), stopped_x);
    }

    TEST_F(PhysicsServiceTest, BusyPhysicsServiceRejectsAnotherOwnerWithoutClearingItsQueue) {
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
        Scene other_scene;
        AudioService other_audio(assets, AudioPlayback::Mode::Offline);
        SceneRuntime other_runtime;
        ASSERT_TRUE(other_runtime.set_services({.audio = &other_audio, .physics = &physics}));
        EXPECT_FALSE(other_runtime.start(other_scene));
        EXPECT_FALSE(other_runtime.is_active());
        EXPECT_TRUE(physics.is_bound_to(scene));
        EXPECT_FALSE(other_audio.is_bound_to(other_scene));
        EXPECT_FALSE(runtime.set_services({}));
        ASSERT_TRUE(other_runtime.set_services({}));
        ASSERT_TRUE(other_runtime.start(other_scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), 0);
    }

    TEST_F(PhysicsServiceTest, BusyAudioServiceReleasesOnlyTheNewlyAcquiredPhysicsBinding) {
        Scene audio_scene;
        AudioService audio(assets, AudioPlayback::Mode::Offline);
        SceneRuntime audio_runtime;
        ASSERT_TRUE(audio_runtime.set_services({.audio = &audio}));
        ASSERT_TRUE(audio_runtime.start(audio_scene));
        ASSERT_TRUE(runtime.set_services({.audio = &audio, .physics = &physics}));
        EXPECT_FALSE(runtime.start(scene));
        EXPECT_FALSE(physics.is_bound_to(scene));
        EXPECT_FALSE(physics.request_impulse(body, {100, 0, 0}));
        EXPECT_TRUE(audio.is_bound_to(audio_scene));
        EXPECT_TRUE(audio_runtime.is_active());
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(audio_runtime.stop());
        ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), 0);
    }

    TEST_F(PhysicsServiceTest, FailedStartupBeforeOrAfterWorldCreationDoesNotReplayCommands) {
        for(const bool before_world : {true, false}) {
            SCOPED_TRACE(before_world);
            ASSERT_TRUE(runtime.clear_systems());
            body.set_transform({});
            if(!before_world)
                add_physics_system();
            ASSERT_TRUE(runtime.add_system(std::make_unique<FailAfterCommands>(body)));
            if(before_world)
                add_physics_system();
            EXPECT_FALSE(runtime.start(scene));
            EXPECT_FALSE(runtime.is_active());
            EXPECT_FALSE(physics.is_bound_to(scene));
            EXPECT_FALSE(physics.request_impulse(body, {100, 0, 0}));
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_FLOAT_EQ(position_x(), 0);
            EXPECT_EQ(physics.get_velocity(body)->angular, Math::Vec3(0));
            ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_GT(position_x(), 0);
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST_F(PhysicsServiceTest, LuaCanQueueCommandsBeforePhysicsSystemStartsAndQueryAfterStep) {
        const auto script = Script::create(R"(
            return {on_start = function()
                comet.apply_impulse(100, 0, 0)
                comet.apply_force(0, 2000, 0)
                comet.apply_torque(0, 0, 10)
                comet.apply_impulse_at(0, 1, 0, 0.5, 0, 0)
                comet.apply_force_at(0, 10, 0, 0.5, 0, 0)
            end, update = function()
                local vx, vy, vz = comet.linear_velocity()
                local wx, wy, wz = comet.angular_velocity()
                assert(vx > 0 and vy > 0 and vz == 0)
                assert(wx == 0 and wy == 0 and wz > 0)
            end}
        )");
        ASSERT_TRUE(script) << script.error().message;
        constexpr AssetHandle handle{42};
        ASSERT_TRUE(assets.register_asset(handle, script.value()));
        body.add_component<ScriptComponent>().asset = handle;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FLOAT_EQ(position_x(), 0);
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), 0);
    }

    TEST_F(PhysicsServiceTest, ForceAndTorqueActOnceAndRespectPauseSingleStepAndStop) {
        body.edit_component<RigidBodyComponent>([](auto& rigid) { rigid.mass = 2; });
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_force(body, {200, 0, 0}));
        ASSERT_TRUE(physics.request_torque(body, {0, 0, 10}));
        ASSERT_TRUE(runtime.advance(0.005));
        ASSERT_TRUE(physics.get_velocity(body));
        EXPECT_FLOAT_EQ(physics.get_velocity(body)->linear.x, 0);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(0.2));
        EXPECT_FLOAT_EQ(physics.get_velocity(body)->angular.z, 0);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        const auto first = physics.get_velocity(body);
        ASSERT_TRUE(first);
        EXPECT_NEAR(first->linear.x, 1, 0.002f);
        EXPECT_NEAR(first->angular.z, 0.3f, 0.002f);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        ASSERT_TRUE(runtime.advance(0.03));
        const auto after = physics.get_velocity(body);
        ASSERT_TRUE(after);
        EXPECT_NEAR(after->linear.x, first->linear.x, 0.005f);
        EXPECT_NEAR(after->angular.z, first->angular.z, 0.005f);
        ASSERT_TRUE(physics.request_force(body, {200, 0, 0}));
        ASSERT_TRUE(physics.request_torque(body, {0, 0, 10}));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(physics.get_velocity(body));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FLOAT_EQ(physics.get_velocity(body)->linear.x, 0);
        EXPECT_EQ(physics.get_velocity(body)->angular, Math::Vec3(0));
    }

    TEST_F(PhysicsServiceTest, FixedStepLuaForcesProduceTheSameMotionAtDifferentDisplayRates) {
        const auto script = Script::create(R"(
            return {fixed_update = function()
                comet.apply_force(20, 0, 0)
                comet.apply_torque(0, 1, 0)
            end}
        )");
        ASSERT_TRUE(script);
        constexpr AssetHandle handle{42};
        ASSERT_TRUE(assets.register_asset(handle, script.value()));
        body.add_component<ScriptComponent>().asset = handle;
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        add_physics_system();
        std::optional<PhysicsAccess::Velocity> reference;
        Math::Vec3 reference_position(0);
        for(const auto rate : {30, 60, 120}) {
            SCOPED_TRACE(rate);
            body.set_transform({});
            ASSERT_TRUE(runtime.start(scene));
            for(int frame = 0; frame < rate; ++frame)
                ASSERT_TRUE(runtime.advance(1.0 / rate));
            const auto velocity = physics.get_velocity(body);
            ASSERT_TRUE(velocity);
            EXPECT_GT(velocity->linear.x, 19);
            EXPECT_GT(velocity->angular.y, 5);
            const auto position = body.get_component<TransformComponent>().translation;
            if(reference) {
                EXPECT_NEAR(velocity->linear.x, reference->linear.x, 1e-5f);
                EXPECT_NEAR(velocity->angular.y, reference->angular.y, 1e-5f);
                EXPECT_NEAR(position.x, reference_position.x, 1e-5f);
                EXPECT_NEAR(position.y, reference_position.y, 1e-5f);
            }
            reference = velocity;
            reference_position = position;
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST_F(PhysicsServiceTest, PointCommandsUseWorldLeverArmsAfterSyncAndActOnlyOnce) {
        add_physics_system();
        for(const auto request :
            {&PhysicsAccess::request_impulse_at, &PhysicsAccess::request_force_at}) {
            const float duration = request == &PhysicsAccess::request_force_at ? 0.01f : 1.0f;
            SCOPED_TRACE(duration);
            for(const float offset : {-0.25f, 0.0f, 0.25f}) {
                SCOPED_TRACE(offset);
                body.set_transform(
                    {.translation = {2, 10, 3}, .rotation = {0, 90, 0}, .scale = {2, 1, 1}});
                body.edit_component<RigidBodyComponent>([](auto& rigid) { rigid.mass = 1; });
                ASSERT_TRUE(runtime.start(scene));
                ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
                body.edit_transform([](auto& transform) { transform.translation.x = 5; });
                body.edit_component<RigidBodyComponent>([](auto& rigid) { rigid.mass = 2; });
                ASSERT_TRUE((physics.*request)(body, {0, 2, 0}, {5 + offset, 10, 3}));
                ASSERT_TRUE(runtime.advance(0.1));
                EXPECT_EQ(physics.get_velocity(body)->linear, Math::Vec3(0));
                EXPECT_EQ(physics.get_velocity(body)->angular, Math::Vec3(0));
                ASSERT_TRUE(runtime.request_step());
                ASSERT_TRUE(runtime.advance(0));
                const auto first = physics.get_velocity(body);
                ASSERT_TRUE(first);
                EXPECT_NEAR(first->linear.y, duration - 9.81f * 0.01f, 0.002f);
                // 绕 Y 转 90 度后，世界 Z 对应盒体短轴，I = m * (1² + 1²) / 12。
                EXPECT_NEAR(first->angular.z, 6 * offset * duration, 0.002f);
                EXPECT_NEAR(first->angular.x, 0, 1e-5f);
                EXPECT_NEAR(first->angular.y, 0, 1e-5f);
                ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
                ASSERT_TRUE(runtime.advance(0.03));
                EXPECT_NEAR(physics.get_velocity(body)->angular.z, first->angular.z, 0.01f);
                ASSERT_TRUE((physics.*request)(body, {0, 2, 0}, {6, 10, 3}));
                ASSERT_TRUE(runtime.stop());
                ASSERT_TRUE(runtime.start(scene));
                ASSERT_TRUE(runtime.advance(0.01));
                EXPECT_EQ(physics.get_velocity(body)->angular, Math::Vec3(0));
                EXPECT_LT(physics.get_velocity(body)->linear.y, 0);
                ASSERT_TRUE(runtime.stop());
            }
        }
    }

    TEST_F(PhysicsServiceTest, PointCommandsRejectInvalidCoordinatesAndLuaArguments) {
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        for(const auto request :
            {&PhysicsAccess::request_impulse_at, &PhysicsAccess::request_force_at}) {
            EXPECT_FALSE((physics.*request)(
                body, {0, 1, 0}, {std::numeric_limits<float>::infinity(), 0, 0}));
            EXPECT_FALSE((physics.*request)(
                body, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, {0, 0, 0}));
        }
        for(const char* name : {"apply_impulse_at", "apply_force_at"}) {
            for(const char* arguments : {"0,1,0", "0,1,0,0,0", "0,1,0,0,0,0,0", "0,1,0,0,0,'0'",
                    "0,1,0,0,0,0/0", "0,1,0,0,0,1/0"}) {
                SCOPED_TRACE(name);
                SCOPED_TRACE(arguments);
                const auto script = Script::create(std::string("return {update = function() comet.")
                                                   + name + "(" + arguments + ") end}");
                ASSERT_TRUE(script);
                auto instance = ScriptInstance::create(*script.value());
                ASSERT_TRUE(instance);
                EXPECT_FALSE(instance.value()->invoke(ScriptInstance::Phase::Update, body, {},
                    {.scene = &scene, .physics = &physics}));
            }
        }
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_velocity(body)->angular, Math::Vec3(0));
        EXPECT_LT(physics.get_velocity(body)->linear.y, 0);
    }

    TEST_F(PhysicsServiceTest, PointCommandsRejectLeverArmOverflowWithoutReplayingAfterFailure) {
        add_physics_system();
        for(const auto request :
            {&PhysicsAccess::request_impulse_at, &PhysicsAccess::request_force_at}) {
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE((physics.*request)(body, {0, 1, 0}, {1e20f, 0, 0}));
            const auto result = runtime.advance(0.01);
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().message.find("velocity range"), std::string::npos);
            EXPECT_FALSE(runtime.is_active());
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_EQ(physics.get_velocity(body)->angular, Math::Vec3(0));
            EXPECT_LT(physics.get_velocity(body)->linear.y, 0);
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST_F(PhysicsServiceTest, VelocityRequiresRegisteredBodyAndDoesNotFollowRecycledEntities) {
        EXPECT_FALSE(physics.get_velocity(body));
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        Scene foreign_scene;
        const auto foreign = foreign_scene.create_entity_with_uuid(body.get_uuid());
        configure_body(foreign);
        EXPECT_FALSE(physics.get_velocity(foreign));
        ASSERT_TRUE(physics.request_force(body, {200, 0, 0}));
        ASSERT_TRUE(physics.request_torque(body, {0, 0, 10}));
        const auto old = body;
        ASSERT_TRUE(physics.request_impulse_at(body, {0, 1, 0}, {1, 0, 0}));
        ASSERT_TRUE(physics.request_force_at(body, {0, 1, 0}, {1, 0, 0}));
        const auto uuid = body.get_uuid();
        scene.destroy_entity(body);
        EXPECT_FALSE(physics.get_velocity(old));
        body = scene.create_entity_with_uuid(uuid);
        configure_body(body);
        EXPECT_FALSE(physics.get_velocity(body));
        ASSERT_TRUE(runtime.advance(0.01));
        const auto velocity = physics.get_velocity(body);
        ASSERT_TRUE(velocity);
        EXPECT_FLOAT_EQ(velocity->linear.x, 0);
        EXPECT_EQ(velocity->angular, Math::Vec3(0));
        body.remove_component<ColliderComponent>();
        EXPECT_FALSE(physics.get_velocity(body));
    }

    TEST_F(PhysicsServiceTest, StaticAndKinematicVelocityUseTheSimulatedState) {
        body.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Kinematic; });
        auto stationary = scene.create_entity();
        configure_body(stationary);
        stationary.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.get_velocity(stationary));
        EXPECT_EQ(physics.get_velocity(stationary)->linear, Math::Vec3(0));
        EXPECT_EQ(physics.get_velocity(stationary)->angular, Math::Vec3(0));
        body.edit_transform([](auto& transform) {
            transform.translation.x = 1;
            transform.rotation.y = 10;
        });
        EXPECT_FLOAT_EQ(physics.get_velocity(body)->linear.x, 0);
        ASSERT_TRUE(runtime.advance(0.01));
        const auto moving = physics.get_velocity(body);
        ASSERT_TRUE(moving);
        EXPECT_NEAR(moving->linear.x, 100, 0.001f);
        EXPECT_NEAR(moving->angular.y, glm::radians(10.0f) / 0.01f, 0.001f);
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_velocity(body)->linear, Math::Vec3(0));
        EXPECT_LT(glm::length(physics.get_velocity(body)->angular), 1e-4f);
    }

    TEST_F(PhysicsServiceTest, LuaVelocityQueriesRequireRegistrationAndBorrowedAccess) {
        ASSERT_TRUE(runtime.start(scene));
        for(const char* name : {"linear_velocity", "angular_velocity"}) {
            const auto script = Script::create(std::string("local function query() comet.") + name
                                               + "() end return {update = query, on_stop = query}");
            ASSERT_TRUE(script);
            auto instance = ScriptInstance::create(*script.value());
            ASSERT_TRUE(instance);
            const auto unregistered = instance.value()->invoke(
                ScriptInstance::Phase::Update, body, {}, {.scene = &scene, .physics = &physics});
            ASSERT_FALSE(unregistered);
            EXPECT_NE(unregistered.error().message.find("registered body"), std::string::npos);
        }
        ASSERT_TRUE(runtime.stop());
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        for(const char* name : {"linear_velocity", "angular_velocity"}) {
            const auto script = Script::create(std::string("local function query() comet.") + name
                                               + "() end return {update = query, on_stop = query}");
            ASSERT_TRUE(script);
            auto instance = ScriptInstance::create(*script.value());
            ASSERT_TRUE(instance);
            ASSERT_TRUE(instance.value()->invoke(
                ScriptInstance::Phase::Update, body, {}, {.scene = &scene, .physics = &physics}));
            EXPECT_FALSE(instance.value()->invoke(
                ScriptInstance::Phase::Update, body, {}, {.scene = &scene}));
            EXPECT_FALSE(instance.value()->invoke(
                ScriptInstance::Phase::Stop, body, {}, {.scene = &scene, .physics = &physics}));
            const auto invalid = Script::create(
                std::string("return {update = function() comet.") + name + "(1) end}");
            ASSERT_TRUE(invalid);
            auto invalid_instance = ScriptInstance::create(*invalid.value());
            ASSERT_TRUE(invalid_instance);
            EXPECT_FALSE(invalid_instance.value()->invoke(
                ScriptInstance::Phase::Update, body, {}, {.scene = &scene, .physics = &physics}));
        }
    }

    TEST_F(PhysicsServiceTest, NewCommandsWakeSleepingBodies) {
        auto floor = scene.create_entity();
        configure_body(floor);
        floor.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        floor.edit_transform([](auto& transform) {
            transform.translation.y = -1;
            transform.scale = {10, 1, 10};
        });
        add_physics_system();
        for(const auto request : {&PhysicsAccess::request_force, &PhysicsAccess::request_torque}) {
            body.set_transform({});
            ASSERT_TRUE(runtime.start(scene));
            for(int step = 0; step < 300; ++step)
                ASSERT_TRUE(runtime.advance(0.01));
            ASSERT_EQ(physics.get_statistics().active_bodies, 0u);
            ASSERT_TRUE((physics.*request)(body, {0, 100, 0}));
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_GT(physics.get_statistics().active_bodies, 0u);
            const auto velocity = physics.get_velocity(body);
            ASSERT_TRUE(velocity);
            EXPECT_GT(glm::length(velocity->linear) + glm::length(velocity->angular), 0.1f);
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST_F(PhysicsServiceTest, NewCommandsRejectBackendOverflowAndDoNotReplayAfterFailure) {
        add_physics_system();
        for(const auto request : {&PhysicsAccess::request_force, &PhysicsAccess::request_torque}) {
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE((physics.*request)(body, {std::numeric_limits<float>::max(), 0, 0}));
            const auto result = runtime.advance(0.01);
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().message.find("velocity range"), std::string::npos);
            EXPECT_FALSE(runtime.is_active());
            EXPECT_FALSE(physics.get_velocity(body));
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_FLOAT_EQ(physics.get_velocity(body)->linear.x, 0);
            EXPECT_EQ(physics.get_velocity(body)->angular, Math::Vec3(0));
            ASSERT_TRUE(runtime.stop());
        }
    }

    TEST_F(PhysicsServiceTest, ScriptInvocationCannotReuseOrBorrowAnotherScenesCapability) {
        const auto script = Script::create(R"(
            return {update = function(self)
                self.calls = (self.calls or 0) + 1
                assert(self.calls == 1)
                comet.apply_impulse(100, 0, 0)
            end, on_stop = function() comet.apply_impulse(100, 0, 0) end}
        )");
        ASSERT_TRUE(script);
        auto instance = ScriptInstance::create(*script.value());
        ASSERT_TRUE(instance);
        Scene other_scene;
        SceneRuntime other_runtime;
        const auto other_body = other_scene.create_entity();
        configure_body(other_body);
        ASSERT_TRUE(other_runtime.start(other_scene));
        add_physics_system();
        const auto inactive = instance.value()->invoke(
            ScriptInstance::Phase::Update, body, {}, {.scene = &scene, .physics = &physics});
        ASSERT_FALSE(inactive);
        EXPECT_NE(inactive.error().message.find("inactive"), std::string::npos);
        ASSERT_TRUE(runtime.start(scene));
        const auto foreign = instance.value()->invoke(ScriptInstance::Phase::Update, other_body, {},
            {.scene = &other_scene, .physics = &physics});
        ASSERT_FALSE(foreign);
        EXPECT_NE(foreign.error().message.find("another scene"), std::string::npos);
        ASSERT_TRUE(instance.value()->invoke(
            ScriptInstance::Phase::Update, body, {}, {.scene = &scene, .physics = &physics}));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), 0);

        const auto impulse_script =
            Script::create("return {update = function() comet.apply_impulse(100, 0, 0) end}");
        ASSERT_TRUE(impulse_script);
        auto impulse = ScriptInstance::create(*impulse_script.value());
        ASSERT_TRUE(impulse);
        ASSERT_TRUE(impulse.value()->invoke(
            ScriptInstance::Phase::Update, body, {}, {.scene = &scene, .physics = &physics}));
        const auto missing =
            impulse.value()->invoke(ScriptInstance::Phase::Update, body, {}, {.scene = &scene});
        ASSERT_FALSE(missing);
        EXPECT_NE(
            missing.error().message.find("Physics service is unavailable"), std::string::npos);
        EXPECT_FALSE(instance.value()->invoke(
            ScriptInstance::Phase::Stop, body, {}, {.scene = &scene, .physics = &physics}));
        EXPECT_TRUE(physics.is_bound_to(scene));
    }

    TEST_F(PhysicsServiceTest, MissingServiceReportsScriptErrorAndKeepsAuthoredComponents) {
        const auto script =
            Script::create("return {on_start = function() comet.apply_impulse(100, 0, 0) end}");
        ASSERT_TRUE(script);
        constexpr AssetHandle handle{42};
        ASSERT_TRUE(assets.register_asset(handle, script.value()));
        body.add_component<ScriptComponent>().asset = handle;
        ASSERT_TRUE(runtime.set_services({}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        const auto started = runtime.start(scene);
        ASSERT_FALSE(started);
        EXPECT_NE(
            started.error().message.find("Physics service is unavailable"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_TRUE(body.has_component<RigidBodyComponent>());
        EXPECT_TRUE(body.has_component<ColliderComponent>());
        ASSERT_TRUE(runtime.set_services({.physics = &physics}));
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), 0);
    }

    TEST_F(PhysicsServiceTest, ActiveSceneCloneDoesNotCopyVelocityOrPendingImpulse) {
        add_physics_system();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
        ASSERT_TRUE(runtime.advance(0.01));
        ASSERT_TRUE(physics.request_impulse(body, {100, 0, 0}));
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto clone = serializer.clone(scene);
        ASSERT_TRUE(clone) << clone.error();
        const auto cloned_body = clone.value()->find_entity(body.get_uuid());
        ASSERT_TRUE(cloned_body);
        const auto copied_x = cloned_body.get_component<TransformComponent>().translation.x;
        PhysicsService cloned_physics;
        SceneRuntime cloned_runtime;
        ASSERT_TRUE(cloned_runtime.set_services({.physics = &cloned_physics}));
        ASSERT_TRUE(cloned_runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(cloned_runtime.add_system(std::make_unique<PhysicsSystem>(cloned_physics)));
        ASSERT_TRUE(cloned_runtime.start(*clone.value()));
        ASSERT_TRUE(cloned_runtime.advance(0.01));
        EXPECT_FLOAT_EQ(cloned_body.get_component<TransformComponent>().translation.x, copied_x);
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_GT(position_x(), copied_x);
        ASSERT_TRUE(cloned_runtime.stop());
        ASSERT_TRUE(runtime.advance(0.01));
    }
}

#include "scripting/script.h"
#include "scene/script_component.h"
#include "scene/material_parameters.h"
#include "scene/scene.h"
#include "scene/systems/script_system.h"
#include "scene/systems/physics_system.h"
#include "scene/systems/audio_system.h"
#include "scene/scene_runtime.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"
#include "asset/registry.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/serialization/material_serializer.h"
#include "audio/audio.h"
#include "core/project.h"
#include "input/input_actions.h"
#include "common/scope_exit.h"
#include "diagnostics/logger.h"
#include "render/material/material.h"
#include "render/material/material_programs.h"
#include "render/scene/scene_extractor.h"
#include "support/math_assertions.h"
#include "pbr_vert.h"
#include "pbr_frag.h"
#include <spdlog/sinks/callback_sink.h>

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace Comet::Tests {
    class ScriptSystemTest: public testing::Test {
    protected:
        const AssetHandle handle{42};
        AssetRegistry assets;
        MaterialPrograms materials{assets};
        Scene scene;
        SceneRuntime runtime;
        void SetUp() override {
            ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
            ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets, &materials)));
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

    TEST_F(ScriptSystemTest, OnStartWritesProjectMaterialBeforeFirstShaderPublication) {
        const AssetHandle material_handle{77}, program_handle{78};
        auto program = std::make_shared<ShaderProgramArtifact>();
        program->handle = program_handle;
        program->vertex_words.assign(PBR_VERT.begin(), PBR_VERT.end());
        program->fragment_words.assign(PBR_FRAG.begin(), PBR_FRAG.end());
        ASSERT_TRUE(assets.register_asset(program_handle, program));
        const auto material = std::make_shared<Material>("project", "pbr", program_handle);
        ASSERT_TRUE(assets.register_asset(material_handle, material));
        const auto revision = material->get_revision();
        source(R"(return {
            on_start = function()
                comet.set_material_scalar('roughness', 0.25)
                comet.set_material_vector('base_color', 0.2, 1, 0.25, 1)
            end
        })");
        auto entity = actor();
        entity.add_component<MeshRendererComponent>(AssetHandle{11}, material_handle);
        ASSERT_EQ(materials.published(program_handle, "pbr"), nullptr);

        const auto started = runtime.start(scene);
        ASSERT_TRUE(started) << started.error().message;
        const auto overrides = scene.get_material_overrides(entity);
        ASSERT_TRUE(overrides);
        EXPECT_FLOAT_EQ(overrides->scalar_properties.at("roughness"), 0.25f);
        EXPECT_EQ(overrides->vector_properties.at("base_color"), Math::Vec4(0.2f, 1, 0.25f, 1));
        EXPECT_EQ(materials.published(program_handle, "pbr"), nullptr);
        EXPECT_EQ(material->get_revision(), revision);
        EXPECT_FALSE(material->get_scalar_property("roughness"));
        EXPECT_FALSE(material->get_vector_property("base_color"));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_material_overrides(entity));
    }

    TEST_F(ScriptSystemTest, MaterialOverridesAreIsolatedAndRespectPauseStepAndStop) {
        const AssetHandle material_handle{77};
        const auto material = std::make_shared<Material>("shared", "pbr");
        const Math::Vec4 authored_color(1, 0.5f, 0, 1);
        ASSERT_TRUE(material->set_vector_property("base_color", authored_color));
        ASSERT_TRUE(material->set_scalar_property("roughness", 0.6f));
        ASSERT_TRUE(assets.register_asset(material_handle, material));
        const auto revision = material->get_revision();
        source(R"(return {
            on_start = function(self)
                self.roughness = 0.25
                comet.set_material_scalar('roughness', self.roughness)
                comet.set_material_vector('base_color', 0.2, 1, 0.25, 1)
            end,
            fixed_update = function(self, dt)
                self.roughness = self.roughness + dt
                comet.set_material_scalar('roughness', self.roughness)
            end,
            update = function(self)
                comet.set_material_vector('base_color', 0.2, 1, 0.25, 1)
            end
        })");
        auto target = actor();
        target.add_component<MeshRendererComponent>(AssetHandle{11}, material_handle);
        auto peer = scene.create_entity("Shared Material Peer");
        peer.add_component<MeshRendererComponent>(AssetHandle{11}, material_handle);
        EXPECT_FALSE(scene.set_material_scalar(target, "roughness", 0.25f, materials));

        ASSERT_TRUE(runtime.start(scene));
        const auto initial = scene.get_material_overrides(target);
        ASSERT_TRUE(initial);
        EXPECT_EQ(initial->material, material_handle);
        EXPECT_NE(initial->instance_id, 0u);
        EXPECT_FLOAT_EQ(initial->scalar_properties.at("roughness"), 0.25f);
        EXPECT_EQ(initial->vector_properties.at("base_color"), Math::Vec4(0.2f, 1, 0.25f, 1));
        EXPECT_FALSE(scene.get_material_overrides(peer));
        ASSERT_TRUE(scene.set_material_scalar(target, "roughness", 0.25f, materials));
        EXPECT_EQ(scene.get_material_overrides(target), initial);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(scene.get_material_overrides(target), initial);

        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(scene.get_material_overrides(target), initial);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        const auto stepped = scene.get_material_overrides(target);
        ASSERT_TRUE(stepped);
        EXPECT_NE(stepped, initial);
        EXPECT_FLOAT_EQ(stepped->scalar_properties.at("roughness"), 0.26f);
        EXPECT_FLOAT_EQ(initial->scalar_properties.at("roughness"), 0.25f);
        EXPECT_FALSE(scene.get_material_overrides(peer));
        EXPECT_EQ(material->get_revision(), revision);
        EXPECT_EQ(material->get_vector_property("base_color"), authored_color);
        EXPECT_EQ(material->get_scalar_property("roughness"), 0.6f);

        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_material_overrides(target));
        EXPECT_FALSE(scene.get_material_overrides(peer));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.get_material_overrides(target));
        EXPECT_FLOAT_EQ(
            scene.get_material_overrides(target)->scalar_properties.at("roughness"), 0.25f);
        EXPECT_NE(scene.get_material_overrides(target), stepped);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, InvalidMaterialWritesLeaveTheLastAcceptedSnapshotUnchanged) {
        const AssetHandle material_handle{77};
        const auto material = std::make_shared<Material>("shared", "pbr");
        ASSERT_TRUE(assets.register_asset(material_handle, material));
        const auto revision = material->get_revision();
        auto entity = scene.create_entity();
        entity.add_component<MeshRendererComponent>(AssetHandle{11}, material_handle);
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.set_material_scalar(entity, "roughness", 0.25f, materials));
        ASSERT_TRUE(
            scene.set_material_vector(entity, "base_color", {0.2f, 1, 0.25f, 1}, materials));
        const auto accepted = scene.get_material_overrides(entity);
        ASSERT_TRUE(accepted);
        for(const char* invalid :
            {"comet.set_material_scalar('missing', 0.5)", "comet.set_material_scalar('', 0.5)",
                "comet.set_material_scalar('roughness\\0extra', 0.5)",
                "comet.set_material_scalar('base_color', 0.5)",
                "comet.set_material_vector('roughness', 1, 1, 1, 1)",
                "comet.set_material_vector('base_color_texture', 1, 1, 1, 1)",
                "comet.set_material_scalar('roughness', 'invalid')",
                "comet.set_material_scalar('roughness', nil)",
                "comet.set_material_scalar('roughness', 0)",
                "comet.set_material_scalar('roughness', 1.01)",
                "comet.set_material_scalar('roughness', math.huge)",
                "comet.set_material_scalar('roughness', -math.huge)",
                "comet.set_material_scalar('roughness', 0/0)",
                "comet.set_material_vector('base_color', math.huge, 1, 1, 1)",
                "comet.set_material_vector('base_color', 1, -math.huge, 1, 1)",
                "comet.set_material_vector('base_color', 1, 1, 0/0, 1)",
                "comet.set_material_vector('base_color', 1, 1, 1, math.huge)",
                "comet.set_material_vector('base_color', 1, 1, 1)",
                "comet.set_material_vector('base_color', {1, 1, 1, 1})"}) {
            SCOPED_TRACE(invalid);
            const auto script =
                Script::create(std::string("return {update = function() ") + invalid + " end}");
            ASSERT_TRUE(script) << script.error().message;
            auto instance = script.value()->instantiate();
            ASSERT_TRUE(instance) << instance.error().message;
            const auto update = instance.value()->invoke(
                Script::Phase::Update, entity, {}, {.scene = &scene, .materials = &materials});
            ASSERT_FALSE(update);
            EXPECT_FALSE(update.error().message.empty());
            EXPECT_EQ(scene.get_material_overrides(entity), accepted);
            EXPECT_FLOAT_EQ(accepted->scalar_properties.at("roughness"), 0.25f);
            EXPECT_EQ(accepted->vector_properties.at("base_color"), Math::Vec4(0.2f, 1, 0.25f, 1));
            EXPECT_EQ(material->get_revision(), revision);
        }
        const auto script = Script::create(
            "return {update = function() comet.set_material_scalar('roughness', 0.5) end}");
        ASSERT_TRUE(script);
        auto without_validator = script.value()->instantiate();
        ASSERT_TRUE(without_validator);
        EXPECT_FALSE(without_validator.value()->invoke(
            Script::Phase::Update, entity, {}, {.scene = &scene}));
        EXPECT_EQ(scene.get_material_overrides(entity), accepted);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, MaterialWritesRejectMissingRendererAssetAndLayout) {
        source("return {update = function() comet.set_material_scalar('roughness', 0.25) end}");
        auto entity = actor();
        const auto expect_failure = [&] {
            ASSERT_TRUE(runtime.start(scene));
            const auto update = runtime.advance(0);
            ASSERT_FALSE(update);
            EXPECT_FALSE(update.error().message.empty());
            EXPECT_FALSE(runtime.is_active());
            EXPECT_FALSE(scene.get_material_overrides(entity));
        };
        expect_failure();
        auto& renderer = entity.add_component<MeshRendererComponent>();
        expect_failure();
        renderer.material = AssetHandle{77};
        expect_failure();
        renderer.material = handle;
        expect_failure();
        renderer.material = AssetHandle{77};
        ASSERT_TRUE(assets.register_asset(
            renderer.material, std::make_shared<Material>("unknown", "unknown")));
        expect_failure();
    }

    TEST_F(ScriptSystemTest, FailedMaterialWriteStopsRuntimeAndClearsAcceptedOverrides) {
        const AssetHandle material_handle{77};
        ASSERT_TRUE(
            assets.register_asset(material_handle, std::make_shared<Material>("shared", "pbr")));
        source(R"(return {
            on_start = function() comet.set_material_scalar('roughness', 0.25) end,
            update = function() comet.set_material_scalar('roughness', 0.5) end
        })");
        auto entity = actor();
        entity.add_component<MeshRendererComponent>(AssetHandle{11}, material_handle);
        ASSERT_TRUE(runtime.start(scene));
        const auto accepted = scene.get_material_overrides(entity);
        ASSERT_TRUE(accepted);
        ASSERT_TRUE(assets.unregister_asset(material_handle));
        EXPECT_FALSE(scene.set_material_scalar(entity, "roughness", 0.25f, materials));
        EXPECT_EQ(scene.get_material_overrides(entity), accepted);
        EXPECT_FALSE(runtime.advance(0));
        EXPECT_FALSE(runtime.is_active());
        EXPECT_FALSE(scene.get_material_overrides(entity));
        EXPECT_FLOAT_EQ(accepted->scalar_properties.at("roughness"), 0.25f);
    }

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
        auto child = scene.create_entity();
        ASSERT_TRUE(scene.set_parent(child, a));
        scene.update_world_transforms();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.02));
        EXPECT_EQ(a.get_component<TransformComponent>().translation,
            b.get_component<TransformComponent>().translation);
        EXPECT_NEAR(a.get_component<TransformComponent>().translation.y, 0.02f, 1e-6f);
        EXPECT_FLOAT_EQ(a.get_component<TransformComponent>().translation.z, 2);
        EXPECT_EQ(scene.update_world_transforms(), 3u);
        EXPECT_EQ(Math::Vec3(child.get_component<WorldTransformComponent>().world_matrix[3]),
            a.get_component<TransformComponent>().translation);
        EXPECT_EQ(scene.update_world_transforms(), 0u);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, KinematicUsesLuaFixedTargetsAndRespectsPauseStepAndRestart) {
        source(R"(
            local script = {}
            function script:fixed_update(dt)
                comet.translate(2 * dt, 0, 0)
                comet.rotate(0, 90 * dt, 0)
            end
            return script
        )");
        auto entity = actor();
        entity.add_component<RigidBodyComponent>().motion = BodyMotion::Kinematic;
        entity.add_component<ColliderComponent>();
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.02));
        const auto& transform = entity.get_component<TransformComponent>();
        EXPECT_NEAR(transform.translation.x, 0.04f, 1e-6f);
        EXPECT_FLOAT_EQ(transform.translation.y, 0);
        EXPECT_NEAR(transform.rotation.y, 1.8f, 1e-4f);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_NEAR(transform.translation.x, 0.04f, 1e-6f);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_NEAR(transform.translation.x, 0.06f, 1e-6f);
        EXPECT_NEAR(transform.rotation.y, 2.7f, 1e-4f);
        ASSERT_TRUE(runtime.stop());
        entity.set_transform({});
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_NEAR(transform.translation.x, 0.02f, 1e-6f);
        EXPECT_FLOAT_EQ(transform.translation.y, 0);
        EXPECT_NEAR(transform.rotation.y, 0.9f, 1e-4f);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, ImpulseButtonAppliesOnceAcrossMultipleFixedStepsAndHeldFrames) {
        auto actions =
            InputActions::create({{"jump", InputActions::Type::Button, {{Input::Key::J}}}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(std::move(actions).value()));
        source(R"(return {
            properties = {reference = false},
            on_start = function(self)
                if self.parameters.reference then comet.apply_impulse(0, 1, 0) end
            end,
            fixed_update = function(self)
                if not self.parameters.reference and comet.action_pressed('jump') then
                    comet.apply_impulse(0, 1, 0)
                end
            end
        })");
        auto controlled = actor();
        auto reference = actor();
        reference.get_component<ScriptComponent>().parameters["reference"] = true;
        reference.edit_transform([](auto& transform) { transform.translation.x = 3; });
        for(auto entity : {controlled, reference}) {
            entity.add_component<RigidBodyComponent>();
            entity.add_component<ColliderComponent>();
        }
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::J, true);
        float first_height = 0;
        for(int frame = 0; frame < 2; ++frame) {
            ASSERT_TRUE(runtime.advance(0.03, &input.publish_frame()));
            EXPECT_EQ(runtime.get_timing().fixed_steps, 3u);
            const auto height = controlled.get_component<TransformComponent>().translation.y;
            EXPECT_GT(height, 0);
            EXPECT_NEAR(height, reference.get_component<TransformComponent>().translation.y, 1e-5f);
            if(frame == 0)
                first_height = height;
            else
                EXPECT_LT(height, 2 * first_height);
        }
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, UpdateImpulseWaitsThroughZeroFixedFramesAndIsConsumedOnlyOnce) {
        source(R"(return {
            properties = {from_update = false},
            on_start = function(self)
                if not self.parameters.from_update then comet.apply_impulse(1, 0, 0) end
            end,
            update = function(self)
                if self.parameters.from_update and not self.requested then
                    comet.apply_impulse(1, 0, 0)
                    self.requested = true
                end
            end
        })");
        auto controlled = actor();
        controlled.get_component<ScriptComponent>().parameters["from_update"] = true;
        auto reference = actor();
        reference.edit_transform([](auto& transform) { transform.translation.z = 3; });
        for(auto entity : {controlled, reference}) {
            entity.add_component<RigidBodyComponent>();
            entity.add_component<ColliderComponent>();
        }
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        for(int frame = 0; frame < 2; ++frame) {
            ASSERT_TRUE(runtime.advance(0));
            EXPECT_EQ(runtime.get_timing().fixed_steps, 0u);
            EXPECT_FLOAT_EQ(controlled.get_component<TransformComponent>().translation.x, 0);
            EXPECT_FLOAT_EQ(reference.get_component<TransformComponent>().translation.x, 0);
        }
        float first_displacement = 0;
        for(int frame = 0; frame < 2; ++frame) {
            ASSERT_TRUE(runtime.advance(0.03));
            const auto displacement = controlled.get_component<TransformComponent>().translation.x;
            EXPECT_GT(displacement, 0);
            EXPECT_NEAR(
                displacement, reference.get_component<TransformComponent>().translation.x, 1e-5f);
            if(frame == 0)
                first_displacement = displacement;
            else
                EXPECT_LE(displacement, 2 * first_displacement + 1e-5f);
        }
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, EntityReferenceReadsAndWritesOnlyLiveSceneEntities) {
        auto target = scene.create_entity("Target");
        const auto target_uuid = target.get_uuid();
        source(std::string(R"(return {
            on_start = function(self)
                self.target = comet.find_entity(')")
               + target_uuid.to_string() + R"(')
                assert(self.target and self.target:is_valid())
                local own = comet.self_entity()
                assert(own:is_valid())
            end,
            update = function(self)
                if not self.target:is_valid() then
                    self.target:position()
                end
                local x, y, z = self.target:position()
                assert(x == 0 and y == 0 and z == 0)
                self.target:translate(2, 0, 0)
                self.target:rotate(0, 30, 0)
            end
        })");
        actor();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FLOAT_EQ(target.get_component<TransformComponent>().translation.x, 2);
        EXPECT_FLOAT_EQ(target.get_component<TransformComponent>().rotation.y, 30);

        scene.destroy_entity(target);
        auto replacement = scene.create_entity_with_uuid(target_uuid, "Replacement");
        ASSERT_TRUE(replacement);
        const auto failed = runtime.advance(0);
        ASSERT_FALSE(failed);
        EXPECT_NE(failed.error().message.find("Entity reference is stale"), std::string::npos);
        EXPECT_FLOAT_EQ(replacement.get_component<TransformComponent>().translation.x, 0);
        EXPECT_FALSE(runtime.is_active());
    }

    TEST_F(ScriptSystemTest, ScriptEntityCreationAndDestructionUseRuntimeBoundaries) {
        source(R"(return {
            on_start = function(self)
                self.spawned = comet.create_entity('Spawned')
                assert(comet.find_entity(self.spawned) == nil)
            end,
            update = function(self)
                if not self.destroyed then
                    local target = comet.find_entity(self.spawned)
                    assert(target and target:is_valid())
                    comet.destroy_entity(target)
                    assert(target:is_valid())
                    self.target = target
                    self.destroyed = true
                else
                    assert(not self.target:is_valid())
                    assert(comet.find_entity(self.spawned) == nil)
                end
            end
        })");
        actor();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_EQ(scene.entity_count(), 2u);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(scene.entity_count(), 1u);
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, CreatedMeshCapturesOnlyAssetReferencesAndOutlivesItsSource) {
        const AssetHandle mesh_handle{11}, material_handle{77};
        ASSERT_TRUE(
            assets.register_asset(material_handle, std::make_shared<Material>("shared", "pbr")));
        source(R"(return {
            on_start = function(self)
                comet.set_material_vector('base_color', 1, 0, 0, 1)
                self.created = comet.create_entity('Marker', {
                    mesh_source = comet.self_entity(),
                    translation = {4, 5, 6},
                    rotation = {0, 45, 0},
                    scale = {0.2, 0.3, 0.4},
                })
                assert(comet.find_entity(self.created) == nil)
                comet.destroy_entity(comet.self_entity())
            end
        })");
        auto parent = scene.create_entity("Parent");
        parent.edit_transform([](auto& value) { value.translation = {100, 100, 100}; });
        auto original = actor();
        const auto original_uuid = original.get_uuid();
        ASSERT_TRUE(scene.set_parent(original, parent));
        original.add_component<MeshRendererComponent>(mesh_handle, material_handle);
        original.add_component<ColliderComponent>();
        original.add_component<RigidBodyComponent>();
        original.add_component<AudioSourceComponent>();
        original.edit_transform([](auto& value) { value.scale = Math::Vec3(3); });

        const auto started = runtime.start(scene);
        ASSERT_TRUE(started) << started.error().message;
        EXPECT_FALSE(scene.find_entity(original_uuid));
        Entity marker;
        scene.each<const NameComponent>([&](Entity entity, const NameComponent& name) {
            if(name.name == "Marker")
                marker = entity;
        });
        ASSERT_TRUE(marker);
        EXPECT_FALSE(scene.get_parent(marker));
        EXPECT_FALSE(marker.has_component<ScriptComponent>());
        EXPECT_FALSE(marker.has_component<ColliderComponent>());
        EXPECT_FALSE(marker.has_component<RigidBodyComponent>());
        EXPECT_FALSE(marker.has_component<AudioSourceComponent>());
        EXPECT_FALSE(scene.get_material_overrides(marker));
        ASSERT_TRUE(marker.has_component<MeshRendererComponent>());
        const auto& mesh = marker.get_component<MeshRendererComponent>();
        EXPECT_EQ(mesh.mesh, mesh_handle);
        EXPECT_EQ(mesh.material, material_handle);
        const auto& transform = marker.get_component<TransformComponent>();
        EXPECT_EQ(transform.translation, Math::Vec3(4, 5, 6));
        EXPECT_EQ(transform.rotation, Math::Vec3(0, 45, 0));
        EXPECT_EQ(transform.scale, Math::Vec3(0.2f, 0.3f, 0.4f));
        const auto extracted = SceneExtractor::extract(scene);
        ASSERT_EQ(extracted.render_items.size(), 1u);
        EXPECT_EQ(extracted.render_items[0].entity_id, marker.get_id());
        EXPECT_EQ(extracted.render_items[0].mesh_handle, mesh_handle);
        EXPECT_EQ(extracted.render_items[0].material_handle, material_handle);
        EXPECT_FALSE(extracted.render_items[0].material_overrides);
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_TRUE(marker);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, ScriptsShareSceneSessionValuesWithoutPersistingAcrossRuns) {
        source(R"(return {
            properties = {spawn = {1, 2, 3}},
            on_start = function(self)
                comet.session_set('game.score', 1)
                comet.session_set('game.ready', true)
                comet.session_set('game.note', 'ready')
                comet.session_set('game.spawn', self.parameters.spawn)
            end,
            fixed_update = function(self)
                comet.session_set('game.score', comet.session_get('game.score') + 1)
            end
        })");
        actor();
        const AssetHandle reader_handle{43};
        auto reader_script = Script::create(R"(return {
            update = function(self)
                if self.checked then return end
                assert(comet.session_get('game.score') == 1)
                assert(comet.session_get('game.ready') == true)
                assert(comet.session_get('game.note') == 'ready')
                local spawn = comet.session_get('game.spawn')
                assert(spawn[1] == 1 and spawn[2] == 2 and spawn[3] == 3)
                spawn[1] = 100
                assert(comet.session_get('game.spawn')[1] == 1)
                comet.session_set('game.score', 2)
                comet.session_set('game.note', nil)
                assert(comet.session_get('game.note') == nil)
                self.checked = true
            end
        })",
            "reader.lua");
        ASSERT_TRUE(reader_script);
        ASSERT_TRUE(assets.register_asset(reader_handle, reader_script.value()));
        auto reader = scene.create_entity("Reader");
        reader.add_component<ScriptComponent>().asset = reader_handle;
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(std::get<float>(*scene.get_session_value("game.score")), 2.0f);
        EXPECT_FALSE(scene.set_session_value("game.target", reader.get_uuid()));
        EXPECT_FALSE(scene.get_session_value("game.target"));
        EXPECT_FALSE(scene.set_session_value("game.color", Math::Vec4(1)));
        EXPECT_FALSE(scene.get_session_value("game.color"));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(std::get<float>(*scene.get_session_value("game.score")), 2.0f);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(std::get<float>(*scene.get_session_value("game.score")), 3.0f);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.get_session_value("game.score"));
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(std::get<float>(*scene.get_session_value("game.score")), 1.0f);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, EventQueueRejectsInvalidInputsAndRecoversCapacityAfterDelivery) {
        EXPECT_FALSE(scene.emit_event("test.event"));
        ASSERT_TRUE(runtime.start(scene));
        for(const auto& name :
            {std::string{}, std::string("test\0hidden", 11), std::string(129, 'x')})
            EXPECT_FALSE(scene.emit_event(name));
        for(const ParameterValue& value :
            {ParameterValue(Math::Vec4(1)), ParameterValue(EntityUuid::generate()),
                ParameterValue(std::numeric_limits<float>::infinity()),
                ParameterValue(std::string(4097, 'x'))})
            EXPECT_FALSE(scene.emit_event("test.event", value));
        for(int index = 0; index < 1024; ++index)
            ASSERT_TRUE(scene.emit_event("test.event", static_cast<float>(index)));
        EXPECT_FALSE(scene.emit_event("test.event"));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_TRUE(scene.emit_event("test.event"));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.emit_event("test.event"));
    }

    TEST_F(ScriptSystemTest, EventsBroadcastSnapshotsInOrderAndDeferNestedEmissions) {
        source(R"(
            local script = {properties = {sender = false}, events = {
                ['test.value'] = 'on_value', ['test.nested'] = 'on_nested'}}
            function script:on_start() self.received = 0 end
            function script:update()
                if not self.parameters.sender or self.sent then return end
                self.sent = true
                local value = {1, 2, 3}
                comet.emit('test.value', value)
                value[1] = 100
                comet.emit('test.value', {4, 5, 6})
                assert(self.received == 0)
            end
            function script:on_value(value)
                self.received = self.received + 1
                assert(value[1] == 1 + 3 * (self.received - 1))
                comet.translate(value[1], value[2], value[3])
                if self.received == 1 then
                    self.first = value
                else
                    assert(self.first[1] == 1 and self.first[2] == 2 and self.first[3] == 3)
                end
                if self.parameters.sender and self.received == 1 then
                    comet.emit('test.nested', 10)
                end
            end
            function script:on_nested(value)
                assert(self.received == 2)
                comet.translate(0, 0, value)
            end
            return script
        )");
        auto sender = actor();
        sender.get_component<ScriptComponent>().parameters["sender"] = true;
        auto receiver = actor();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        for(const auto entity : {sender, receiver})
            EXPECT_EQ(entity.get_component<TransformComponent>().translation, Math::Vec3(5, 7, 9));
        ASSERT_TRUE(runtime.advance(0));
        for(const auto entity : {sender, receiver})
            EXPECT_EQ(entity.get_component<TransformComponent>().translation, Math::Vec3(5, 7, 19));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(receiver.get_component<TransformComponent>().translation, Math::Vec3(5, 7, 19));
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, EventsWaitForUpdatesAndRespectPauseStepAndStop) {
        source(R"(
            local script = {events = {
                ['test.start'] = 'on_started', ['test.tick'] = 'on_tick',
                ['test.marker'] = 'on_marker'}}
            function script:on_start()
                self.fixed_ticks = 0
                self.updated_ticks = 0
                comet.emit('test.start')
            end
            function script:fixed_update()
                self.fixed_ticks = self.fixed_ticks + 1
                comet.emit('test.tick')
            end
            function script:update() self.updated_ticks = self.fixed_ticks end
            function script:on_started(value)
                assert(value == nil)
                comet.translate(1, 0, 0)
            end
            function script:on_tick()
                assert(self.updated_ticks == self.fixed_ticks)
                comet.translate(0, 1, 0)
            end
            function script:on_marker(value) comet.translate(0, 0, value) end
            return script
        )");
        auto entity = actor();
        const auto& translation = entity.get_component<TransformComponent>().translation;
        EXPECT_FALSE(scene.emit_event("test.marker", 100.0f));
        ASSERT_TRUE(runtime.start(scene, SceneRuntime::State::Paused));
        EXPECT_EQ(translation, Math::Vec3(0));
        ASSERT_TRUE(scene.emit_event("test.marker", 2.0f));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(translation, Math::Vec3(0));
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(translation, Math::Vec3(1, 1, 2));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(translation, Math::Vec3(1, 1, 2));
        ASSERT_TRUE(scene.emit_event("test.marker", 100.0f));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(scene.emit_event("test.marker", 100.0f));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(translation, Math::Vec3(2, 1, 2));
        ASSERT_TRUE(runtime.advance(0.03));
        EXPECT_EQ(translation, Math::Vec3(2, 4, 2));
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, EventsUseLiveSubscriptionsAtDeliveryAfterEntityAndComponentChanges) {
        source(R"(
            local script = {properties = {amount = 1}, events = {['test.ping'] = 'on_ping'}}
            function script:on_start() self.amount = self.parameters.amount end
            function script:on_ping() comet.translate(self.amount, 0, 0) end
            return script
        )");
        auto original = actor();
        const auto uuid = original.get_uuid();
        auto removed = actor();
        auto replaced = actor();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.emit_event("test.ping"));

        scene.destroy_entity(original);
        auto replacement = scene.create_entity_with_uuid(uuid);
        auto& replacement_script = replacement.add_component<ScriptComponent>();
        replacement_script.asset = handle;
        replacement_script.parameters["amount"] = 5.0f;
        removed.remove_component<ScriptComponent>();
        replaced.remove_component<ScriptComponent>();
        auto& changed = replaced.add_component<ScriptComponent>();
        changed.asset = handle;
        changed.parameters["amount"] = 9.0f;

        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FLOAT_EQ(replacement.get_component<TransformComponent>().translation.x, 5);
        EXPECT_FLOAT_EQ(replaced.get_component<TransformComponent>().translation.x, 9);
        EXPECT_FLOAT_EQ(removed.get_component<TransformComponent>().translation.x, 0);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, FailedEventHandlerStopsRuntimeAndClearsPendingWork) {
        source(R"(
            local script = {events = {['test.fail'] = 'on_failure', ['test.next'] = 'on_next'}}
            function script:on_failure()
                comet.create_entity('Discarded')
                comet.session_set('event.called', true)
                comet.emit('test.next')
                comet.restart_scene()
                error('event handler failed')
            end
            function script:on_next() comet.translate(100, 0, 0) end
            return script
        )");
        auto entity = actor();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.emit_event("test.fail"));
        ASSERT_TRUE(scene.emit_event("test.next"));
        const auto advanced = runtime.advance(0);
        ASSERT_FALSE(advanced);
        EXPECT_NE(advanced.error().message.find("event handler failed"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_EQ(scene.entity_count(), 1u);
        EXPECT_FALSE(scene.get_session_value("event.called"));
        EXPECT_FALSE(scene.take_restart_request());
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().translation.x, 0);
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_FLOAT_EQ(entity.get_component<TransformComponent>().translation.x, 0);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, TriggerNotificationsReachBothParticipantsAndIgnoreOtherScripts) {
        ASSERT_TRUE(runtime.clear_systems());
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>()));
        source(R"(return {
            on_trigger_enter = function(self, other)
                assert(other:is_valid())
                comet.translate(1, 0, 0)
            end,
            on_trigger_exit = function(self, other)
                assert(other:is_valid())
                comet.translate(2, 0, 0)
            end
        })");
        auto sensor = actor();
        sensor.add_component<RigidBodyComponent>().motion = BodyMotion::Static;
        sensor.add_component<ColliderComponent>().is_trigger = true;
        auto unrelated = actor();
        unrelated.edit_transform(
            [](TransformComponent& transform) { transform.translation = {20, 0, 0}; });
        auto target = actor();
        target.add_component<RigidBodyComponent>();
        target.add_component<ColliderComponent>();
        target.edit_transform(
            [](TransformComponent& transform) { transform.translation = {0, 0.3f, 0}; });
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FLOAT_EQ(sensor.get_component<TransformComponent>().translation.x, 1);
        EXPECT_FLOAT_EQ(target.get_component<TransformComponent>().translation.x, 1);
        EXPECT_FLOAT_EQ(unrelated.get_component<TransformComponent>().translation.x, 20);
        target.edit_transform(
            [](TransformComponent& transform) { transform.translation = {10, 3, 0}; });
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FLOAT_EQ(sensor.get_component<TransformComponent>().translation.x, 3);
        EXPECT_FLOAT_EQ(target.get_component<TransformComponent>().translation.x, 12);
        EXPECT_FLOAT_EQ(unrelated.get_component<TransformComponent>().translation.x, 20);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, FailedContactCallbackStopsRuntimeAndClearsNotifications) {
        ASSERT_TRUE(runtime.clear_systems());
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>()));
        source(R"(return {
            on_collision_enter = function(self, other)
                assert(other:is_valid())
                error('contact failed')
            end
        })");
        auto floor = actor();
        floor.add_component<RigidBodyComponent>().motion = BodyMotion::Static;
        floor.add_component<ColliderComponent>();
        auto falling = scene.create_entity("Falling");
        falling.add_component<RigidBodyComponent>();
        falling.add_component<ColliderComponent>();
        falling.edit_transform(
            [](TransformComponent& transform) { transform.translation = {0, 0.3f, 0}; });
        ASSERT_TRUE(runtime.start(scene));
        const auto advanced = runtime.advance(0.01);
        ASSERT_FALSE(advanced);
        EXPECT_NE(advanced.error().message.find("contact failed"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_TRUE(scene.get_contact_events().empty());
    }

    TEST_F(ScriptSystemTest, ContactForEntityDestroyedAtFixedBoundaryIsNotDelivered) {
        class DestroyAfterPhysics final: public System {
        public:
            explicit DestroyAfterPhysics(Entity target) : target(target) {}
            Result<void, Error> fixed_update(Scene& scene, const Context&) override {
                if(!scene.request_destroy_entity(target))
                    return Result<void, Error>::failure({"Cannot queue entity destruction"});
                return Result<void, Error>::success();
            }
            Entity target;
        };
        ASSERT_TRUE(runtime.clear_systems());
        ASSERT_TRUE(runtime.add_system(std::make_unique<ScriptSystem>(assets)));
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>()));
        source(R"(return {
            on_collision_enter = function(self, other)
                error('stale contact delivered')
            end
        })");
        auto floor = actor();
        floor.add_component<RigidBodyComponent>().motion = BodyMotion::Static;
        floor.add_component<ColliderComponent>();
        auto falling = scene.create_entity("Falling");
        falling.add_component<RigidBodyComponent>();
        falling.add_component<ColliderComponent>();
        falling.edit_transform(
            [](TransformComponent& transform) { transform.translation = {0, 0.3f, 0}; });
        ASSERT_TRUE(runtime.add_system(std::make_unique<DestroyAfterPhysics>(falling)));
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FALSE(falling);
        EXPECT_TRUE(scene.get_contact_events().empty());
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, FailedScriptClearsPendingRuntimeRequestsAndSessionState) {
        source(R"(return {
            update = function(self)
                comet.create_entity('Discarded')
                comet.session_set('game.score', 1)
                comet.restart_scene()
                error('script failed')
            end
        })");
        actor();
        ASSERT_TRUE(runtime.start(scene));
        const auto failed = runtime.advance(0);
        ASSERT_FALSE(failed);
        EXPECT_NE(failed.error().message.find("script failed"), std::string::npos);
        EXPECT_EQ(scene.entity_count(), 1u);
        EXPECT_FALSE(scene.get_session_value("game.score"));
        EXPECT_FALSE(scene.take_restart_request());
        EXPECT_FALSE(runtime.is_active());
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

    TEST_F(ScriptSystemTest, SourceReloadMigratesSharedInstancesAtTheUpdateBoundary) {
        source(R"(return {
            properties = {retained = 1, removed = 2, changed = 3},
            on_start = function(self) self.private_value = 99 end,
            update = function() comet.translate(1, 0, 0) end
        })");
        auto first = actor();
        auto second = actor();
        for(auto entity : {first, second})
            entity.get_component<ScriptComponent>().parameters = {
                {"retained", 3.0f}, {"removed", 7.0f}, {"changed", 8.0f}};
        second.get_component<ScriptComponent>().parameters["retained"] = 6.0f;
        ASSERT_TRUE(runtime.start(scene));
        const auto old_script = first.get_component<ScriptComponent>().running_script();
        const auto lifetime = first.get_component<ScriptComponent>().lifetime();
        ASSERT_TRUE(runtime.advance(0));
        source(R"(return {
            properties = {retained = 100, changed = false, added = 'new'},
            on_start = function(self)
                assert(self.private_value == nil)
                self.private_value = 0
                assert(self.parameters.removed == nil)
                assert(self.parameters.changed == false and self.parameters.added == 'new')
                comet.translate(0, 1, 0)
            end,
            update = function(self)
                self.private_value = self.private_value + 1
                comet.translate(self.parameters.retained, 0, self.private_value)
            end
        })");
        const auto replacement = assets.resolve<Script>(handle);
        ASSERT_NE(replacement, old_script);
        EXPECT_EQ(first.get_component<ScriptComponent>().running_script(), old_script);
        EXPECT_EQ(second.get_component<ScriptComponent>().running_script(), old_script);
        ASSERT_TRUE(runtime.advance(0));
        for(const auto entity : {first, second}) {
            const auto& component = entity.get_component<ScriptComponent>();
            EXPECT_EQ(component.running_script(), replacement);
            ASSERT_EQ(component.parameters.size(), 1u);
            EXPECT_TRUE(component.parameters.contains("retained"));
        }
        EXPECT_EQ(first.get_component<ScriptComponent>().lifetime(), lifetime);
        EXPECT_EQ(first.get_component<TransformComponent>().translation, Math::Vec3(4, 1, 1));
        EXPECT_EQ(second.get_component<TransformComponent>().translation, Math::Vec3(7, 1, 1));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(first.get_component<TransformComponent>().translation, Math::Vec3(7, 1, 3));
        EXPECT_EQ(second.get_component<TransformComponent>().translation, Math::Vec3(13, 1, 3));
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, SourceReloadWaitsWhilePausedAndStartsOnceAcrossFixedSteps) {
        source("return {}");
        const auto entity = actor();
        ASSERT_TRUE(runtime.start(scene));
        const auto old_script = entity.get_component<ScriptComponent>().running_script();
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        source(R"(return {
            on_start = function() comet.translate(1, 0, 0) end,
            fixed_update = function() comet.translate(0, 1, 0) end,
            update = function() comet.translate(0, 0, 1) end
        })");
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(entity.get_component<ScriptComponent>().running_script(), old_script);
        EXPECT_EQ(entity.get_component<TransformComponent>().translation, Math::Vec3(0));
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(entity.get_component<ScriptComponent>().running_script(),
            assets.resolve<Script>(handle));
        EXPECT_EQ(entity.get_component<TransformComponent>().translation, Math::Vec3(1, 1, 1));
        ASSERT_TRUE(runtime.advance(1));
        EXPECT_EQ(entity.get_component<TransformComponent>().translation, Math::Vec3(1, 1, 1));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        ASSERT_TRUE(runtime.advance(0.03));
        EXPECT_EQ(entity.get_component<TransformComponent>().translation, Math::Vec3(1, 4, 2));
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, SourceReloadUsesNewEventDeclarationsForPendingNotifications) {
        source(R"(return {
            events = {['test.before'] = 'before'},
            before = function() comet.translate(1, 0, 0) end
        })");
        const auto entity = actor();
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.emit_event("test.before"));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(entity.get_component<TransformComponent>().translation, Math::Vec3(1, 0, 0));
        ASSERT_TRUE(scene.emit_event("test.before"));
        ASSERT_TRUE(scene.emit_event("test.after", 5.0f));
        source(R"(return {
            events = {['test.after'] = 'after'},
            after = function(self, value) comet.translate(0, value, 0) end
        })");
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(entity.get_component<TransformComponent>().translation, Math::Vec3(1, 5, 0));
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, FailedReloadStartStopsRuntimeAndClearsPendingWork) {
        source("return {}");
        const auto first = actor();
        const auto second = actor();
        ASSERT_TRUE(runtime.start(scene));
        source(R"(return {
            on_start = function()
                local started = comet.session_get('reload.started') or 0
                comet.session_set('reload.started', started + 1)
                comet.create_entity('Discarded')
                comet.session_set('reload.pending', true)
                comet.emit('reload.pending')
                if started == 1 then error('reload start failed') end
            end
        })");
        const auto reloaded = runtime.advance(0);
        ASSERT_FALSE(reloaded);
        EXPECT_NE(reloaded.error().message.find("reload start failed"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_FALSE(first.get_component<ScriptComponent>().running_script());
        EXPECT_FALSE(second.get_component<ScriptComponent>().running_script());
        EXPECT_EQ(scene.entity_count(), 2u);
        EXPECT_FALSE(scene.get_session_value("reload.pending"));
        source(R"(return {
            on_start = function()
                assert(comet.session_get('reload.started') == nil)
                assert(comet.session_get('reload.pending') == nil)
            end,
            events = {['reload.pending'] = 'stale'},
            stale = function() error('old reload notification survived') end
        })");
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(scene.entity_count(), 2u);
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, CleanupRunsOnceInReverseActualStartOrderEvenAfterFailure) {
        Config::Log config;
        config.enable_file_logging = false;
        Logger::init(config);
        const auto logger = Logger::get_console_logger();
        std::vector<std::string> messages;
        bool retiring_old_instances = false;
        const auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [&](const spdlog::details::log_msg& message) {
                const std::string_view text(message.payload.data(), message.payload.size());
                if(!text.starts_with("Script cleanup failed:"))
                    return;
                if(retiring_old_instances)
                    EXPECT_FALSE(scene.get_session_value("reload.started"));
                messages.emplace_back(text);
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
        source(R"(return {
            properties = {label = 'first'},
            on_start = function() comet.session_set('reload.started', true) end,
            on_stop = function(self) error(self.parameters.label) end
        })");
        retiring_old_instances = true;
        ASSERT_TRUE(runtime.advance(0));
        retiring_old_instances = false;
        ASSERT_EQ(messages.size(), 2u);
        EXPECT_NE(messages[0].find("second"), std::string::npos);
        EXPECT_NE(messages[1].find("first"), std::string::npos);
        EXPECT_TRUE(scene.get_session_value("reload.started"));
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(messages.size(), 4u);
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

    TEST_F(ScriptSystemTest, NamedActionsUseAuthorizedFrameAndUpdateDelta) {
        auto actions =
            InputActions::create({{"move", InputActions::Type::Axis, {{Input::Key::W}}}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(std::move(actions).value()));
        source(R"(return {
            update = function(self, dt)
                comet.translate(0, 0, comet.action_value('move') * dt)
            end
        })");
        auto entity = actor();
        ASSERT_TRUE(runtime.start(scene));
        Input::Frame input{.serial = 1, .focused = true};
        input.keys[static_cast<size_t>(Input::Key::W)].down = true;
        ASSERT_TRUE(runtime.advance(0.1, &input));
        EXPECT_NEAR(entity.get_component<TransformComponent>().translation.z, 0.1f, 1e-6f);
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_NEAR(entity.get_component<TransformComponent>().translation.z, 0.1f, 1e-6f);
    }

    TEST_F(ScriptSystemTest, NamedActionsUseProjectBindingsAndDoNotRepeatAcrossFixedSteps) {
        auto actions =
            InputActions::create({{"jump", InputActions::Type::Button, {{Input::Key::J}}},
                {"move", InputActions::Type::Axis, {{Input::Key::L}}}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(std::move(actions).value()));
        source(R"(return {
            fixed_update = function(self, dt)
                if comet.action_pressed('jump') then comet.translate(1,0,0) end
                if comet.action_released('jump') then comet.translate(0,1,0) end
                comet.translate(0,0,comet.action_value('move') * dt)
            end,
            update = function(self)
                assert(not comet.action_down('jump'))
            end
        })");
        auto entity = actor();
        ASSERT_TRUE(runtime.start(scene));
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::J, true);
        input.key_event(Input::Key::J, false);
        input.key_event(Input::Key::L, true);
        ASSERT_TRUE(runtime.advance(0.001, &input.publish_frame()));
        ASSERT_TRUE(runtime.advance(0.03, &input.publish_frame()));
        const auto& position = entity.get_component<TransformComponent>().translation;
        EXPECT_FLOAT_EQ(position.x, 1);
        EXPECT_FLOAT_EQ(position.y, 1);
        EXPECT_NEAR(position.z, 0.03f, 1e-6f);
        ASSERT_TRUE(runtime.advance(0.02));
        EXPECT_NEAR(position.z, 0.03f, 1e-6f);
        ASSERT_TRUE(runtime.stop());
        source("return {update = function(self) comet.action_down('typo') end}");
        ASSERT_TRUE(runtime.start(scene));
        auto failed = runtime.advance(0);
        ASSERT_FALSE(failed);
        EXPECT_NE(failed.error().message.find("Unknown input action: typo"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
    }

    TEST_F(ScriptSystemTest, InputContextChangesWaitUntilTheNextFrameBoundary) {
        auto actions =
            InputActions::create({{"move", InputActions::Type::Axis, {{Input::Key::L}}, "gameplay"},
                                     {"common", InputActions::Type::Button, {{Input::Key::K}}}},
                {{"gameplay", true}});
        ASSERT_TRUE(actions);
        ASSERT_TRUE(runtime.set_input_actions(std::move(actions).value()));
        source(R"(return {
            fixed_update = function(self)
                if not self.requested then
                    comet.set_input_context('gameplay', false)
                    self.requested = true
                end
                comet.translate(comet.action_value('move'), 0, 0)
            end,
            update = function()
                comet.translate(0, comet.action_value('move'), 0)
                if comet.action_pressed('common') then comet.translate(0, 0, 1) end
            end
        })");
        const auto entity = actor();
        ASSERT_TRUE(runtime.start(scene));
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::L, true);
        ASSERT_TRUE(runtime.advance(0.03, &input.publish_frame()));
        EXPECT_EQ(runtime.get_timing().fixed_steps, 3u);
        EXPECT_EQ(entity.get_component<TransformComponent>().translation, Math::Vec3(3, 1, 0));

        input.key_event(Input::Key::K, true);
        ASSERT_TRUE(runtime.advance(0.02, &input.publish_frame()));
        EXPECT_EQ(entity.get_component<TransformComponent>().translation, Math::Vec3(3, 1, 1));
        input.key_event(Input::Key::K, false);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01, &input.publish_frame()));
        EXPECT_EQ(entity.get_component<TransformComponent>().translation, Math::Vec3(4, 2, 1));
    }

    TEST_F(ScriptSystemTest, DemoProjectAndLuaSourceUseTheSameToggleContract) {
        const auto project = Project::load(
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "demo");
        ASSERT_TRUE(project) << project.error();
        ASSERT_TRUE(runtime.set_input_actions(project.value().input_actions()));
        const std::array<std::filesystem::path, 1> roots{"scripts/spin.lua"};
        auto scripts = Script::load_group(project.value().paths().assets(), roots);
        ASSERT_TRUE(scripts) << scripts.error().message;
        ASSERT_TRUE(assets.register_asset(handle, scripts.value().front()));
        auto entity = actor();
        const auto& rotation = entity.get_component<TransformComponent>().rotation;
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FLOAT_EQ(rotation.y, 1);
        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        ASSERT_TRUE(runtime.advance(0.03, &input.publish_frame()));
        EXPECT_FLOAT_EQ(rotation.y, 1);
        input.key_event(Input::Key::Space, false);
        ASSERT_TRUE(runtime.advance(0.01, &input.publish_frame()));
        input.key_event(Input::Key::Space, true);
        ASSERT_TRUE(runtime.advance(0.02, &input.publish_frame()));
        EXPECT_FLOAT_EQ(rotation.y, 3);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FLOAT_EQ(rotation.y, 4);
    }

    TEST_F(ScriptSystemTest, DemoGoalAndRestartRestoreAnIsolatedRunFromTheAuthoredScene) {
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project) << project.error();
        ASSERT_TRUE(runtime.set_input_actions(project.value().input_actions()));
        const auto material_path = project.value().paths().assets() / "materials/cube.mat";
        const auto authored = MaterialSerializer{}.load(material_path);
        ASSERT_TRUE(authored) << authored.error();
        const auto material = std::make_shared<Material>("cube", authored.value().template_name);
        for(const auto& [name, value] : authored.value().scalar_properties)
            ASSERT_TRUE(material->set_scalar_property(name, value));
        for(const auto& [name, value] : authored.value().vector_properties)
            ASSERT_TRUE(material->set_vector_property(name, value));
        const AssetHandle material_handle{6482638524486200214ULL};
        ASSERT_TRUE(assets.register_asset(material_handle, material));
        const std::array<std::filesystem::path, 4> roots{"scripts/spin.lua",
            "scripts/move_cube.lua", "scripts/collect_goal.lua", "scripts/impulse_cube.lua"};
        const std::array handles{AssetHandle{7821648321594001021},
            AssetHandle{14309634625000312001ULL}, AssetHandle{14309634625000312002ULL},
            AssetHandle{14309634625000312003ULL}};
        auto scripts = Script::load_group(project.value().paths().assets(), roots);
        ASSERT_TRUE(scripts) << scripts.error().message;
        for(size_t index = 0; index < handles.size(); ++index)
            ASSERT_TRUE(assets.register_asset(handles[index], scripts.value()[index]));
        auto cue = AudioClip::load(project.value().paths().assets() / "audio/play_chime.wav");
        ASSERT_TRUE(cue) << cue.error().message;
        ASSERT_TRUE(
            assets.register_asset(AssetHandle{8247160951280394421ULL}, std::move(cue).value()));
        const auto components = create_scene_component_registry();
        const SceneSerializer serializer(components);
        auto edit_scene =
            serializer.load((project.value().paths().assets() / "scenes/default.scene").string());
        ASSERT_TRUE(edit_scene) << edit_scene.error();
        const auto center_uuid = EntityUuid::parse("672cd0cc-501f-419e-af5e-a883a0cd3d02");
        const auto player_uuid = EntityUuid::parse("672cd0cc-501f-419e-af5e-a883a0cd3d05");
        const auto impulse_uuid = EntityUuid::parse("672cd0cc-501f-419e-af5e-a883a0cd3d08");
        ASSERT_TRUE(center_uuid);
        ASSERT_TRUE(player_uuid);
        ASSERT_TRUE(impulse_uuid);
        const Math::Vec4 score_color(1, 0.15f, 0.5f, 1);
        auto edit_center = edit_scene.value()->find_entity(*center_uuid);
        ASSERT_TRUE(edit_center);
        edit_center.get_component<ScriptComponent>().parameters["score_color"] = score_color;
        auto playing = serializer.clone(*edit_scene.value());
        ASSERT_TRUE(playing) << playing.error();
        const ScopeExit stop_playing([&] { EXPECT_TRUE(runtime.stop()); });
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>()));
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AudioSystem>(assets, AudioPlayback::Mode::Offline)));
        ASSERT_TRUE(runtime.start(*playing.value()));

        Input input;
        input.focus_event(true);
        auto impulse_cube = playing.value()->find_entity(*impulse_uuid);
        ASSERT_TRUE(impulse_cube);
        const auto center = playing.value()->find_entity(*center_uuid);
        const auto player = playing.value()->find_entity(*player_uuid);
        ASSERT_TRUE(center);
        ASSERT_TRUE(player);
        const auto initial_height = impulse_cube.get_component<TransformComponent>().translation.y;
        input.key_event(Input::Key::J, true);
        ASSERT_TRUE(runtime.advance(0.03, &input.publish_frame()));
        EXPECT_GT(impulse_cube.get_component<TransformComponent>().translation.y, initial_height);
        input.key_event(Input::Key::J, false);
        input.key_event(Input::Key::Right, true);
        bool score_feedback_observed = false;
        for(int frame = 0; frame < 120; ++frame) {
            ASSERT_TRUE(runtime.advance(0.01, &input.publish_frame()));
            if(playing.value()->get_session_value("demo.score")) {
                EXPECT_FLOAT_EQ(center.get_component<TransformComponent>().translation.y, 0.4f);
                const auto tint = playing.value()->get_material_overrides(center);
                ASSERT_TRUE(tint);
                EXPECT_EQ(tint->vector_properties.at("base_color"), score_color);
                score_feedback_observed = true;
                break;
            }
        }
        ASSERT_TRUE(score_feedback_observed);
        const auto player_position_at_score =
            player.get_component<TransformComponent>().translation;
        ASSERT_TRUE(runtime.advance(0.03, &input.publish_frame()));
        EXPECT_EQ(player.get_component<TransformComponent>().translation, player_position_at_score);

        const auto goal_uuid = EntityUuid::parse("672cd0cc-501f-419e-af5e-a883a0cd3d07");
        ASSERT_TRUE(goal_uuid);
        EXPECT_FALSE(playing.value()->find_entity(*goal_uuid));
        const auto score = playing.value()->get_session_value("demo.score");
        ASSERT_TRUE(score);
        EXPECT_FLOAT_EQ(std::get<float>(*score), 1);
        Entity marker;
        std::size_t marker_count = 0;
        playing.value()->each<const NameComponent>([&](Entity entity, const NameComponent& name) {
            if(name.name == "Collected_Goal_1") {
                marker = entity;
                ++marker_count;
            }
        });
        ASSERT_TRUE(marker);
        EXPECT_EQ(marker_count, 1u);
        const auto edit_goal = edit_scene.value()->find_entity(*goal_uuid);
        ASSERT_TRUE(edit_goal);
        ASSERT_TRUE(marker.has_component<MeshRendererComponent>());
        const auto& marker_mesh = marker.get_component<MeshRendererComponent>();
        const auto& goal_mesh = edit_goal.get_component<MeshRendererComponent>();
        EXPECT_EQ(marker_mesh.mesh, goal_mesh.mesh);
        EXPECT_EQ(marker_mesh.material, goal_mesh.material);
        EXPECT_FALSE(edit_scene.value()->find_entity(marker.get_uuid()));
        EXPECT_FALSE(edit_scene.value()->get_material_overrides(
            edit_scene.value()->find_entity(*center_uuid)));
        input.key_event(Input::Key::Right, false);
        input.key_event(Input::Key::R, true);
        ASSERT_TRUE(runtime.advance(0, &input.publish_frame()));
        EXPECT_TRUE(playing.value()->take_restart_request());
        EXPECT_FALSE(playing.value()->find_entity(*goal_uuid));
        ASSERT_TRUE(runtime.stop());
        EXPECT_TRUE(edit_scene.value()->find_entity(*goal_uuid));
        auto restarted = serializer.clone(*edit_scene.value());
        ASSERT_TRUE(restarted) << restarted.error();
        const ScopeExit stop_restarted([&] { EXPECT_TRUE(runtime.stop()); });
        EXPECT_FALSE(restarted.value()->find_entity(marker.get_uuid()));
        ASSERT_TRUE(runtime.start(
            *restarted.value(), SceneRuntime::State::Running, SceneRuntime::InputStart::Rebase));
        EXPECT_TRUE(restarted.value()->find_entity(*goal_uuid));
        EXPECT_FALSE(restarted.value()->get_session_value("demo.score"));
        EXPECT_FALSE(restarted.value()->get_material_overrides(
            restarted.value()->find_entity(*center_uuid)));
        const auto restarted_player = restarted.value()->find_entity(*player_uuid);
        const auto authored_player_position = edit_scene.value()
                                                  ->find_entity(*player_uuid)
                                                  .get_component<TransformComponent>()
                                                  .translation;
        ASSERT_TRUE(restarted_player);
        EXPECT_EQ(restarted_player.get_component<TransformComponent>().translation,
            authored_player_position);
        input.key_event(Input::Key::Right, true);
        ASSERT_TRUE(runtime.advance(0.03, &input.publish_frame()));
        EXPECT_FALSE(restarted.value()->take_restart_request());
        EXPECT_GT(restarted_player.get_component<TransformComponent>().translation.x,
            authored_player_position.x);
        input.key_event(Input::Key::Right, false);
        input.key_event(Input::Key::R, false);
        ASSERT_TRUE(runtime.advance(0.01, &input.publish_frame()));
        input.key_event(Input::Key::R, true);
        ASSERT_TRUE(runtime.advance(0.01, &input.publish_frame()));
        EXPECT_TRUE(restarted.value()->take_restart_request());
        ASSERT_TRUE(runtime.stop());
    }

    TEST_F(ScriptSystemTest, DemoGoalIgnoresContactsWithUnassignedActors) {
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project);
        const std::array<std::filesystem::path, 1> roots{"scripts/collect_goal.lua"};
        auto scripts = Script::load_group(project.value().paths().assets(), roots);
        ASSERT_TRUE(scripts) << scripts.error().message;
        auto instance = scripts.value().front()->instantiate();
        ASSERT_TRUE(instance);
        const auto goal = scene.create_entity("Goal");
        const auto player = scene.create_entity("Player");
        const auto other = scene.create_entity("Other");
        for(const auto target : {player.get_uuid(), EntityUuid{}}) {
            ASSERT_TRUE(instance.value()->invoke(Script::Phase::TriggerEnter, goal,
                {{"player", target}}, {.scene = &scene, .contact_other = other}));
        }
        EXPECT_TRUE(scene.is_valid(goal));
        EXPECT_FALSE(scene.get_session_value("demo.score"));
    }

    TEST_F(ScriptSystemTest, OneShotRequiresAnAuthoredAudioSource) {
        source("return {update = function() comet.play_one_shot() end}");
        actor();
        ASSERT_TRUE(runtime.start(scene));
        const auto failed = runtime.advance(0);
        ASSERT_FALSE(failed);
        EXPECT_NE(failed.error().message.find("valid Audio Source"), std::string::npos);
        EXPECT_FALSE(runtime.is_active());
    }

}

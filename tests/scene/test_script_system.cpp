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
#include "common/file_io.h"
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
                self.held = comet.action_down('jump')
                assert(self.held == comet.key_down('J'))
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

    TEST_F(ScriptSystemTest, DemoProjectAndLuaSourceUseTheSameToggleContract) {
        const auto project = Project::load(
            std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "demo");
        ASSERT_TRUE(project) << project.error();
        ASSERT_TRUE(runtime.set_input_actions(project.value().input_actions()));
        auto script = Script::load(project.value().paths().assets() / "scripts/spin.lua");
        ASSERT_TRUE(script) << script.error().message;
        ASSERT_TRUE(assets.register_asset(handle, std::move(script).value()));
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
        const auto material_source = read_text_file(material_path);
        ASSERT_TRUE(material_source) << material_source.error();
        const auto authored = MaterialSerializer{}.load(material_path);
        ASSERT_TRUE(authored) << authored.error();
        const auto material = std::make_shared<Material>("cube", authored.value().template_name);
        for(const auto& [name, value] : authored.value().scalar_properties)
            ASSERT_TRUE(material->set_scalar_property(name, value));
        for(const auto& [name, value] : authored.value().vector_properties)
            ASSERT_TRUE(material->set_vector_property(name, value));
        const AssetHandle material_handle{6482638524486200214ULL};
        ASSERT_TRUE(assets.register_asset(material_handle, material));
        const auto material_revision = material->get_revision();
        for(const auto& [name, script_handle] :
            {std::pair{"spin.lua", AssetHandle{7821648321594001021}},
                std::pair{"move_cube.lua", AssetHandle{14309634625000312001ULL}},
                std::pair{"collect_goal.lua", AssetHandle{14309634625000312002ULL}}}) {
            auto script = Script::load(project.value().paths().assets() / "scripts" / name);
            ASSERT_TRUE(script) << script.error().message;
            ASSERT_TRUE(assets.register_asset(script_handle, std::move(script).value()));
        }
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
        ASSERT_TRUE(center_uuid);
        const Math::Vec4 score_color(1, 0.15f, 0.5f, 1);
        auto edit_center = edit_scene.value()->find_entity(*center_uuid);
        ASSERT_TRUE(edit_center);
        edit_center.get_component<ScriptComponent>().parameters["score_color"] = score_color;
        auto playing = serializer.clone(*edit_scene.value());
        ASSERT_TRUE(playing) << playing.error();
        ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>()));
        ASSERT_TRUE(runtime.add_system(
            std::make_unique<AudioSystem>(assets, AudioPlayback::Mode::Offline)));
        ASSERT_TRUE(runtime.start(*playing.value()));

        Input input;
        input.focus_event(true);
        input.key_event(Input::Key::Right, true);
        for(int frame = 0; frame < 120; ++frame)
            ASSERT_TRUE(runtime.advance(0.01, &input.publish_frame()));

        const auto goal_uuid = EntityUuid::parse("672cd0cc-501f-419e-af5e-a883a0cd3d07");
        const auto player_uuid = EntityUuid::parse("672cd0cc-501f-419e-af5e-a883a0cd3d05");
        ASSERT_TRUE(goal_uuid);
        ASSERT_TRUE(player_uuid);
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
        EXPECT_TRUE(TestUtils::Vec3Equal(marker.get_component<TransformComponent>().translation,
            edit_goal.get_component<TransformComponent>().translation + Math::Vec3(0, 0.8f, 0)));
        EXPECT_EQ(marker.get_component<TransformComponent>().scale, Math::Vec3(0.15f));
        EXPECT_FALSE(marker.has_component<ScriptComponent>());
        EXPECT_FALSE(marker.has_component<ColliderComponent>());
        EXPECT_FALSE(marker.has_component<AudioSourceComponent>());
        EXPECT_FALSE(edit_scene.value()->find_entity(marker.get_uuid()));
        const auto extracted = SceneExtractor::extract(*playing.value());
        const auto marker_item =
            std::ranges::find(extracted.render_items, marker.get_id(), &RenderItem::entity_id);
        ASSERT_NE(marker_item, extracted.render_items.end());
        EXPECT_EQ(marker_item->mesh_handle, goal_mesh.mesh);
        EXPECT_EQ(marker_item->material_handle, goal_mesh.material);
        EXPECT_FLOAT_EQ(playing.value()
                            ->find_entity(*center_uuid)
                            .get_component<TransformComponent>()
                            .translation.y,
            0.4f);
        const auto center = playing.value()->find_entity(*center_uuid);
        const auto player = playing.value()->find_entity(*player_uuid);
        ASSERT_TRUE(center);
        ASSERT_TRUE(player);
        EXPECT_EQ(center.get_component<MeshRendererComponent>().material, material_handle);
        EXPECT_EQ(player.get_component<MeshRendererComponent>().material, material_handle);
        const auto tint = playing.value()->get_material_overrides(center);
        ASSERT_TRUE(tint);
        EXPECT_EQ(tint->vector_properties.at("base_color"), score_color);
        EXPECT_FALSE(playing.value()->get_material_overrides(player));
        EXPECT_FALSE(edit_scene.value()->get_material_overrides(
            edit_scene.value()->find_entity(*center_uuid)));
        EXPECT_EQ(material->get_revision(), material_revision);
        EXPECT_EQ(material->get_vector_property("base_color"),
            authored.value().vector_properties.at("base_color"));
        input.key_event(Input::Key::Right, false);
        input.key_event(Input::Key::R, true);
        ASSERT_TRUE(runtime.advance(0, &input.publish_frame()));
        EXPECT_TRUE(playing.value()->take_restart_request());
        EXPECT_FALSE(playing.value()->take_restart_request());
        EXPECT_FALSE(playing.value()->find_entity(*goal_uuid));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(playing.value()->get_session_value("demo.score"));
        EXPECT_FALSE(playing.value()->get_material_overrides(center));
        EXPECT_TRUE(edit_scene.value()->find_entity(*goal_uuid));
        auto restarted = serializer.clone(*edit_scene.value());
        ASSERT_TRUE(restarted) << restarted.error();
        EXPECT_FALSE(restarted.value()->find_entity(marker.get_uuid()));
        ASSERT_TRUE(runtime.start(
            *restarted.value(), SceneRuntime::State::Running, SceneRuntime::InputStart::Rebase));
        EXPECT_TRUE(restarted.value()->find_entity(*goal_uuid));
        EXPECT_FALSE(restarted.value()->get_session_value("demo.score"));
        EXPECT_FALSE(restarted.value()->get_material_overrides(
            restarted.value()->find_entity(*center_uuid)));
        EXPECT_EQ(restarted.value()
                      ->find_entity(*player_uuid)
                      .get_component<TransformComponent>()
                      .translation,
            edit_scene.value()
                ->find_entity(*player_uuid)
                .get_component<TransformComponent>()
                .translation);
        for(int frame = 0; frame < 3; ++frame) {
            ASSERT_TRUE(runtime.advance(0.01, &input.publish_frame()));
            EXPECT_FALSE(restarted.value()->take_restart_request());
        }
        input.key_event(Input::Key::R, false);
        ASSERT_TRUE(runtime.advance(0.01, &input.publish_frame()));
        input.key_event(Input::Key::R, true);
        ASSERT_TRUE(runtime.advance(0.01, &input.publish_frame()));
        EXPECT_TRUE(restarted.value()->take_restart_request());
        ASSERT_TRUE(runtime.stop());
        const auto unchanged_material_source = read_text_file(material_path);
        ASSERT_TRUE(unchanged_material_source) << unchanged_material_source.error();
        EXPECT_EQ(unchanged_material_source.value(), material_source.value());
    }

    TEST_F(ScriptSystemTest, DemoGoalIgnoresContactsWithUnassignedActors) {
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project);
        auto script = Script::load(project.value().paths().assets() / "scripts/collect_goal.lua");
        ASSERT_TRUE(script) << script.error().message;
        auto instance = script.value()->instantiate();
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

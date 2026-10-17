#include <gtest/gtest.h>

#include "scene/component_registry.h"
#include "scene/components.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "scene/scene_runtime.h"
#include "scene/script_component.h"
#include "support/math_assertions.h"
#include "render/scene/scene_extractor.h"
#include "render/scene/scene_resolver.h"
#include "asset/registry.h"
#include "support/temporary_directory.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Comet::Tests {
    TEST(SceneSerializerTest, VectorParametersPreserveDimensionsAndSparseOverrides) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        Scene scene;
        auto actor = scene.create_entity("Actor");
        const ParameterMap parameters{
            {"direction", Math::Vec3(1, 2, 3)}, {"score_color", Math::Vec4(2, 0.25f, 0.5f, 0.75f)}};
        actor.add_component<ScriptComponent>().parameters = parameters;
        TemporaryDirectory directory;
        const auto path = (directory.path() / "color.scene").string();
        ASSERT_TRUE(serializer.save(scene, path));
        auto loaded = serializer.load(path);
        ASSERT_TRUE(loaded) << loaded.error();
        auto clone = serializer.clone(*loaded.value());
        ASSERT_TRUE(clone) << clone.error();
        auto cloned_actor = clone.value()->find_entity(actor.get_uuid());
        EXPECT_EQ(cloned_actor.get_component<ScriptComponent>().parameters, parameters);
        cloned_actor.get_component<ScriptComponent>().parameters["score_color"] = Math::Vec4(1);
        EXPECT_EQ(actor.get_component<ScriptComponent>().parameters, parameters);
        auto text = serializer.serialize(scene);
        ASSERT_TRUE(text);
        EXPECT_EQ(text.value().find("semantic"), std::string::npos);
    }

    TEST(SceneSerializerTest, RejectsMalformedVectorParametersAndNonFiniteFourthComponent) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        for(const std::string value :
            {"[]", "[1,2]", "[1,2,3,4,5]", "[1,2,3,null]", "[1,2,3,\"4\"]", "[1,2,3,1e100]"}) {
            SCOPED_TRACE(value);
            const auto json = R"({"version":2,"entities":[{
                "uuid":"00000000-0000-4000-8000-000000000001",
                "components":{"name":"Actor","script":{"asset":42,
                "parameters":{"color":)"
                              + value + "}}}}]}";
            auto loaded = serializer.deserialize(json);
            EXPECT_FALSE(loaded);
            if(!loaded)
                EXPECT_NE(loaded.error().find("parameters"), std::string::npos);
        }
        Scene scene;
        auto& parameters = scene.create_entity().add_component<ScriptComponent>().parameters;
        parameters["color"] = Math::Vec4(1, 1, 1, std::numeric_limits<float>::infinity());
        const auto saved = serializer.serialize(scene);
        ASSERT_FALSE(saved);
        EXPECT_NE(saved.error().find("parameters"), std::string::npos);
        const auto cloned = serializer.clone(scene);
        ASSERT_FALSE(cloned);
        EXPECT_NE(cloned.error().find("parameters"), std::string::npos);

        const std::array<std::string, 3> invalid_parameters{R"({"":1})", R"({"same":1,"same":2})",
            std::string("{\"long\":\"") + std::string(4097, 'a') + "\"}"};
        for(const auto& object : invalid_parameters) {
            const auto json = R"({"version":2,"entities":[{
                "uuid":"00000000-0000-4000-8000-000000000001",
                "components":{"name":"Actor","script":{"asset":42,"parameters":)"
                              + object + "}}}]}";
            const auto loaded = serializer.deserialize(json, "invalid-parameters.scene");
            ASSERT_FALSE(loaded);
            EXPECT_NE(loaded.error().find("parameters"), std::string::npos);
            EXPECT_NE(loaded.error().find("invalid-parameters.scene"), std::string::npos);
        }
        parameters.clear();
        for(int index = 0; index < 128; ++index)
            parameters.emplace("value" + std::to_string(index), float(index));
        const auto full = serializer.serialize(scene);
        ASSERT_TRUE(full) << full.error();
        EXPECT_TRUE(serializer.deserialize(full.value()));
        auto oversized = full.value();
        constexpr std::string_view parameter_object = R"("parameters": {)";
        const auto position = oversized.find(parameter_object);
        ASSERT_NE(position, std::string::npos);
        oversized.insert(position + parameter_object.size(), R"("extra": 1,)");
        const auto rejected = serializer.deserialize(oversized, "too-many-parameters.scene");
        ASSERT_FALSE(rejected);
        EXPECT_NE(rejected.error().find("parameters"), std::string::npos);
        EXPECT_NE(rejected.error().find("too-many-parameters.scene"), std::string::npos);
        parameters.emplace("extra", 1.0f);
        EXPECT_FALSE(serializer.serialize(scene));
    }

    TEST(SceneSerializerTest, EntityParametersRoundTripAndCloneWithoutGuessingStringValues) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        Scene scene;
        const auto target = scene.create_entity("Target");
        auto actor = scene.create_entity("Actor");
        const ParameterMap parameters{{"target", target.get_uuid()}, {"none", EntityUuid{}},
            {"missing", EntityUuid::generate()}, {"text", target.get_uuid().to_string()}};
        actor.add_component<ScriptComponent>().parameters = parameters;
        TemporaryDirectory directory;
        const auto path = (directory.path() / "references.scene").string();
        ASSERT_TRUE(serializer.save(scene, path));
        auto loaded = serializer.load(path);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value()
                      ->find_entity(actor.get_uuid())
                      .get_component<ScriptComponent>()
                      .parameters,
            parameters);
        auto clone = serializer.clone(*loaded.value());
        ASSERT_TRUE(clone) << clone.error();
        clone.value()->destroy_entity(clone.value()->find_entity(target.get_uuid()));
        auto cloned_actor = clone.value()->find_entity(actor.get_uuid());
        EXPECT_EQ(cloned_actor.get_component<ScriptComponent>().parameters, parameters);
        EXPECT_TRUE(scene.find_entity(target.get_uuid()));
        auto serialized = serializer.serialize(*clone.value());
        ASSERT_TRUE(serialized) << serialized.error();
        EXPECT_TRUE(serializer.deserialize(serialized.value()));
        auto invalid = serialized.value();
        const auto field = std::string("\"entity\": \"") + target.get_uuid().to_string() + "\"";
        const auto position = invalid.find(field);
        ASSERT_NE(position, std::string::npos);
        invalid.replace(position, field.size(), "\"entity\": \"not-a-uuid\"");
        EXPECT_FALSE(serializer.deserialize(invalid));
    }

    TEST(SceneSerializerTest, AudioCategoryPersistsClonesAndDefaultsWhenOmitted) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        Scene scene;
        auto sound = scene.create_entity("Music");
        sound.add_component<AudioSourceComponent>().category = AudioCategory::Music;
        auto serialized = serializer.serialize(scene);
        ASSERT_TRUE(serialized) << serialized.error();
        auto restored = serializer.deserialize(serialized.value());
        ASSERT_TRUE(restored) << restored.error();
        EXPECT_EQ(restored.value()
                      ->find_entity(sound.get_uuid())
                      .get_component<AudioSourceComponent>()
                      .category,
            AudioCategory::Music);
        auto clone = serializer.clone(scene);
        ASSERT_TRUE(clone) << clone.error();
        EXPECT_EQ(clone.value()
                      ->find_entity(sound.get_uuid())
                      .get_component<AudioSourceComponent>()
                      .category,
            AudioCategory::Music);
        const auto field = std::string(R"("category": "music",)");
        auto omitted = serialized.value();
        const auto position = omitted.find(field);
        ASSERT_NE(position, std::string::npos);
        omitted.erase(position, field.size());
        auto defaults = serializer.deserialize(omitted);
        ASSERT_TRUE(defaults) << defaults.error();
        EXPECT_EQ(defaults.value()
                      ->find_entity(sound.get_uuid())
                      .get_component<AudioSourceComponent>()
                      .category,
            AudioCategory::Effects);
        auto invalid = serialized.value();
        invalid.replace(position, field.size(), R"("category": "unknown",)");
        EXPECT_FALSE(serializer.deserialize(invalid));
    }

    TEST(SceneSerializerTest, RejectsAudioVolumeOutsideDataBoundsOnLoadAndSave) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        Scene scene;
        auto& source = scene.create_entity("Sound").add_component<AudioSourceComponent>();

        auto serialized = serializer.serialize(scene);
        ASSERT_TRUE(serialized) << serialized.error();
        std::string invalid = serialized.value();
        const auto position = invalid.find(R"("volume": 0.5)");
        ASSERT_NE(position, std::string::npos);
        invalid.replace(position, std::string(R"("volume": 0.5)").size(), R"("volume": 1.5)");
        auto loaded = serializer.deserialize(invalid, "invalid-audio.scene");
        EXPECT_FALSE(loaded);
        if(!loaded) {
            EXPECT_NE(loaded.error().find("volume"), std::string::npos);
            EXPECT_NE(loaded.error().find("outside allowed bounds"), std::string::npos);
        }

        source.volume = 1.5f;
        EXPECT_FALSE(serializer.serialize(scene));
    }

    TEST(SceneSerializerTest, RigidBodyOptionsPersistCloneAndDefaultWhenOmitted) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto defaults = serializer.deserialize(R"({"version":2,"entities":[{
            "uuid":"00000000-0000-4000-8000-000000000001",
            "components":{"name":"Body","rigid_body":{"motion":"dynamic"}}}]})");
        ASSERT_TRUE(defaults) << defaults.error();
        auto body = defaults.value()->get_entities().front();
        EXPECT_FLOAT_EQ(body.get_component<RigidBodyComponent>().mass, 1.0f);
        EXPECT_FALSE(body.get_component<RigidBodyComponent>().continuous_collision);
        body.edit_component<RigidBodyComponent>([](auto& component) {
            component.mass = 3.5f;
            component.continuous_collision = true;
        });
        TemporaryDirectory directory;
        const auto path = (directory.path() / "mass.scene").string();
        ASSERT_TRUE(serializer.save(*defaults.value(), path));
        auto loaded = serializer.load(path);
        ASSERT_TRUE(loaded) << loaded.error();
        const auto loaded_body = loaded.value()->find_entity(body.get_uuid());
        EXPECT_FLOAT_EQ(loaded_body.get_component<RigidBodyComponent>().mass, 3.5f);
        EXPECT_TRUE(loaded_body.get_component<RigidBodyComponent>().continuous_collision);
        auto clone = serializer.clone(*loaded.value());
        ASSERT_TRUE(clone) << clone.error();
        const auto cloned_entity = clone.value()->find_entity(body.get_uuid());
        const auto& cloned_body = cloned_entity.get_component<RigidBodyComponent>();
        EXPECT_FLOAT_EQ(cloned_body.mass, 3.5f);
        EXPECT_TRUE(cloned_body.continuous_collision);
        cloned_entity.edit_component<RigidBodyComponent>([](auto& component) {
            component.mass = RigidBodyComponent::MIN_MASS;
            component.continuous_collision = false;
        });
        EXPECT_TRUE(serializer.serialize(*clone.value()));
        EXPECT_FLOAT_EQ(loaded_body.get_component<RigidBodyComponent>().mass, 3.5f);
        EXPECT_TRUE(loaded_body.get_component<RigidBodyComponent>().continuous_collision);
    }

    TEST(SceneSerializerTest, JointKindsPreserveReferencesAnchorsAndAxesAcrossSaveAndClone) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        Scene scene;
        auto target = scene.create_entity("Target");
        auto actor = scene.create_entity("Actor");
        const PointJointComponent joint{.connected_body = target.get_uuid(),
            .anchor = {0, 0.5f, 0},
            .connected_anchor = {0, -0.5f, 0},
            .break_force = 25};
        actor.add_component<PointJointComponent>(joint);
        const HingeJointComponent hinge{.enabled = false,
            .connected_body = target.get_uuid(),
            .anchor = {1, 2, 3},
            .connected_anchor = {4, 5, 6},
            .axis = {1, 1, 0},
            .connected_axis = {0, 1, 1},
            .limits_enabled = true,
            .reference_axis = {0, 0, 1},
            .connected_reference_axis = {1, 0, 0},
            .min_angle = -30,
            .max_angle = 60,
            .limits_frequency = 3,
            .limits_damping = 0.5f,
            .motor_enabled = true,
            .motor_mode = JointMotorMode::Position,
            .motor_target = -15,
            .motor_speed = -120,
            .motor_max_torque = 2.5f,
            .motor_frequency = 4,
            .motor_damping = 0.7f,
            .break_force = 30,
            .break_torque = 5};
        actor.add_component<HingeJointComponent>(hinge);
        const SliderJointComponent slider{.enabled = false,
            .connected_body = target.get_uuid(),
            .anchor = {1, 2, 3},
            .connected_anchor = {4, 5, 6},
            .axis = {0, 0, 2},
            .connected_axis = {0, 0, 3},
            .reference_axis = {0, 1, 1},
            .connected_reference_axis = {0, 2, 1},
            .limits_enabled = true,
            .min_distance = -0.25f,
            .max_distance = 1.25f,
            .limits_frequency = 5,
            .limits_damping = 0.8f,
            .motor_enabled = true,
            .motor_mode = JointMotorMode::Position,
            .motor_target = 0.5f,
            .motor_speed = -2,
            .motor_max_force = 30,
            .motor_frequency = 6,
            .motor_damping = 1.2f,
            .break_force = 40,
            .break_torque = 6};
        actor.add_component<SliderJointComponent>(slider);
        TemporaryDirectory directory;
        const auto path = (directory.path() / "joint.scene").string();
        ASSERT_TRUE(serializer.save(scene, path));
        auto loaded = serializer.load(path);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(
            loaded.value()->find_entity(actor.get_uuid()).get_component<PointJointComponent>(),
            joint);
        EXPECT_EQ(
            loaded.value()->find_entity(actor.get_uuid()).get_component<HingeJointComponent>(),
            hinge);
        EXPECT_EQ(
            loaded.value()->find_entity(actor.get_uuid()).get_component<SliderJointComponent>(),
            slider);
        auto clone = serializer.clone(*loaded.value());
        ASSERT_TRUE(clone) << clone.error();
        auto cloned_actor = clone.value()->find_entity(actor.get_uuid());
        EXPECT_EQ(cloned_actor.get_component<PointJointComponent>(), joint);
        EXPECT_EQ(cloned_actor.get_component<HingeJointComponent>(), hinge);
        EXPECT_EQ(cloned_actor.get_component<SliderJointComponent>(), slider);
        EXPECT_TRUE(clone.value()->find_entity(
            cloned_actor.get_component<PointJointComponent>().connected_body));
        cloned_actor.edit_component<PointJointComponent>([](auto& value) {
            value.connected_body = {};
            value.connected_anchor = {1, 2, 3};
        });
        cloned_actor.edit_component<HingeJointComponent>(
            [](auto& value) { value.connected_body = {}; });
        cloned_actor.edit_component<SliderJointComponent>(
            [](auto& value) { value.connected_body = {}; });
        auto text = serializer.serialize(*clone.value());
        ASSERT_TRUE(text);
        auto world_joint = serializer.deserialize(text.value());
        ASSERT_TRUE(world_joint) << world_joint.error();
        EXPECT_FALSE(world_joint.value()
                ->find_entity(actor.get_uuid())
                .get_component<PointJointComponent>()
                .connected_body);
        EXPECT_FALSE(world_joint.value()
                ->find_entity(actor.get_uuid())
                .get_component<HingeJointComponent>()
                .connected_body);
        EXPECT_EQ(actor.get_component<HingeJointComponent>(), hinge);
        EXPECT_EQ(actor.get_component<SliderJointComponent>(), slider);
        EXPECT_FALSE(world_joint.value()
                ->find_entity(actor.get_uuid())
                .get_component<SliderJointComponent>()
                .connected_body);
        EXPECT_EQ(actor.get_component<PointJointComponent>(), joint);
        for(const auto member :
            {&PointJointComponent::anchor, &PointJointComponent::connected_anchor}) {
            auto invalid = joint;
            (invalid.*member).x = std::numeric_limits<float>::infinity();
            actor.edit_component<PointJointComponent>([&](auto& value) { value = invalid; });
            EXPECT_FALSE(serializer.serialize(scene));
        }
        ASSERT_TRUE(actor.try_set_component(joint));
        for(const auto member : {&HingeJointComponent::anchor,
                &HingeJointComponent::connected_anchor, &HingeJointComponent::axis,
                &HingeJointComponent::connected_axis, &HingeJointComponent::reference_axis,
                &HingeJointComponent::connected_reference_axis}) {
            auto invalid = hinge;
            (invalid.*member).x = std::numeric_limits<float>::infinity();
            ASSERT_TRUE(actor.try_set_component(invalid));
            EXPECT_FALSE(serializer.serialize(scene));
        }
    }

    TEST(SceneSerializerTest, LegacyHingeContentDefaultsControlsOffAndRejectsOutOfRangeValues) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        const std::string legacy = R"({"version":2,"entities":[{
            "uuid":"00000000-0000-4000-8000-000000000001",
            "components":{"name":"Hinge","hinge_joint":{"enabled":true,
                "connected_body":{"entity":"00000000-0000-0000-0000-000000000000"},
                "anchor":[0,0,0],"connected_anchor":[0,0,0],"axis":[0,0,1],"connected_axis":[0,0,1]}}}]})";
        auto loaded = serializer.deserialize(legacy);
        ASSERT_TRUE(loaded) << loaded.error();
        auto actor = loaded.value()->get_entities().front();
        EXPECT_EQ(actor.get_component<HingeJointComponent>(), HingeJointComponent{});
        for(const auto& [key, value] :
            {std::pair{"min_angle", 1.0f}, std::pair{"max_angle", 181.0f},
                std::pair{"motor_max_torque", -1.0f}, std::pair{"motor_target", 181.0f},
                std::pair{"motor_frequency", 0.0f}, std::pair{"motor_damping", -1.0f},
                std::pair{"limits_frequency", -1.0f}, std::pair{"limits_damping", -1.0f}}) {
            auto modified = legacy;
            const auto position = modified.find("\"enabled\":true,");
            ASSERT_NE(position, std::string::npos);
            modified.insert(
                position, std::string("\"") + key + "\":" + std::to_string(value) + ",");
            EXPECT_FALSE(serializer.deserialize(modified));
        }
        for(const auto member : {&HingeJointComponent::min_angle, &HingeJointComponent::max_angle,
                &HingeJointComponent::motor_speed, &HingeJointComponent::motor_max_torque,
                &HingeJointComponent::motor_target, &HingeJointComponent::motor_frequency,
                &HingeJointComponent::motor_damping, &HingeJointComponent::limits_frequency,
                &HingeJointComponent::limits_damping}) {
            auto invalid = HingeJointComponent{};
            invalid.*member = std::numeric_limits<float>::infinity();
            ASSERT_TRUE(actor.try_set_component(invalid));
            EXPECT_FALSE(serializer.serialize(*loaded.value()));
        }
    }

    TEST(SceneSerializerTest, SliderDefaultsAndDataBoundsUseComponentDescriptors) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        const std::string json = R"({"version":2,"entities":[{
            "uuid":"00000000-0000-4000-8000-000000000001",
            "components":{"name":"Slider","slider_joint":{"enabled":true,
                "connected_body":{"entity":"00000000-0000-0000-0000-000000000000"},
                "anchor":[0,0,0],"connected_anchor":[0,0,0],"axis":[0,1,0],"connected_axis":[0,1,0]}}}]})";
        auto loaded = serializer.deserialize(json);
        ASSERT_TRUE(loaded) << loaded.error();
        auto actor = loaded.value()->get_entities().front();
        EXPECT_EQ(actor.get_component<SliderJointComponent>(), SliderJointComponent{});
        for(const auto& [key, value] : {std::pair{"min_distance", 1.0f},
                std::pair{"max_distance", -1.0f}, std::pair{"motor_max_force", -1.0f},
                std::pair{"motor_frequency", 0.0f}, std::pair{"motor_damping", -1.0f},
                std::pair{"limits_frequency", -1.0f}, std::pair{"limits_damping", -1.0f}}) {
            auto modified = json;
            const auto position = modified.find("\"enabled\":true,");
            ASSERT_NE(position, std::string::npos);
            modified.insert(
                position, std::string("\"") + key + "\":" + std::to_string(value) + ",");
            EXPECT_FALSE(serializer.deserialize(modified));
        }
        for(const auto member : {&SliderJointComponent::min_distance,
                &SliderJointComponent::max_distance, &SliderJointComponent::motor_speed,
                &SliderJointComponent::motor_max_force, &SliderJointComponent::motor_target,
                &SliderJointComponent::motor_frequency, &SliderJointComponent::motor_damping,
                &SliderJointComponent::limits_frequency, &SliderJointComponent::limits_damping}) {
            auto invalid = SliderJointComponent{};
            invalid.*member = std::numeric_limits<float>::infinity();
            ASSERT_TRUE(actor.try_set_component(invalid));
            EXPECT_FALSE(serializer.serialize(*loaded.value()));
        }
        for(const auto member : {&SliderJointComponent::anchor,
                &SliderJointComponent::connected_anchor, &SliderJointComponent::axis,
                &SliderJointComponent::connected_axis, &SliderJointComponent::reference_axis,
                &SliderJointComponent::connected_reference_axis}) {
            auto invalid = SliderJointComponent{};
            (invalid.*member).x = std::numeric_limits<float>::infinity();
            ASSERT_TRUE(actor.try_set_component(invalid));
            EXPECT_FALSE(serializer.serialize(*loaded.value()));
        }
    }

    TEST(SceneSerializerTest, JointMotorModesRejectUnknownIdsOnLoadAndSave) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        Scene scene;
        auto actor = scene.create_entity();
        for(const auto id : {"hinge_joint", "slider_joint"}) {
            SCOPED_TRACE(id);
            const auto* descriptor = registry.find_component(id);
            ASSERT_NE(descriptor, nullptr);
            ASSERT_TRUE(descriptor->add_component(actor));
            auto serialized = serializer.serialize(scene);
            ASSERT_TRUE(serialized) << serialized.error();
            const std::string field = R"("motor_mode": "velocity")";
            auto invalid = serialized.value();
            const auto position = invalid.find(field);
            ASSERT_NE(position, std::string::npos);
            invalid.replace(position, field.size(), R"("motor_mode": "unknown")");
            EXPECT_FALSE(serializer.deserialize(invalid));
            if(actor.has_component<HingeJointComponent>())
                actor.edit_component<HingeJointComponent>(
                    [](auto& joint) { joint.motor_mode = static_cast<JointMotorMode>(99); });
            else
                actor.edit_component<SliderJointComponent>(
                    [](auto& joint) { joint.motor_mode = static_cast<JointMotorMode>(99); });
            EXPECT_FALSE(serializer.serialize(scene));
            ASSERT_TRUE(descriptor->remove_component(actor));
        }
    }

    TEST(SceneSerializerTest, JointBreakThresholdsDefaultOffAndRejectInvalidNumbersOnLoadAndSave) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        Scene scene;
        auto actor = scene.create_entity();
        for(const auto id : {"point_joint", "hinge_joint", "slider_joint"}) {
            SCOPED_TRACE(id);
            const auto* descriptor = registry.find_component(id);
            ASSERT_NE(descriptor, nullptr);
            ASSERT_TRUE(descriptor->add_component(actor));
            for(const auto field : {"break_force", "break_torque"}) {
                const auto* property = descriptor->find_property(field);
                if(!property)
                    continue;
                auto serialized = serializer.serialize(scene);
                ASSERT_TRUE(serialized) << serialized.error();
                const std::string key = std::string("\"") + field + "\": 0";
                const auto position = serialized.value().find(key);
                ASSERT_NE(position, std::string::npos);
                auto omitted = serialized.value();
                auto begin = position;
                auto end = position + key.size();
                if(omitted[end] == ',')
                    ++end;
                else
                    begin = omitted.rfind(',', position);
                ASSERT_NE(begin, std::string::npos);
                omitted.erase(begin, end - begin);
                auto defaults = serializer.deserialize(omitted);
                ASSERT_TRUE(defaults) << defaults.error();
                EXPECT_EQ(property->copy_value(descriptor->get_component(
                              defaults.value()->find_entity(actor.get_uuid()))),
                    PropertyValue(0.0f));
                for(const auto number : {"-1", "1e100"}) {
                    auto invalid = serialized.value();
                    invalid.replace(
                        position, key.size(), std::string("\"") + field + "\": " + number);
                    EXPECT_FALSE(serializer.deserialize(invalid));
                }
                // 内容 API 不依赖编辑器的数值边界；保存时仍须拒绝无效配置。
                if(actor.has_component<PointJointComponent>())
                    actor.edit_component<PointJointComponent>(
                        [](auto& joint) { joint.break_force = -1; });
                else if(actor.has_component<HingeJointComponent>())
                    actor.edit_component<HingeJointComponent>([&](auto& joint) {
                        (std::string_view(field) == "break_force" ? joint.break_force
                                                                  : joint.break_torque) = -1;
                    });
                else
                    actor.edit_component<SliderJointComponent>([&](auto& joint) {
                        (std::string_view(field) == "break_force" ? joint.break_force
                                                                  : joint.break_torque) = -1;
                    });
                EXPECT_FALSE(serializer.serialize(scene));
                ASSERT_TRUE(descriptor->assign_property(actor, field, 0.0f));
            }
            ASSERT_TRUE(descriptor->remove_component(actor));
        }
    }

    TEST(SceneSerializerTest, CollisionMasksRoundTripExactIntegersForBodiesAndCharacters) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        Scene scene;
        auto body = scene.create_entity("Body");
        body.add_component<ColliderComponent>(
            ColliderComponent{.collision_layer = 0x80000001u, .collision_mask = 0x80000000u});
        auto character = scene.create_entity("Character");
        character.add_component<CharacterControllerComponent>(CharacterControllerComponent{
            .collision_layer = 0x80000000u, .collision_mask = UINT32_MAX});
        auto encoded = serializer.serialize(scene);
        ASSERT_TRUE(encoded);
        auto loaded = serializer.deserialize(encoded.value());
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value()->find_entity(body.get_uuid()).get_component<ColliderComponent>(),
            body.get_component<ColliderComponent>());
        EXPECT_EQ(loaded.value()
                      ->find_entity(character.get_uuid())
                      .get_component<CharacterControllerComponent>(),
            character.get_component<CharacterControllerComponent>());
        for(const auto invalid : {"-1", "1.5", "4294967296", "1e100"}) {
            auto text = encoded.value();
            const auto at = text.find("2147483649");
            ASSERT_NE(at, std::string::npos);
            text.replace(at, 10, invalid);
            EXPECT_FALSE(serializer.deserialize(text)) << invalid;
        }
    }

    TEST(SceneSerializerTest, MeshCollidersPersistCloneAndCollectTheirAssetDependencies) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        Scene scene;
        auto convex = scene.create_entity("Convex");
        auto triangles = scene.create_entity("Triangles");
        convex.add_component<ColliderComponent>(
            ColliderComponent{.shape = ColliderShape::ConvexHull, .mesh = AssetHandle{701}});
        triangles.add_component<ColliderComponent>(
            ColliderComponent{.shape = ColliderShape::TriangleMesh, .mesh = AssetHandle{702}});
        const auto encoded = serializer.serialize(scene);
        ASSERT_TRUE(encoded);
        const auto loaded = serializer.deserialize(encoded.value());
        ASSERT_TRUE(loaded) << loaded.error();
        const auto clone = serializer.clone(*loaded.value());
        ASSERT_TRUE(clone) << clone.error();
        for(const auto entity : {convex, triangles})
            EXPECT_EQ(
                clone.value()->find_entity(entity.get_uuid()).get_component<ColliderComponent>(),
                entity.get_component<ColliderComponent>());
        const auto references = registry.collect_asset_references(*clone.value());
        EXPECT_TRUE(std::ranges::any_of(references, [](const auto& ref) {
            return ref.handle == AssetHandle{701} && ref.type == AssetType::Mesh && ref.cpu_only;
        }));
        EXPECT_TRUE(std::ranges::any_of(references, [](const auto& ref) {
            return ref.handle == AssetHandle{702} && ref.type == AssetType::Mesh && ref.cpu_only;
        }));
        convex.edit_component<ColliderComponent>(
            [](auto& collider) { collider.shape = ColliderShape::Box; });
        const auto runtime_refs =
            registry.collect_asset_references(scene, ComponentRegistry::ReferenceScope::Runtime);
        EXPECT_FALSE(std::ranges::any_of(
            runtime_refs, [](const auto& ref) { return ref.handle == AssetHandle{701}; }));
        EXPECT_EQ(registry.collect_asset_references(scene).size(), 2u);
    }

    TEST(SceneSerializerTest, ColliderSurfaceParametersPersistCloneAndDefaultWhenOmitted) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto defaults = serializer.deserialize(R"({"version":2,"entities":[{
            "uuid":"00000000-0000-4000-8000-000000000001",
            "components":{"name":"Body","collider":{"shape":"sphere",
            "half_extents":[0.5,0.5,0.5],"radius":0.5}}}]})");
        ASSERT_TRUE(defaults) << defaults.error();
        auto body = defaults.value()->get_entities().front();
        EXPECT_FLOAT_EQ(body.get_component<ColliderComponent>().friction, 0.2f);
        EXPECT_FLOAT_EQ(body.get_component<ColliderComponent>().restitution, 0);
        EXPECT_FLOAT_EQ(body.get_component<ColliderComponent>().height, 2);
        body.edit_component<ColliderComponent>([](auto& collider) {
            collider.shape = ColliderShape::Capsule;
            collider.height = 3.5f;
            collider.friction = 0.6f;
            collider.restitution = 0.8f;
        });
        TemporaryDirectory directory;
        const auto path = (directory.path() / "collider.scene").string();
        ASSERT_TRUE(serializer.save(*defaults.value(), path));
        auto loaded = serializer.load(path);
        ASSERT_TRUE(loaded) << loaded.error();
        const auto loaded_body = loaded.value()->find_entity(body.get_uuid());
        EXPECT_EQ(loaded_body.get_component<ColliderComponent>().shape, ColliderShape::Capsule);
        EXPECT_FLOAT_EQ(loaded_body.get_component<ColliderComponent>().height, 3.5f);
        EXPECT_FLOAT_EQ(loaded_body.get_component<ColliderComponent>().friction, 0.6f);
        EXPECT_FLOAT_EQ(loaded_body.get_component<ColliderComponent>().restitution, 0.8f);
        auto clone = serializer.clone(*loaded.value());
        ASSERT_TRUE(clone) << clone.error();
        const auto cloned_body = clone.value()->find_entity(body.get_uuid());
        EXPECT_EQ(cloned_body.get_component<ColliderComponent>(),
            loaded_body.get_component<ColliderComponent>());
        cloned_body.edit_component<ColliderComponent>([](auto& collider) {
            collider.friction = 0;
            collider.restitution = 1;
        });
        EXPECT_TRUE(serializer.serialize(*clone.value()));
        EXPECT_FLOAT_EQ(loaded_body.get_component<ColliderComponent>().friction, 0.6f);
        EXPECT_FLOAT_EQ(loaded_body.get_component<ColliderComponent>().restitution, 0.8f);
    }

    TEST(SceneSerializerTest, RejectsInvalidColliderSurfaceParametersOnLoadAndSave) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        for(const auto* field : {"friction", "restitution"}) {
            for(const auto* value : {"-0.1", "1.1", "null", "1e100", "\"0.2\""}) {
                const auto json = std::string(R"({"version":2,"entities":[{
                    "uuid":"00000000-0000-4000-8000-000000000001",
                    "components":{"name":"Body","collider":{"shape":"box",
                    "half_extents":[0.5,0.5,0.5],"radius":0.5,")")
                                  + field + "\":" + value + "}}}]}";
                const auto loaded = serializer.deserialize(json);
                ASSERT_FALSE(loaded);
                EXPECT_NE(loaded.error().find(field), std::string::npos);
            }
        }
        Scene scene;
        auto body = scene.create_entity();
        body.add_component<ColliderComponent>();
        for(const auto property : {&ColliderComponent::friction, &ColliderComponent::restitution}) {
            for(const float value : {-0.1f, 1.1f, std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::quiet_NaN()}) {
                body.edit_component<ColliderComponent>(
                    [&](auto& collider) { collider.*property = value; });
                EXPECT_FALSE(serializer.serialize(scene));
            }
            body.edit_component<ColliderComponent>(
                [&](auto& collider) { collider.*property = 0.2f; });
        }
    }

    TEST(SceneSerializerTest, DensityModePersistsCloneAndDefaultsToExplicitMassWhenOmitted) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto legacy = serializer.deserialize(R"({"version":2,"entities":[{
            "uuid":"00000000-0000-4000-8000-000000000001",
            "components":{"name":"Body","rigid_body":{"motion":"dynamic","mass":3}}}]})");
        ASSERT_TRUE(legacy) << legacy.error();
        auto body = legacy.value()->get_entities().front();
        EXPECT_EQ(body.get_component<RigidBodyComponent>().mass_mode, BodyMassMode::Mass);
        EXPECT_FLOAT_EQ(body.get_component<RigidBodyComponent>().mass, 3);
        EXPECT_FLOAT_EQ(body.get_component<RigidBodyComponent>().density, 1000);
        body.edit_component<RigidBodyComponent>([](auto& rigid) {
            rigid.mass_mode = BodyMassMode::Density;
            rigid.density = 600;
        });
        TemporaryDirectory directory;
        const auto path = (directory.path() / "density.scene").string();
        ASSERT_TRUE(serializer.save(*legacy.value(), path));
        const auto loaded = serializer.load(path);
        ASSERT_TRUE(loaded) << loaded.error();
        const auto loaded_body = loaded.value()->find_entity(body.get_uuid());
        EXPECT_EQ(loaded_body.get_component<RigidBodyComponent>(),
            body.get_component<RigidBodyComponent>());
        auto clone = serializer.clone(*loaded.value());
        ASSERT_TRUE(clone) << clone.error();
        auto cloned_body = clone.value()->find_entity(body.get_uuid());
        EXPECT_EQ(cloned_body.get_component<RigidBodyComponent>(),
            loaded_body.get_component<RigidBodyComponent>());
        cloned_body.edit_component<RigidBodyComponent>([](auto& rigid) {
            rigid.mass_mode = BodyMassMode::Mass;
            rigid.mass = 5;
        });
        EXPECT_TRUE(serializer.serialize(*clone.value()));
        EXPECT_EQ(loaded_body.get_component<RigidBodyComponent>().mass_mode, BodyMassMode::Density);
        EXPECT_FLOAT_EQ(loaded_body.get_component<RigidBodyComponent>().mass, 3);
    }

    TEST(SceneSerializerTest, RejectsInvalidDensityAndMassModeOnLoadAndSave) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        for(const auto& [field, value] :
            {std::pair{"density", "0"}, std::pair{"density", "-1"}, std::pair{"density", "0.0005"},
                std::pair{"density", "null"}, std::pair{"density", "1e100"},
                std::pair{"density", "\"1000\""}, std::pair{"mass_mode", "null"},
                std::pair{"mass_mode", "1"}, std::pair{"mass_mode", "\"unknown\""}}) {
            const auto json = std::string(R"({"version":2,"entities":[{
                "uuid":"00000000-0000-4000-8000-000000000001",
                "components":{"name":"Body","rigid_body":{"motion":"dynamic",")")
                              + field + "\":" + value + "}}}]}";
            const auto loaded = serializer.deserialize(json);
            ASSERT_FALSE(loaded);
            EXPECT_NE(loaded.error().find(field), std::string::npos);
        }
        Scene scene;
        auto body = scene.create_entity();
        body.add_component<RigidBodyComponent>();
        for(const float density : {0.0f, -1.0f, RigidBodyComponent::MIN_DENSITY / 2,
                std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            body.edit_component<RigidBodyComponent>([&](auto& rigid) { rigid.density = density; });
            EXPECT_FALSE(serializer.serialize(scene));
        }
        body.edit_component<RigidBodyComponent>([](auto& rigid) {
            rigid.density = 1000;
            rigid.mass_mode = static_cast<BodyMassMode>(42);
        });
        EXPECT_FALSE(serializer.serialize(scene));
    }

    TEST(SceneSerializerTest, RejectsInvalidRigidBodyMassOnLoadAndSave) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        for(const std::string value : {"0", "-1", "0.0005", "null", "1e100", "\"2\""}) {
            SCOPED_TRACE(value);
            const auto json = R"({"version":2,"entities":[{
                "uuid":"00000000-0000-4000-8000-000000000001",
                "components":{"name":"Body","rigid_body":{"motion":"dynamic","mass":)"
                              + value + "}}}]}";
            const auto loaded = serializer.deserialize(json);
            ASSERT_FALSE(loaded);
            EXPECT_NE(loaded.error().find("mass"), std::string::npos);
        }
        Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<RigidBodyComponent>();
        for(const auto invalid : {0.0f, -1.0f, RigidBodyComponent::MIN_MASS * 0.5f,
                std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            SCOPED_TRACE(invalid);
            entity.edit_component<RigidBodyComponent>(
                [invalid](auto& component) { component.mass = invalid; });
            const auto serialized = serializer.serialize(scene);
            ASSERT_FALSE(serialized);
            EXPECT_NE(serialized.error().find("mass"), std::string::npos);
        }
    }

    TEST(ScenePostProcessTest, PersistsClonesAndTravelsThroughBothCameraPaths) {
        Scene scene;
        const PostProcessSettings settings{.exposure = 0.75f,
            .bloom_enabled = true,
            .bloom_strength = 0.5f,
            .bloom_threshold = 2.0f,
            .ao_enabled = true,
            .ao_radius = 1.2f,
            .ao_strength = 1.5f,
            .reflections = {.enabled = true,
                .strength = .7f,
                .max_distance = 18,
                .thickness = .2f,
                .max_roughness = .8f}};
        ASSERT_TRUE(scene.set_post_process(settings));
        scene.create_entity("Camera").add_component<CameraComponent>(
            CameraComponent{.primary = true});
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        TemporaryDirectory directory;
        const auto path = (directory.path() / "bloom.scene").string();
        ASSERT_TRUE(serializer.save(scene, path));
        const auto loaded = serializer.load(path);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value()->get_post_process(), settings);
        const auto cloned = serializer.clone(*loaded.value());
        ASSERT_TRUE(cloned) << cloned.error();
        const auto snapshot = SceneExtractor::extract(*cloned.value());
        EXPECT_EQ(snapshot.post_process, settings);
        const AssetRegistry assets;
        SceneResolver resolver(assets);
        EXPECT_EQ(resolver.resolve(snapshot).post_process, settings);

        auto disabled = settings;
        disabled.bloom_enabled = false;
        ASSERT_TRUE(cloned.value()->set_post_process(disabled));
        EXPECT_FALSE(cloned.value()->get_post_process().uses_bloom());
        EXPECT_EQ(scene.get_post_process(), settings);
        EXPECT_EQ(
            resolver.resolve(SceneExtractor::extract(*cloned.value())).post_process, disabled);
        EXPECT_EQ(resolver.resolve(RenderScene{}).post_process, PostProcessSettings{});
    }

    TEST(ScenePostProcessTest, DefaultsOffAndRejectsMalformedOrOutOfRangeSettings) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        const auto loaded = serializer.deserialize(R"({"version":2,"entities":[]})");
        ASSERT_TRUE(loaded);
        auto& scene = *loaded.value();
        EXPECT_EQ(scene.get_post_process(), PostProcessSettings{});
        EXPECT_FALSE(scene.get_post_process().uses_bloom());
        EXPECT_FALSE(scene.get_post_process().uses_ao());
        EXPECT_FALSE(scene.get_post_process().uses_reflections());
        for(const auto field :
            {&ScreenSpaceReflectionSettings::strength, &ScreenSpaceReflectionSettings::max_distance,
                &ScreenSpaceReflectionSettings::thickness,
                &ScreenSpaceReflectionSettings::max_roughness}) {
            auto value = scene.get_post_process();
            value.reflections.*field = std::numeric_limits<float>::infinity();
            EXPECT_FALSE(scene.set_post_process(value));
        }
        for(auto field : {&PostProcessSettings::exposure, &PostProcessSettings::bloom_strength,
                &PostProcessSettings::bloom_threshold, &PostProcessSettings::ao_radius,
                &PostProcessSettings::ao_strength}) {
            for(float invalid : {-1.0f, 100000.0f, std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity()}) {
                auto value = scene.get_post_process();
                value.*field = invalid;
                EXPECT_FALSE(scene.set_post_process(value));
                EXPECT_EQ(scene.get_post_process(), PostProcessSettings{});
            }
        }
        for(const char* object : {"null", "[]", "{}",
                R"({"exposure":-1,"bloom_enabled":true,"bloom_strength":1,"bloom_threshold":1})",
                R"({"exposure":101,"bloom_enabled":true,"bloom_strength":1,"bloom_threshold":1})",
                R"({"exposure":1,"bloom_enabled":true,"bloom_strength":11,"bloom_threshold":1})",
                R"({"exposure":1,"bloom_enabled":true,"bloom_strength":1,"bloom_threshold":65505})",
                R"({"exposure":1,"bloom_enabled":1,"bloom_strength":1,"bloom_threshold":1})",
                R"({"exposure":"bad","bloom_enabled":true,"bloom_strength":1,"bloom_threshold":1})",
                R"({"exposure":1,"bloom_enabled":true,"bloom_strength":1,"bloom_threshold":1,"unknown":0})"}) {
            const auto result = serializer.deserialize(
                std::string(R"({"version":2,"entities":[],"post_process":)") + object + "}",
                "invalid-post-process.scene");
            ASSERT_FALSE(result) << object;
            EXPECT_NE(result.error().find("post_process"), std::string::npos);
            EXPECT_NE(result.error().find("invalid-post-process.scene"), std::string::npos);
        }
        for(const std::string value :
            {"null", "[]", R"({"enabled":1})", R"({"strength":2})", R"({"max_distance":0})",
                R"({"thickness":-1})", R"({"max_roughness":0})", R"({"unknown":1})"}) {
            const auto result = serializer.deserialize(
                R"({"version":2,"entities":[],"post_process":{"exposure":1,"bloom_enabled":false,"bloom_strength":0,"bloom_threshold":1,"screen_space_reflections":)"
                    + value + "}}",
                "invalid-reflections.scene");
            EXPECT_FALSE(result) << value;
        }
    }

    TEST(SceneEnvironmentTest, PersistsExtractsAndCollectsReferenceWithoutAnEntity) {
        Scene scene;
        const SceneEnvironment environment{AssetHandle(902), true, 2.0f, -90.0f, true, 0.75f,
            {0.25f, 0.5f, 2.0f}, true, {0.1f, 0.2f, 0.3f}, 5, 60};
        ASSERT_TRUE(scene.set_environment(environment));
        EXPECT_EQ(scene.get_environment().rotation, -90.0f);
        const auto registry = create_scene_component_registry();
        const auto references = registry.collect_asset_references(scene);
        ASSERT_EQ(references.size(), 1u);
        EXPECT_EQ(references.front().handle, environment.asset);
        EXPECT_EQ(references.front().type, AssetType::Environment);
        EXPECT_EQ(SceneExtractor::extract(scene).environment, scene.get_environment());
        AssetRegistry assets;
        SceneResolver resolver(assets);
        EXPECT_EQ(
            resolver.resolve(SceneExtractor::extract(scene)).environment, scene.get_environment());
        const SceneSerializer serializer(registry);
        auto cloned = serializer.clone(scene);
        ASSERT_TRUE(cloned) << cloned.error();
        EXPECT_EQ(cloned.value()->get_environment(), scene.get_environment());
        EXPECT_EQ(cloned.value()->entity_count(), 0u);
        auto serialized = serializer.serialize(scene);
        ASSERT_TRUE(serialized) << serialized.error();
        auto restored = serializer.deserialize(serialized.value());
        ASSERT_TRUE(restored) << restored.error();
        EXPECT_EQ(restored.value()->get_environment(), scene.get_environment());
        auto hidden = scene.get_environment();
        hidden.background = false;
        ASSERT_TRUE(scene.set_environment(hidden));
        EXPECT_EQ(registry.collect_asset_references(scene).size(), 1u);
    }

    TEST(SceneEnvironmentTest, VolumetricFogPersistsClonesAndRejectsInvalidValues) {
        const auto components = create_scene_component_registry();
        const SceneSerializer serializer(components);
        Scene scene;
        auto environment = scene.get_environment();
        environment.volumetric_fog = {.enabled = true,
            .color = {.2f, .4f, .7f},
            .density = .1f,
            .base_height = -2,
            .height_falloff = .3f,
            .anisotropy = -.2f,
            .max_distance = 40,
            .ambient = .5f};
        ASSERT_TRUE(scene.set_environment(environment));
        auto text = serializer.serialize(scene);
        ASSERT_TRUE(text) << text.error();
        auto loaded = serializer.deserialize(text.value());
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value()->get_environment(), environment);
        auto clone = serializer.clone(scene);
        ASSERT_TRUE(clone);
        EXPECT_EQ(clone.value()->get_environment(), environment);
        EXPECT_EQ(SceneExtractor::extract(scene).environment, environment);
        for(const auto& field : {R"("density":-1)", R"("height_falloff":-1)", R"("anisotropy":1)",
                R"("max_distance":0)", R"("ambient":-1)", R"("color":[2,1,1])", R"("enabled":1)",
                R"("unknown":0)"}) {
            auto invalid = serializer.deserialize(
                std::string(R"({"version":2,"entities":[],"environment":{"volumetric_fog":{)")
                + field + "}}}");
            EXPECT_FALSE(invalid) << field;
        }
        environment.volumetric_fog.density = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(scene.set_environment(environment));
        EXPECT_TRUE(scene.get_environment().volumetric_fog.active());
    }

    TEST(SceneEnvironmentTest, FogRejectsInvalidRangesColorsAndTypesWithoutChangingScene) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        Scene scene;
        const auto original = scene.get_environment();
        for(const auto& field : {R"("fog_enabled":1)", R"("fog_color":[1,-1,1])",
                R"("fog_color":[1,1,1e100])", R"("fog_color":null)", R"("fog_color":[1,1])",
                R"("fog_start":-1)", R"("fog_start":100)", R"("fog_end":10)", R"("fog_end":100001)",
                R"("fog_end":null)", R"("fog_start":0,"fog_end":1e-40)"}) {
            SCOPED_TRACE(field);
            const auto json = std::string(R"({"version":2,"entities":[],"environment":{
                "asset":0,"background":false,"intensity":1,"rotation":0,)")
                              + field + "}}";
            auto loaded = serializer.deserialize(json);
            EXPECT_FALSE(loaded);
            if(!loaded)
                EXPECT_NE(loaded.error().find("fog"), std::string::npos);
        }
        for(float invalid : {-1.0f, std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::quiet_NaN()}) {
            auto value = original;
            value.fog_start = invalid;
            EXPECT_FALSE(scene.set_environment(value));
            value = original;
            value.fog_color.y = invalid;
            EXPECT_FALSE(scene.set_environment(value));
            EXPECT_EQ(scene.get_environment(), original);
        }
    }

    TEST(SceneEnvironmentTest, LegacyScenesDefaultOffAndInvalidEnvironmentIsRejected) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto legacy = serializer.deserialize(R"({"version":2,"entities":[]})");
        ASSERT_TRUE(legacy);
        EXPECT_EQ(legacy.value()->get_environment(), SceneEnvironment{});
        EXPECT_EQ(legacy.value()->get_environment().realtime_gi_budget, 32u);
        for(const auto* invalid : {"0", "16385", "-1", "1.5", "true", "null"})
            EXPECT_FALSE(serializer.deserialize(
                std::string(
                    R"({"version":2,"entities":[],"environment":{"asset":0,"background":false,"intensity":1,"rotation":0,"realtime_gi_budget":)")
                + invalid + "}}"))
                << invalid;

        EXPECT_EQ(legacy.value()->get_environment().background_color, Math::Vec3(0));
        auto background_only = serializer.deserialize(
            R"({"version":2,"environment":{"asset":0,"background":true,"intensity":1,"rotation":0},"entities":[]})");
        ASSERT_TRUE(background_only);
        EXPECT_FALSE(background_only.value()->get_environment().lighting);
        EXPECT_FLOAT_EQ(background_only.value()->get_environment().lighting_intensity, 1);
        for(const auto* color :
            {"null", "true", "[1,2]", "[1,2,3,4]", "[-1,0,0]", "[0,65505,0]", "[0,\"bad\",0]"}) {
            EXPECT_FALSE(serializer.deserialize(
                std::string(
                    R"({"version":2,"environment":{"asset":0,"background":false,"intensity":1,"rotation":0,"background_color":)")
                + color + R"(},"entities":[]})"))
                << color;
        }
        for(const float channel : {-1.0f, 65505.0f, std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::quiet_NaN()}) {
            auto invalid_color = SceneEnvironment{};
            invalid_color.background_color.y = channel;
            EXPECT_FALSE(legacy.value()->set_environment(invalid_color));
            EXPECT_EQ(legacy.value()->get_environment(), SceneEnvironment{});
        }
        for(const auto* invalid : {"-1", "65", "null", "true", "\"bad\""}) {
            EXPECT_FALSE(serializer.deserialize(
                std::string(
                    R"({"version":2,"environment":{"asset":0,"background":true,"intensity":1,"rotation":0,"lighting_intensity":)")
                + invalid + R"(},"entities":[]})"));
        }
        EXPECT_FALSE(serializer.deserialize(R"({"version":2,"environment":null,"entities":[]})"));
        EXPECT_FALSE(serializer.deserialize(
            R"({"version":2,"environment":{"asset":0,"background":true,"intensity":-1,"rotation":0},"entities":[]})"));
        EXPECT_FALSE(serializer.deserialize(
            R"({"version":2,"environment":{"asset":0,"background":true,"intensity":1,"rotation":"bad"},"entities":[]})"));
        EXPECT_FALSE(serializer.deserialize(
            R"({"version":2,"environment":{"asset":0,"background":true,"intensity":1,"rotation":0,"unknown":3},"entities":[]})"));
        auto invalid = SceneEnvironment{};
        invalid.intensity = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(legacy.value()->set_environment(invalid));
        invalid.intensity = 65;
        EXPECT_FALSE(legacy.value()->set_environment(invalid));
        invalid.intensity = 1;
        invalid.rotation = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(legacy.value()->set_environment(invalid));
        EXPECT_EQ(legacy.value()->get_environment(), SceneEnvironment{});
    }

    namespace {
        struct DescriptorTestComponent {
            float persisted = 0.0f;
            float runtime_only = 17.0f;
            std::string text;
        };

        struct RuntimeOnlyTestComponent {
            bool enabled = true;
        };

        EntityUuid uuid(const std::string_view value) {
            return EntityUuid::parse(value).value();
        }

        const ComponentRegistry& component_registry() {
            static const ComponentRegistry registry = create_scene_component_registry();
            return registry;
        }

        SceneSerializer make_scene_serializer() {
            return SceneSerializer(component_registry());
        }

        void expect_scene_error(const SceneSerializer& serializer, const std::string_view contents,
            const std::string_view expected_detail) {
            const auto result = serializer.deserialize(contents, "invalid.scene");
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().find("invalid.scene"), std::string::npos);
            EXPECT_NE(result.error().find(expected_detail), std::string::npos) << result.error();
        }

        void expect_scene_error(
            const std::string_view contents, const std::string_view expected_detail) {
            const SceneSerializer serializer = make_scene_serializer();
            expect_scene_error(serializer, contents, expected_detail);
        }
    }

    TEST(SceneSerializerTest, RoundTripsComponentsAndHierarchy) {
        const EntityUuid root_uuid = uuid("00000000-0000-4000-8000-000000000010");
        const EntityUuid child_uuid = uuid("00000000-0000-4000-8000-000000000020");
        Scene scene;
        Entity root = scene.create_entity_with_uuid(root_uuid, "Root");
        Entity child = scene.create_entity_with_uuid(child_uuid, "Child");
        ASSERT_TRUE(root);
        ASSERT_TRUE(child);

        const auto& root_transform = root.get_component<TransformComponent>();
        EXPECT_TRUE(root.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(3.0f, 4.0f, 5.0f); }));
        EXPECT_TRUE(root.try_edit_transform(
            [&](auto& value) { value.rotation = Math::Vec3(10.0f, 20.0f, 30.0f); }));
        EXPECT_TRUE(root.try_edit_transform([&](auto& value) { value.scale = Math::Vec3(2.0f); }));
        const auto& child_transform = child.get_component<TransformComponent>();
        EXPECT_TRUE(child.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(1.0f, 2.0f, 3.0f); }));
        EXPECT_TRUE(child.try_edit_transform(
            [&](auto& value) { value.rotation = Math::Vec3(-15.0f, 45.0f, 5.0f); }));
        EXPECT_TRUE(child.try_edit_transform(
            [&](auto& value) { value.scale = Math::Vec3(0.5f, 1.5f, 2.0f); }));
        child.add_component<MeshRendererComponent>(AssetHandle(101), AssetHandle(202));
        root.add_component<CameraComponent>(CameraComponent{.primary = true,
            .projection = CameraComponent::Projection::Orthographic,
            .fov = 60.0f,
            .orthographic_height = 14.0f,
            .near_clip = 0.25f,
            .far_clip = 2500.0f});
        ASSERT_TRUE(scene.set_parent(child, root));

        const SceneSerializer serializer = make_scene_serializer();
        auto contents_result = serializer.serialize(scene);
        ASSERT_TRUE(contents_result) << contents_result.error();
        auto contents = std::move(contents_result).value();

        EXPECT_NE(contents.find(R"("version": 2)"), std::string::npos);
        EXPECT_NE(contents.find(R"("children":)"), std::string::npos);
        EXPECT_EQ(contents.find(R"("parent":)"), std::string::npos);
        EXPECT_EQ(contents.find("entity_id"), std::string::npos);
        EXPECT_EQ(contents.find("world_matrix"), std::string::npos);

        auto loaded_result = serializer.deserialize(contents, "round-trip.scene");
        ASSERT_TRUE(loaded_result) << loaded_result.error();
        auto loaded = std::move(loaded_result).value();
        ASSERT_NE(loaded, nullptr);
        ASSERT_EQ(loaded->entity_count(), 2u);

        Entity loaded_root = loaded->find_entity(root_uuid);
        Entity loaded_child = loaded->find_entity(child_uuid);
        ASSERT_TRUE(loaded_root);
        ASSERT_TRUE(loaded_child);
        EXPECT_EQ(loaded_root.get_component<NameComponent>().name, "Root");
        EXPECT_EQ(loaded_child.get_component<NameComponent>().name, "Child");
        EXPECT_EQ(loaded->get_parent(loaded_child), loaded_root);

        const auto& loaded_root_transform = loaded_root.get_component<TransformComponent>();
        const auto& loaded_child_transform = loaded_child.get_component<TransformComponent>();
        EXPECT_VEC3_EQ(root_transform.translation, loaded_root_transform.translation);
        EXPECT_VEC3_EQ(root_transform.rotation, loaded_root_transform.rotation);
        EXPECT_VEC3_EQ(root_transform.scale, loaded_root_transform.scale);
        EXPECT_VEC3_EQ(child_transform.translation, loaded_child_transform.translation);
        EXPECT_VEC3_EQ(child_transform.rotation, loaded_child_transform.rotation);
        EXPECT_VEC3_EQ(child_transform.scale, loaded_child_transform.scale);

        const auto& mesh = loaded_child.get_component<MeshRendererComponent>();
        EXPECT_EQ(mesh.mesh, AssetHandle(101));
        EXPECT_EQ(mesh.material, AssetHandle(202));
        const auto& loaded_camera = loaded_root.get_component<CameraComponent>();
        EXPECT_TRUE(loaded_camera.primary);
        EXPECT_EQ(loaded_camera.projection, CameraComponent::Projection::Orthographic);
        EXPECT_FLOAT_EQ(loaded_camera.fov, 60.0f);
        EXPECT_FLOAT_EQ(loaded_camera.orthographic_height, 14.0f);
        EXPECT_FLOAT_EQ(loaded_camera.near_clip, 0.25f);
        EXPECT_FLOAT_EQ(loaded_camera.far_clip, 2500.0f);

        const Math::Mat4 expected_world = root_transform.to_matrix() * child_transform.to_matrix();
        EXPECT_MAT4_EQ(
            expected_world, loaded_child.get_component<WorldTransformComponent>().world_matrix);
        const auto serialized_again = serializer.serialize(*loaded);
        ASSERT_TRUE(serialized_again) << serialized_again.error();
        EXPECT_EQ(serialized_again.value(), contents);
    }

    TEST(SceneSerializerTest, CameraWithoutProjectionFieldsUsesPerspectiveDefaults) {
        constexpr std::string_view old_scene = R"({
            "version": 2,
            "entities": [{
                "uuid": "00000000-0000-4000-8000-000000000001",
                "components": {
                    "name": "Camera",
                    "camera": {"primary": true, "fov": 45, "near_clip": 0.1, "far_clip": 100}
                }
            }]
        })";
        const auto serializer = make_scene_serializer();
        auto loaded = serializer.deserialize(old_scene);
        ASSERT_TRUE(loaded) << loaded.error();
        auto entity = loaded.value()->find_entity(uuid("00000000-0000-4000-8000-000000000001"));
        ASSERT_TRUE(entity);
        const auto& camera = entity.get_component<CameraComponent>();
        EXPECT_EQ(camera.projection, CameraComponent::Projection::Perspective);
        EXPECT_FLOAT_EQ(camera.orthographic_height, 10.0f);

        std::string invalid(old_scene);
        const auto fov = invalid.find("\"fov\": 45");
        ASSERT_NE(fov, std::string::npos);
        invalid.insert(fov, "\"projection\": \"fisheye\", ");
        auto rejected = serializer.deserialize(invalid);
        ASSERT_FALSE(rejected);
        EXPECT_NE(rejected.error().find("unknown enum name"), std::string::npos);
    }

    TEST(SceneSerializerTest, PreservesMissingOptionalComponents) {
        const EntityUuid entity_uuid = uuid("00000000-0000-4000-8000-000000000030");
        Scene scene;
        Entity entity = scene.create_entity_with_uuid(entity_uuid, "Data Only");
        ASSERT_TRUE(entity);
        entity.remove_component<TransformComponent>();

        const SceneSerializer serializer = make_scene_serializer();
        auto contents_result = serializer.serialize(scene);
        ASSERT_TRUE(contents_result) << contents_result.error();
        auto contents = std::move(contents_result).value();
        auto loaded_result = serializer.deserialize(contents);
        ASSERT_TRUE(loaded_result) << loaded_result.error();
        auto loaded = std::move(loaded_result).value();
        Entity loaded_entity = loaded->find_entity(entity_uuid);

        ASSERT_TRUE(loaded_entity);
        EXPECT_FALSE(loaded_entity.has_component<TransformComponent>());
        EXPECT_FALSE(loaded_entity.has_component<MeshRendererComponent>());
        EXPECT_FALSE(loaded_entity.has_component<CameraComponent>());
        EXPECT_EQ(contents.find(R"("transform":)"), std::string::npos);
        auto cloned = serializer.clone(scene);
        ASSERT_TRUE(cloned) << cloned.error();
        const auto cloned_entity = cloned.value()->find_entity(entity_uuid);
        ASSERT_TRUE(cloned_entity);
        EXPECT_FALSE(cloned_entity.has_component<TransformComponent>());
    }

    TEST(SceneSerializerTest, ClonesIndependentRuntimeScene) {
        const EntityUuid parent_uuid = uuid("00000000-0000-4000-8000-000000000060");
        const EntityUuid child_uuid = uuid("00000000-0000-4000-8000-000000000061");
        Scene edit_scene;
        Entity edit_parent = edit_scene.create_entity_with_uuid(parent_uuid, "Edit Parent");
        Entity edit_child = edit_scene.create_entity_with_uuid(child_uuid, "Edit Child");
        ASSERT_TRUE(edit_parent);
        ASSERT_TRUE(edit_child);
        EXPECT_TRUE(edit_child.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(1.0f, 2.0f, 3.0f); }));
        ASSERT_TRUE(edit_scene.set_parent(edit_child, edit_parent));

        const SceneSerializer serializer = make_scene_serializer();
        auto runtime_scene_result = serializer.clone(edit_scene);
        ASSERT_TRUE(runtime_scene_result) << runtime_scene_result.error();
        auto runtime_scene = std::move(runtime_scene_result).value();

        ASSERT_NE(runtime_scene, nullptr);
        EXPECT_NE(runtime_scene.get(), &edit_scene);
        Entity runtime_parent = runtime_scene->find_entity(parent_uuid);
        Entity runtime_child = runtime_scene->find_entity(child_uuid);
        ASSERT_TRUE(runtime_parent);
        ASSERT_TRUE(runtime_child);
        EXPECT_EQ(runtime_scene->get_parent(runtime_child), runtime_parent);
        EXPECT_VEC3_EQ(runtime_child.get_component<TransformComponent>().translation,
            Math::Vec3(1.0f, 2.0f, 3.0f));

        runtime_parent.get_component<NameComponent>().name = "Runtime Parent";
        EXPECT_TRUE(
            runtime_child.try_edit_transform([&](auto& value) { value.translation.x = 9.0f; }));

        EXPECT_EQ(edit_parent.get_component<NameComponent>().name, "Edit Parent");
        EXPECT_FLOAT_EQ(edit_child.get_component<TransformComponent>().translation.x, 1.0f);
    }

    TEST(SceneSerializerTest, CloningActiveWorldCopiesContentWithoutRuntimeStateOrQueuedRequests) {
        Scene scene;
        const auto actor = scene.create_entity("Actor");
        SceneRuntime source_runtime;
        ASSERT_TRUE(source_runtime.start(scene));
        ASSERT_TRUE(source_runtime.get_session().set_value("score", 7.0f));
        ASSERT_TRUE(source_runtime.get_session().request_restart());
        const auto queued = scene.request_create_entity("Pending", {});
        ASSERT_TRUE(queued);

        const auto serializer = make_scene_serializer();
        auto cloned = serializer.clone(scene);
        ASSERT_TRUE(cloned) << cloned.error();
        EXPECT_EQ(cloned.value()->entity_count(), 1u);
        EXPECT_TRUE(cloned.value()->find_entity(actor.get_uuid()));
        EXPECT_FALSE(cloned.value()->find_entity(*queued));
        SceneRuntime clone_runtime;
        EXPECT_FALSE(clone_runtime.take_scene_request());
        EXPECT_FALSE(clone_runtime.get_session().set_value("score", 8.0f));
        ASSERT_TRUE(clone_runtime.start(*cloned.value()));
        EXPECT_FALSE(clone_runtime.get_session().get_value("score"));
        ASSERT_TRUE(clone_runtime.advance(1.0 / 60.0));
        EXPECT_EQ(cloned.value()->entity_count(), 1u);
        EXPECT_EQ(source_runtime.get_session().get_value("score"), ParameterValue(7.0f));
        EXPECT_TRUE(source_runtime.take_scene_request());
        ASSERT_TRUE(source_runtime.advance(1.0 / 60.0));
        EXPECT_TRUE(scene.find_entity(*queued));
    }

    TEST(SceneSerializerTest, CloneRestoresReadOnlyPropertiesWithoutEditNotifications) {
        auto registry = create_scene_component_registry();
        auto property = make_property_descriptor("value", "Value",
            &DescriptorTestComponent::persisted, {.editable = false, .read_only = true});
        property.on_changed = [](void*) { throw std::runtime_error("unexpected edit callback"); };
        ASSERT_TRUE(registry.register_component(make_component_descriptor<DescriptorTestComponent>(
            "custom", "Custom", {std::move(property)})));
        Scene scene;
        auto actor = scene.create_entity("Actor");
        actor.add_component<DescriptorTestComponent>().persisted = 42.0f;
        const SceneSerializer serializer(registry);
        auto cloned = serializer.clone(scene);
        ASSERT_TRUE(cloned) << cloned.error();
        EXPECT_FLOAT_EQ(cloned.value()
                            ->find_entity(actor.get_uuid())
                            .get_component<DescriptorTestComponent>()
                            .persisted,
            42.0f);
    }

    TEST(SceneSerializerTest, CloneRejectsInvalidContentWithoutTextRoundTrip) {
        Scene scene;
        auto actor = scene.create_entity("Actor");
        actor.add_component<CameraComponent>();
        const auto serializer = make_scene_serializer();
        actor.edit_component<CameraComponent>(
            [](auto& camera) { camera.fov = std::numeric_limits<float>::infinity(); });
        EXPECT_FALSE(serializer.clone(scene));
        actor.edit_component<CameraComponent>([](auto& camera) {
            camera.fov = 60.0f;
            camera.projection = static_cast<CameraComponent::Projection>(99);
        });
        EXPECT_FALSE(serializer.clone(scene));
        actor.edit_component<CameraComponent>(
            [](auto& camera) { camera.projection = CameraComponent::Projection::Perspective; });
        actor.get_component<NameComponent>().name = std::string(1, static_cast<char>(0xff));
        const auto invalid_name = serializer.clone(scene);
        ASSERT_FALSE(invalid_name);
        EXPECT_NE(invalid_name.error().find("UTF-8"), std::string::npos);
        actor.get_component<NameComponent>().name = "Actor";
        auto& script = actor.add_component<ScriptComponent>();
        script.parameters["text"] = std::string(1, static_cast<char>(0xff));
        EXPECT_FALSE(serializer.clone(scene));
        script.parameters["text"] = "valid";
        EXPECT_TRUE(serializer.clone(scene));
    }

    TEST(SceneSerializerTest, OrdersEntitiesByUuid) {
        Scene scene;
        const EntityUuid later = uuid("00000000-0000-4000-8000-000000000200");
        const EntityUuid earlier = uuid("00000000-0000-4000-8000-000000000100");
        ASSERT_TRUE(scene.create_entity_with_uuid(later, "Later"));
        ASSERT_TRUE(scene.create_entity_with_uuid(earlier, "Earlier"));

        const auto result = make_scene_serializer().serialize(scene);
        ASSERT_TRUE(result) << result.error();
        const auto& contents = result.value();

        EXPECT_LT(contents.find(earlier.to_string()), contents.find(later.to_string()));
    }

    TEST(SceneSerializerTest, SavesAndLoadsSceneFile) {
        const EntityUuid entity_uuid = uuid("00000000-0000-4000-8000-000000000040");
        Scene scene;
        ASSERT_TRUE(scene.create_entity_with_uuid(entity_uuid, "Saved"));
        const TemporaryDirectory directory;
        const auto path = (directory.path() / "saved.scene").string();
        const SceneSerializer serializer = make_scene_serializer();

        ASSERT_TRUE(serializer.save(scene, path));
        auto loaded_result = serializer.load(path);
        ASSERT_TRUE(loaded_result) << loaded_result.error();
        auto loaded = std::move(loaded_result).value();

        ASSERT_NE(loaded, nullptr);
        Entity entity = loaded->find_entity(entity_uuid);
        ASSERT_TRUE(entity);
        EXPECT_EQ(entity.get_component<NameComponent>().name, "Saved");
    }

    TEST(SceneSerializerTest, CreatesMissingParentDirectoriesWhenSaving) {
        Scene scene;
        ASSERT_TRUE(scene.create_entity("Saved"));
        const TemporaryDirectory directory;
        const auto path = (directory.path() / "nested" / "untitled.scene").string();
        ASSERT_FALSE(std::filesystem::exists(std::filesystem::path(path).parent_path()));

        const SceneSerializer serializer = make_scene_serializer();
        ASSERT_TRUE(serializer.save(scene, path));

        EXPECT_TRUE(std::filesystem::is_regular_file(path));
        auto loaded_result = serializer.load(path);
        ASSERT_TRUE(loaded_result) << loaded_result.error();
        auto loaded = std::move(loaded_result).value();
        ASSERT_NE(loaded, nullptr);
        EXPECT_EQ(loaded->entity_count(), 1u);
    }

    TEST(SceneSerializerTest, RejectsDuplicateUuid) {
        expect_scene_error(R"({
  "version": 2,
  "entities": [
    {
      "uuid": "00000000-0000-4000-8000-000000000001",
      "components": {"name": "First"}
    },
    {
      "uuid": "00000000-0000-4000-8000-000000000001",
      "components": {"name": "Second"}
    }
  ]
})",
            "duplicate UUID");
    }

    TEST(SceneSerializerTest, RejectsLegacyParentField) {
        expect_scene_error(R"({
  "version": 2,
  "entities": [
    {
      "uuid": "00000000-0000-4000-8000-000000000001",
      "parent": "00000000-0000-4000-8000-000000000099",
      "components": {"name": "Child"}
    }
  ]
})",
            "unknown field 'parent'");
    }

    TEST(SceneSerializerTest, RejectsRepeatedAncestorUuid) {
        expect_scene_error(R"({
  "version": 2,
  "entities": [
    {
      "uuid": "00000000-0000-4000-8000-000000000001",
      "components": {"name": "First"},
      "children": [{
        "uuid": "00000000-0000-4000-8000-000000000001",
        "components": {"name": "Repeated"}
      }]
    }
  ]
})",
            "entities[0].children[0].uuid': duplicate UUID");
    }

    TEST(SceneSerializerTest, LoadsNestedChildrenAndSortsOnlyWithinEachLevel) {
        const auto serializer = make_scene_serializer();
        const auto scene_result = serializer.deserialize(R"({
  "version": 2,
  "entities": [{
    "uuid": "00000000-0000-4000-8000-000000000003",
    "components": {"name": "Root"},
    "children": [{
      "uuid": "00000000-0000-4000-8000-000000000004",
      "components": {"name": "Later child"}
    }, {
      "uuid": "00000000-0000-4000-8000-000000000002",
      "components": {"name": "Earlier child"},
      "children": [{
        "uuid": "00000000-0000-4000-8000-000000000001",
        "components": {"name": "Grandchild"},
        "children": []
      }]
    }]
  }]
})");
        ASSERT_TRUE(scene_result) << scene_result.error();
        const auto& scene = scene_result.value();
        ASSERT_EQ(scene->entity_count(), 4U);
        const auto root = scene->find_entity(uuid("00000000-0000-4000-8000-000000000003"));
        const auto child = scene->find_entity(uuid("00000000-0000-4000-8000-000000000002"));
        const auto later = scene->find_entity(uuid("00000000-0000-4000-8000-000000000004"));
        const auto grandchild = scene->find_entity(uuid("00000000-0000-4000-8000-000000000001"));
        EXPECT_EQ(scene->get_parent(child), root);
        EXPECT_EQ(scene->get_parent(later), root);
        EXPECT_EQ(scene->get_parent(grandchild), child);
        auto text_result = serializer.serialize(*scene);
        ASSERT_TRUE(text_result) << text_result.error();
        auto text = std::move(text_result).value();
        EXPECT_LT(text.find(root.get_uuid().to_string()), text.find(child.get_uuid().to_string()));
        EXPECT_LT(
            text.find(child.get_uuid().to_string()), text.find(grandchild.get_uuid().to_string()));
        EXPECT_LT(
            text.find(grandchild.get_uuid().to_string()), text.find(later.get_uuid().to_string()));
        EXPECT_EQ(text.find(R"("parent":)"), std::string::npos);
        const auto decoded = serializer.deserialize(text);
        ASSERT_TRUE(decoded) << decoded.error();
        const auto cloned = serializer.clone(*scene);
        ASSERT_TRUE(cloned) << cloned.error();
        for(const auto entity : decoded.value()->get_entities()) {
            const auto cloned_entity = cloned.value()->find_entity(entity.get_uuid());
            ASSERT_TRUE(cloned_entity);
            EXPECT_EQ(cloned_entity.get_id(), entity.get_id());
        }
        EXPECT_EQ(cloned.value()->get_parent(cloned.value()->find_entity(grandchild.get_uuid())),
            cloned.value()->find_entity(child.get_uuid()));
        const auto cloned_text = serializer.serialize(*cloned.value());
        ASSERT_TRUE(cloned_text) << cloned_text.error();
        EXPECT_EQ(cloned_text.value(), text);
        const auto serialized_again = serializer.serialize(*decoded.value());
        ASSERT_TRUE(serialized_again) << serialized_again.error();
        EXPECT_EQ(serialized_again.value(), text);
    }

    TEST(SceneSerializerTest, RejectsMalformedChildrenWithNestedLocation) {
        for(const auto children : {"null", "{}", "[42]"}) {
            const std::string text = std::string(R"({"version": 2, "entities": [{
              "uuid": "00000000-0000-4000-8000-000000000001",
              "components": {"name": "Root"}, "children": )")
                                     + children + "}]}";
            expect_scene_error(text, "entities[0].children");
        }
    }

    TEST(SceneSerializerTest, BoundsHierarchyDepthOnReadAndSave) {
        Scene scene;
        Entity parent;
        for(std::size_t depth = 0; depth < SceneSerializer::MAX_HIERARCHY_DEPTH; ++depth) {
            auto entity = scene.create_entity("Node");
            if(parent)
                ASSERT_TRUE(scene.set_parent(entity, parent));
            parent = entity;
        }
        const auto serializer = make_scene_serializer();
        const TemporaryDirectory directory;
        const auto path = (directory.path() / "depth.scene").string();
        ASSERT_TRUE(serializer.save(scene, path));
        ASSERT_TRUE(serializer.clone(scene));
        const auto loaded = serializer.load(path);
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value()->entity_count(), scene.entity_count());
        ASSERT_TRUE(scene.set_parent(scene.create_entity("Too deep"), parent));
        EXPECT_FALSE(serializer.save(scene, path));
        const auto rejected_clone = serializer.clone(scene);
        ASSERT_FALSE(rejected_clone);
        EXPECT_NE(rejected_clone.error().find("maximum hierarchy depth"), std::string::npos);
        const auto preserved = serializer.load(path);
        ASSERT_TRUE(preserved) << preserved.error();
        EXPECT_EQ(preserved.value()->entity_count(), SceneSerializer::MAX_HIERARCHY_DEPTH);

        std::string children = "[]";
        for(std::size_t index = 0; index <= SceneSerializer::MAX_HIERARCHY_DEPTH; ++index) {
            const auto digits = std::to_string(index + 1);
            const auto id =
                "00000000-0000-4000-8000-" + std::string(12 - digits.size(), '0') + digits;
            children = "[{\"uuid\":\"" + id
                       + "\",\"components\":{\"name\":\"Node\"},\"children\":" + children + "}]";
        }
        expect_scene_error(
            "{\"version\":2,\"entities\":" + children + "}", "maximum hierarchy depth exceeded");
    }

    TEST(SceneSerializerTest, RejectsUnknownAndMalformedFields) {
        expect_scene_error(R"({
  "version": 2,
  "entities": [
    {
      "uuid": "00000000-0000-4000-8000-000000000001",
      "components": {
        "name": "Invalid",
        "transform": {"translation": [1, 2], "rotation": [0, 0, 0], "scale": [1, 1, 1]}
      }
    }
  ]
})",
            "exactly 3 numbers");

        expect_scene_error(
            R"({"version": 2, "entities": [], "runtime_id": 1})", "unknown field 'runtime_id'");

        expect_scene_error(R"({
  "version": 2,
  "entities": [
    {
      "uuid": "00000000-0000-4000-8000-000000000001",
      "components": {"name": "Invalid", "mesh_renderer": {"mesh": -1, "material": 2}}
    }
  ]
})",
            "expected a non-negative integer");

        expect_scene_error(R"({
  "version": 2,
  "entities": [
    {
      "uuid": "00000000-0000-4000-8000-000000000001",
      "components": {"name": "First", "name": "Second"}
    }
  ]
})",
            "duplicate field 'name'");
    }

    TEST(SceneSerializerTest, RejectsUnsupportedVersionAndNonFiniteValues) {
        expect_scene_error(R"({"version": 3, "entities": []})", "unsupported version 3");

        Scene scene;
        Entity entity = scene.create_entity("Invalid");
        entity.add_component<CameraComponent>(
            CameraComponent{.fov = std::numeric_limits<float>::infinity()});
        const auto result = make_scene_serializer().serialize(scene);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().find("finite"), std::string::npos);
    }

    TEST(SceneSerializerTest, PreservesFullWidthHandlesAndRejectsWrongJsonTypes) {
        Scene scene;
        auto entity = scene.create_entity("场景\"\\\n");
        entity.add_component<MeshRendererComponent>(
            AssetHandle(std::numeric_limits<std::uint64_t>::max()), AssetHandle(42));
        const auto serializer = make_scene_serializer();
        auto contents_result = serializer.serialize(scene);
        ASSERT_TRUE(contents_result) << contents_result.error();
        auto contents = std::move(contents_result).value();
        auto loaded_result = serializer.deserialize(contents);
        ASSERT_TRUE(loaded_result) << loaded_result.error();
        auto loaded = std::move(loaded_result).value();
        EXPECT_EQ(
            loaded->find_entity(entity.get_uuid()).get_component<MeshRendererComponent>().mesh,
            AssetHandle(std::numeric_limits<std::uint64_t>::max()));
        const auto serialized_again = serializer.serialize(*loaded);
        ASSERT_TRUE(serialized_again) << serialized_again.error();
        EXPECT_EQ(serialized_again.value(), contents);
        expect_scene_error(R"({"version": "2", "entities": []})", "non-negative integer");
        expect_scene_error(R"({"version": 2, "entities": {}})", "expected an array");
        expect_scene_error(R"({"version": 2, "entities": [{
          "uuid": "00000000-0000-4000-8000-000000000001",
          "components": {"name": 42}
        }]})",
            "expected a string");
        expect_scene_error("version: 1\nentities: []\n", "<json>");
    }

    TEST(SceneSerializerTest, DoesNotMaskComponentCallbackFailuresAsInvalidSceneData) {
        ComponentRegistry registry = create_scene_component_registry();
        auto property =
            make_property_descriptor("value", "Value", &DescriptorTestComponent::persisted);
        property.const_accessor = [](const void*) -> const void* {
            throw std::runtime_error("component accessor failed");
        };
        ASSERT_TRUE(registry.register_component(make_component_descriptor<DescriptorTestComponent>(
            "custom", "Custom", {std::move(property)})));
        Scene scene;
        scene.create_entity("Custom").add_component<DescriptorTestComponent>();
        const SceneSerializer serializer(registry);
        EXPECT_THROW(static_cast<void>(serializer.serialize(scene)), std::runtime_error);
        EXPECT_THROW(static_cast<void>(serializer.clone(scene)), std::runtime_error);
    }

    TEST(SceneSerializerTest, UsesDescriptorIdsAndSerializationFlags) {
        ComponentRegistry registry = create_scene_component_registry();
        ASSERT_TRUE(registry.register_component(make_component_descriptor<DescriptorTestComponent>(
            "descriptor_component", "Descriptor Component",
            {make_property_descriptor(
                 "persisted_value", "Persisted Value", &DescriptorTestComponent::persisted),
                make_property_descriptor("text", "Text", &DescriptorTestComponent::text),
                make_property_descriptor("runtime_value", "Runtime Value",
                    &DescriptorTestComponent::runtime_only,
                    {.serializable = false, .transient = true})})));
        ASSERT_TRUE(registry.register_component(make_component_descriptor<RuntimeOnlyTestComponent>(
            "runtime_component", "Runtime Component",
            {make_property_descriptor("enabled", "Enabled", &RuntimeOnlyTestComponent::enabled)},
            false)));

        Scene scene;
        const EntityUuid entity_uuid = uuid("00000000-0000-4000-8000-000000000050");
        Entity entity = scene.create_entity_with_uuid(entity_uuid, "Descriptor Driven");
        auto& component = entity.add_component<DescriptorTestComponent>();
        component.persisted = 42.0f;
        component.runtime_only = 99.0f;
        component.text = "文本: \"quoted\"\n" + std::string(1024, 'x');
        entity.add_component<RuntimeOnlyTestComponent>().enabled = false;

        const SceneSerializer serializer(registry);
        auto contents_result = serializer.serialize(scene);
        ASSERT_TRUE(contents_result) << contents_result.error();
        auto contents = std::move(contents_result).value();
        EXPECT_NE(contents.find(R"("descriptor_component":)"), std::string::npos);
        EXPECT_NE(contents.find(R"("persisted_value": 42)"), std::string::npos);
        EXPECT_EQ(contents.find("runtime_value"), std::string::npos);
        EXPECT_EQ(contents.find("runtime_component"), std::string::npos);

        auto loaded_result = serializer.deserialize(contents);
        ASSERT_TRUE(loaded_result) << loaded_result.error();
        auto loaded = std::move(loaded_result).value();
        const Entity loaded_entity = loaded->find_entity(entity_uuid);
        ASSERT_TRUE(loaded_entity);
        ASSERT_TRUE(loaded_entity.has_component<DescriptorTestComponent>());
        const auto& loaded_component = loaded_entity.get_component<DescriptorTestComponent>();
        EXPECT_FLOAT_EQ(loaded_component.persisted, 42.0f);
        EXPECT_FLOAT_EQ(loaded_component.runtime_only, 17.0f);
        EXPECT_EQ(loaded_component.text, component.text);
        EXPECT_FALSE(loaded_entity.has_component<RuntimeOnlyTestComponent>());

        auto cloned = serializer.clone(scene);
        ASSERT_TRUE(cloned) << cloned.error();
        auto cloned_entity = cloned.value()->find_entity(entity_uuid);
        ASSERT_TRUE(cloned_entity.has_component<DescriptorTestComponent>());
        auto& cloned_component = cloned_entity.get_component<DescriptorTestComponent>();
        EXPECT_FLOAT_EQ(cloned_component.persisted, component.persisted);
        EXPECT_FLOAT_EQ(cloned_component.runtime_only, 17.0f);
        EXPECT_EQ(cloned_component.text, component.text);
        EXPECT_FALSE(cloned_entity.has_component<RuntimeOnlyTestComponent>());
        cloned_component.text.clear();
        EXPECT_FALSE(component.text.empty());

        expect_scene_error(serializer, R"({
  "version": 2,
  "entities": [
    {
      "uuid": "00000000-0000-4000-8000-000000000050",
      "components": {
        "name": "Invalid",
        "descriptor_component": {"persisted_value": 42, "runtime_value": 99}
      }
    }
  ]
})",
            "unknown field 'runtime_value'");
    }
    TEST(SceneSerializerTest, RealtimeIrradianceKeepsSettingsAndSkipsBakedRuntimeDependency) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        Scene scene;
        auto environment = scene.get_environment();
        environment.realtime_gi_budget = 3;
        ASSERT_TRUE(scene.set_environment(environment));
        auto entity = scene.create_entity("实时探针");
        auto transform = entity.get_component<TransformComponent>();
        transform.translation = {2, 3, 4};
        entity.set_transform(transform);
        IrradianceVolumeComponent volume;
        volume.realtime = true;
        volume.irradiance = AssetHandle(999);
        volume.update_budget = 12;
        volume.update_rays = 128;
        volume.history_weight = .85f;
        volume.blend_distance = 1.25f;
        entity.add_component<IrradianceVolumeComponent>(volume);
        auto moving = scene.create_entity("移动网格");
        moving.add_component<MeshRendererComponent>(
            MeshRendererComponent{AssetHandle(1), AssetHandle(2), false});
        const auto text = serializer.serialize(scene);
        ASSERT_TRUE(text) << text.error();
        auto loaded = serializer.deserialize(text.value());
        ASSERT_TRUE(loaded) << loaded.error();
        auto copy = serializer.clone(*loaded.value());
        ASSERT_TRUE(copy) << copy.error();
        EXPECT_EQ(
            copy.value()->find_entity(entity.get_uuid()).get_component<IrradianceVolumeComponent>(),
            volume);
        EXPECT_FALSE(copy.value()
                ->find_entity(moving.get_uuid())
                .get_component<MeshRendererComponent>()
                .contributes_to_gi);
        const auto snapshot = SceneExtractor::extract(*copy.value());
        ASSERT_EQ(snapshot.irradiance_volumes.size(), 1u);
        const auto& extracted = snapshot.irradiance_volumes[0];
        EXPECT_EQ(snapshot.environment.realtime_gi_budget, 3u);
        EXPECT_FLOAT_EQ(extracted.blend_distance, 1.25f);
        EXPECT_TRUE(extracted.realtime);
        EXPECT_EQ(extracted.center, Math::Vec3(2, 3, 4));
        EXPECT_EQ(extracted.update_budget, 12u);
        EXPECT_EQ(extracted.update_rays, 128u);
        EXPECT_FLOAT_EQ(extracted.history_weight, .85f);
        ASSERT_EQ(snapshot.render_items.size(), 1u);
        EXPECT_FALSE(snapshot.render_items[0].contributes_to_gi);
        const auto runtime = registry.collect_asset_references(
            *copy.value(), ComponentRegistry::ReferenceScope::Runtime);
        EXPECT_FALSE(std::ranges::any_of(
            runtime, [](const auto& ref) { return ref.type == AssetType::Irradiance; }));
        const auto edit = registry.collect_asset_references(*copy.value());
        EXPECT_TRUE(std::ranges::any_of(
            edit, [](const auto& ref) { return ref.type == AssetType::Irradiance; }));
    }

}

#include "render/lighting.h"
#include "render/scene/scene_extractor.h"
#include "render/scene/scene_resolver.h"
#include "asset/registry.h"
#include "scene/scene_serializer.h"
#include "scene/component_registry.h"
#include "scene/command_history.h"
#include "graphics/pipeline/shader_interface.h"
#include "pbr_frag.h"

#include <gtest/gtest.h>
#include <limits>
#include <algorithm>

namespace Comet::Tests {
    TEST(LightingTest, OwnsWorldPoseAndIgnoresLocalAndAncestorScaleForDirection) {
        Scene scene;
        auto parent = scene.create_entity("parent");
        const auto& transform = parent.get_component<TransformComponent>();
        EXPECT_TRUE(parent.try_edit_transform([&](auto& value) { value.translation = {4, 2, 1}; }));
        EXPECT_TRUE(parent.try_edit_transform([&](auto& value) { value.rotation = {0, 90, 0}; }));
        EXPECT_TRUE(parent.try_edit_transform([&](auto& value) { value.scale = {2, -3, -4}; }));
        auto light = scene.create_entity("light");
        light.add_component<LightComponent>(LightComponent{.type = LightType::Spot});
        light.edit_component<LightComponent>(
            [&](auto& component) { component.casts_shadow = true; });
        const auto& child = light.get_component<TransformComponent>();
        EXPECT_TRUE(light.try_edit_transform([&](auto& value) { value.translation = {0, 0, 2}; }));
        EXPECT_TRUE(light.try_edit_transform([&](auto& value) { value.scale = {0, -10, 0}; }));
        ASSERT_TRUE(scene.set_parent(light, parent));
        auto extracted = SceneExtractor::extract(scene);
        ASSERT_EQ(extracted.lights.size(), 1);
        const auto expected = transform.to_matrix() * Math::Vec4(child.translation, 1);
        EXPECT_LT(Math::length(extracted.lights[0].position - Math::Vec3(expected)), 1e-5f);
        EXPECT_LT(
            Math::length(Math::normalize(extracted.lights[0].direction) - Math::Vec3(-1, 0, 0)),
            1e-5f);
        light.edit_component<LightComponent>([&](auto& component) { component.enabled = false; });
        EXPECT_TRUE(SceneExtractor::extract(scene).lights.empty());
        EXPECT_EQ(extracted.lights[0].type, LightType::Spot);
        AssetRegistry assets;
        SceneResolver resolver(assets);
        const auto world = resolver.resolve(extracted);
        extracted.lights.clear();
        ASSERT_EQ(world.lights.size(), 1);
        EXPECT_EQ(world.lights[0].entity_id, light.get_id());
        EXPECT_TRUE(world.lights[0].casts_shadow);
    }

    TEST(LightingTest, PacksDeterministicBoundedValidLightsAndReportsRejectedInputs) {
        std::vector<RenderLight> lights;
        for(unsigned index = 40; index > 0; --index)
            lights.push_back(
                {.entity_id = index, .position = {float(index), 0, 0}, .direction = {0, 0, -5}});
        auto invalid = lights.front();
        invalid.type = static_cast<LightType>(99);
        lights.push_back(invalid);
        invalid = lights.front();
        invalid.direction = {};
        lights.push_back(invalid);
        invalid = lights.front();
        invalid.color.x = std::numeric_limits<float>::quiet_NaN();
        lights.push_back(invalid);
        const auto data = LightingData::prepare(lights);
        EXPECT_EQ(data.light_count, 32);
        EXPECT_EQ(data.excess_lights, 8);
        EXPECT_EQ(data.invalid_lights, 3);
        for(const auto& light : data.lights) {
            EXPECT_EQ(light.direction, Math::Vec3(0, 0, -1));
            EXPECT_EQ(light.range, 0);
            EXPECT_TRUE(Math::is_finite(light.position));
        }
        for(auto& light : lights)
            light.type = LightType::Point;
        const auto points = LightingData::prepare(lights);
        EXPECT_EQ(points.lights.front().position.x, 1);
        EXPECT_EQ(points.lights.back().position.x, 32);
    }

    TEST(LightingTest, BoundedSelectionMatchesStableFullSortWithDuplicateIdentities) {
        std::vector<RenderLight> lights;
        for(unsigned index = 0; index < 768; ++index) {
            lights.push_back({.entity_id = (index * 73) % 97,
                .type = LightType::Point,
                .position = {float(index), 0, 1},
                .range = index % 17 == 0 ? 0.0f : 10.0f,
                .casts_shadow = index % 3 == 0});
        }
        auto reference = lights;
        std::erase_if(reference, [](const auto& light) { return light.range == 0; });
        std::stable_sort(reference.begin(), reference.end(),
            [](const auto& a, const auto& b) { return a.entity_id < b.entity_id; });
        const auto packed = LightingData::prepare(lights);
        EXPECT_EQ(packed.light_count, LightingData::MAX_LIGHTS);
        EXPECT_EQ(packed.invalid_lights, lights.size() - reference.size());
        EXPECT_EQ(packed.excess_lights, reference.size() - LightingData::MAX_LIGHTS);
        for(size_t index = 0; index < packed.lights.size(); ++index) {
            EXPECT_EQ(packed.lights[index].position, reference[index].position);
            EXPECT_EQ(
                packed.lights[index].casts_shadow, reference[index].casts_shadow ? 1.0f : 0.0f);
        }
    }

    TEST(LightingTest, RejectsInvalidRangesConesAndEnergyWithoutPoisoningValidLights) {
        RenderLight light{.type = LightType::Spot};
        EXPECT_EQ(LightingData::prepare(std::span(&light, 1)).light_count, 1);
        light.inner_angle = light.outer_angle;
        EXPECT_EQ(LightingData::prepare(std::span(&light, 1)).invalid_lights, 1);
        light.type = LightType::Point;
        light.range = -1;
        EXPECT_EQ(LightingData::prepare(std::span(&light, 1)).invalid_lights, 1);
        light.range = 10;
        light.intensity = std::numeric_limits<float>::infinity();
        EXPECT_EQ(LightingData::prepare(std::span(&light, 1)).invalid_lights, 1);
        light.intensity = 1;
        light.direction.x = std::numeric_limits<float>::quiet_NaN();
        const auto data = LightingData::prepare(std::span(&light, 1));
        EXPECT_EQ(data.light_count, 1);
        EXPECT_TRUE(Math::is_finite(data.lights.front().direction));
    }

    TEST(LightingTest, ValidatesOnlyFieldsUsedByEachLightType) {
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        std::array<RenderLight, 3> lights{{{.entity_id = 1,
                                               .type = LightType::Directional,
                                               .position = {nan, nan, nan},
                                               .range = nan,
                                               .inner_angle = nan,
                                               .outer_angle = nan},
            {.entity_id = 2,
                .type = LightType::Point,
                .direction = {nan, nan, nan},
                .inner_angle = nan,
                .outer_angle = nan},
            {.entity_id = 3, .type = LightType::Spot}}};
        const auto valid = LightingData::prepare(lights);
        ASSERT_EQ(valid.light_count, 3);
        EXPECT_EQ(valid.invalid_lights, 0);
        EXPECT_EQ(valid.lights[0].position, Math::Vec3(0));
        EXPECT_EQ(valid.lights[0].range, 0);
        EXPECT_EQ(valid.lights[1].direction, Math::Vec3(0, 0, -1));
        EXPECT_EQ(valid.lights[1].inner_cone_cos, 0);
        EXPECT_EQ(valid.lights[1].outer_cone_cos, 0);

        lights[2].inner_angle = nan;
        EXPECT_EQ(LightingData::prepare(lights).invalid_lights, 1);
        lights[1].position.x = nan;
        EXPECT_EQ(LightingData::prepare(lights).invalid_lights, 2);
        lights[0].direction.x = nan;
        const auto invalid = LightingData::prepare(lights);
        EXPECT_EQ(invalid.light_count, 0);
        EXPECT_EQ(invalid.invalid_lights, 3);
    }

    TEST(LightingTest, ShadowDistanceIsValidatedOnlyForShadowCastingDirectionalLights) {
        RenderLight light{.casts_shadow = true};
        for(float distance : {0.0f, -1.0f, 10001.0f, std::numeric_limits<float>::quiet_NaN()}) {
            light.shadow_distance = distance;
            EXPECT_EQ(LightingData::prepare(std::span(&light, 1)).invalid_lights, 1);
        }
        light.casts_shadow = false;
        EXPECT_EQ(LightingData::prepare(std::span(&light, 1)).light_count, 1);
        light.casts_shadow = true;
        light.type = LightType::Point;
        EXPECT_EQ(LightingData::prepare(std::span(&light, 1)).light_count, 1);
    }

    TEST(LightingTest, ShaderFrameBlockMatchesCpuPacking) {
        const auto reflected = ShaderInterface::reflect(PBR_FRAG);
        ASSERT_TRUE(reflected) << reflected.error();
        const auto& shader = reflected.value();
        const auto found = std::ranges::find_if(shader.get_bindings(),
            [](const auto& binding) { return binding.set == 0 && binding.binding == 1; });
        ASSERT_NE(found, shader.get_bindings().end());
        EXPECT_EQ(found->block_size, sizeof(LightingData));
        const std::array fields{std::pair{"lights", offsetof(LightingData, lights)},
            std::pair{"light_count", offsetof(LightingData, light_count)},
            std::pair{"excess_lights", offsetof(LightingData, excess_lights)},
            std::pair{"invalid_lights", offsetof(LightingData, invalid_lights)},
            std::pair{"reserved", offsetof(LightingData, reserved)},
            std::pair{"shadow_view_projection", offsetof(LightingData, shadow_view_projection)},
            std::pair{"shadow_splits", offsetof(LightingData, shadow_splits)},
            std::pair{"shadow_depth_bias", offsetof(LightingData, shadow_depth_bias)},
            std::pair{"shadow_camera_depth", offsetof(LightingData, shadow_camera_depth)},
            std::pair{"shadow_light_index", offsetof(LightingData, shadow_light_index)},
            std::pair{"shadow_cascade_count", offsetof(LightingData, shadow_cascade_count)},
            std::pair{"shadow_texel_size", offsetof(LightingData, shadow_texel_size)},
            std::pair{"shadow_blend", offsetof(LightingData, shadow_blend)},
            std::pair{"environment", offsetof(LightingData, environment)}};
        ASSERT_EQ(found->members.size(), fields.size());
        for(size_t index = 0; index < fields.size(); ++index) {
            SCOPED_TRACE(fields[index].first);
            EXPECT_EQ(found->members[index].name, fields[index].first);
            EXPECT_EQ(found->members[index].offset, fields[index].second);
        }
        const std::array light_fields{
            std::pair{"position", offsetof(LightingData::Light, position)},
            std::pair{"type", offsetof(LightingData::Light, type)},
            std::pair{"direction", offsetof(LightingData::Light, direction)},
            std::pair{"range", offsetof(LightingData::Light, range)},
            std::pair{"color", offsetof(LightingData::Light, color)},
            std::pair{"intensity", offsetof(LightingData::Light, intensity)},
            std::pair{"inner_cone_cos", offsetof(LightingData::Light, inner_cone_cos)},
            std::pair{"outer_cone_cos", offsetof(LightingData::Light, outer_cone_cos)},
            std::pair{"casts_shadow", offsetof(LightingData::Light, casts_shadow)},
            std::pair{"shadow_distance", offsetof(LightingData::Light, shadow_distance)}};
        EXPECT_EQ(found->members.front().shape.array_stride, sizeof(LightingData::Light));
        EXPECT_EQ(found->members.front().shape.array_dimensions,
            (std::vector<uint32_t>{LightingData::MAX_LIGHTS}));
        const auto& members = found->members.front().members;
        ASSERT_EQ(members.size(), light_fields.size());
        for(size_t index = 0; index < light_fields.size(); ++index) {
            SCOPED_TRACE(light_fields[index].first);
            EXPECT_EQ(members[index].name, light_fields[index].first);
            EXPECT_EQ(members[index].offset, light_fields[index].second);
        }
    }

    TEST(LightingTest, CascadesCoverOverlappingCameraSlicesForEveryLightDirection) {
        const BoundingBox bounds{{-100, -10, -150}, {100, 80, 20}};
        RenderCamera camera{
            .view_matrix = Math::look_at({0, 2, 8}, {0, 0, 0}, {0, 1, 0}), .far_clip = 200};
        const auto projection = *camera.projection_matrix(16.0f / 9);
        const auto inverse = Math::inverse(projection * camera.view_matrix);
        for(const auto direction : {Math::Vec3(0, 0, -1), Math::Vec3(0, -1, 0), Math::Vec3(0, 1, 0),
                Math::Vec3(1, -2, 3)}) {
            const RenderLight light{
                .direction = direction, .casts_shadow = true, .shadow_distance = 60};
            auto data = LightingData::prepare(std::span(&light, 1));
            data.prepare_shadow(bounds, 1024, camera.view_matrix, projection);
            ASSERT_EQ(data.shadow_cascade_count, 4);
            EXPECT_FLOAT_EQ(data.shadow_splits.w, 60);
            EXPECT_FLOAT_EQ(data.shadow_texel_size, 1.0f / 1024);
            float previous = 0, previous_start = 0;
            for(unsigned cascade = 0; cascade < 4; ++cascade) {
                EXPECT_GT(data.shadow_splits[cascade], previous);
                const float start = previous - (previous - previous_start) * data.shadow_blend;
                for(unsigned corner = 0; corner < 4; ++corner) {
                    auto near =
                        inverse * Math::Vec4(corner & 1 ? 1 : -1, corner & 2 ? 1 : -1, 0, 1);
                    auto far = inverse * Math::Vec4(corner & 1 ? 1 : -1, corner & 2 ? 1 : -1, 1, 1);
                    near /= near.w;
                    far /= far.w;
                    for(float depth : {start, data.shadow_splits[cascade]}) {
                        const auto point =
                            near + (far - near) * (depth / (camera.far_clip - camera.near_clip));
                        const auto clip = data.shadow_view_projection[cascade] * point;
                        EXPECT_LE(std::abs(clip.x), 1.0001f);
                        EXPECT_LE(std::abs(clip.y), 1.0001f);
                        EXPECT_GE(clip.z, 0);
                        EXPECT_LE(clip.z, 1);
                        EXPECT_NEAR(
                            Math::dot(Math::Vec3(data.shadow_camera_depth), Math::Vec3(point))
                                + data.shadow_camera_depth.w,
                            depth, .01f);
                    }
                }
                previous_start = previous;
                previous = data.shadow_splits[cascade];
            }
        }
    }

    TEST(LightingTest, CascadeProjectionSnapsSmallCameraMotionAndRejectsInvalidCamera) {
        const RenderLight light{.casts_shadow = true};
        auto first = LightingData::prepare(std::span(&light, 1));
        const BoundingBox bounds{{-50, -50, -100}, {50, 50, 50}};
        RenderCamera camera{
            .view_matrix = Math::look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0}), .far_clip = 200};
        const auto projection = *camera.projection_matrix(1);
        first.prepare_shadow(bounds, 1024, camera.view_matrix, projection);
        auto next = first;
        camera.view_matrix = Math::look_at({.00001f, 0, 5}, {.00001f, 0, 0}, {0, 1, 0});
        next.prepare_shadow(bounds, 1024, camera.view_matrix, projection);
        for(unsigned cascade = 0; cascade < 4; ++cascade)
            for(unsigned column = 0; column < 4; ++column)
                for(unsigned row = 0; row < 2; ++row)
                    EXPECT_NEAR(first.shadow_view_projection[cascade][column][row],
                        next.shadow_view_projection[cascade][column][row], 1e-6f);
        camera.projection = RenderCamera::Projection::Orthographic;
        next.prepare_shadow(bounds, 1024, camera.view_matrix, *camera.projection_matrix(1));
        EXPECT_EQ(next.shadow_cascade_count, 4);
        EXPECT_NEAR(next.shadow_splits.x, 25, 1e-4f);
        next.prepare_shadow(bounds, 1024, camera.view_matrix, Math::Mat4(0));
        EXPECT_EQ(next.shadow_light_index, -1);
        EXPECT_EQ(next.shadow_cascade_count, 0);
    }

    TEST(LightingTest, ShadowSelectsOnlyRetainedDirectionalLightAndResetsInvalidFit) {
        std::vector<RenderLight> lights{{.entity_id = 8, .casts_shadow = true},
            {.entity_id = 1, .type = LightType::Point, .casts_shadow = true}, {.entity_id = 2},
            {.entity_id = 4, .intensity = 0, .casts_shadow = true},
            {.entity_id = 6, .casts_shadow = true}};
        auto data = LightingData::prepare(lights);
        const BoundingBox bounds{{-1, -1, -1}, {1, 1, 1}};
        data.prepare_shadow(bounds, 1024, Math::Mat4(1), Math::Mat4(1));
        EXPECT_EQ(data.shadow_light_index, 3);
        data.prepare_shadow(bounds, 0, Math::Mat4(1), Math::Mat4(1));
        EXPECT_EQ(data.shadow_light_index, -1);
        data.prepare_shadow(BoundingBox::from_point({0, 0, 0}), 1024, Math::Mat4(1), Math::Mat4(1));
        EXPECT_EQ(data.shadow_light_index, -1);
        data.prepare_shadow({{2, 0, 0}, {1, 1, 1}}, 1024, Math::Mat4(1), Math::Mat4(1));
        EXPECT_EQ(data.shadow_light_index, -1);
        lights.clear();
        for(uint32_t id = 0; id < 33; ++id)
            lights.push_back({.entity_id = id, .casts_shadow = id == 32});
        data = LightingData::prepare(lights);
        data.prepare_shadow(bounds, 1024, Math::Mat4(1), Math::Mat4(1));
        EXPECT_EQ(data.shadow_light_index, -1);
    }

    TEST(LightingTest, ShadowSettingsUsePropertyUndoSerializationAndLegacyDefault) {
        auto registry = create_scene_component_registry();
        SceneSerializer serializer(registry);
        Scene scene;
        auto light = scene.create_entity("shadow light");
        light.add_component<LightComponent>();
        CometEditor::CommandHistory history;
        history.bind_scene(&scene);
        CometEditor::PropertyEditTransaction edit(history, registry);
        ASSERT_TRUE(edit.begin({light.get_uuid(), "light", "casts_shadow"}));
        ASSERT_TRUE(edit.preview(true));
        ASSERT_TRUE(edit.commit());
        EXPECT_TRUE(light.get_component<LightComponent>().casts_shadow);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(light.get_component<LightComponent>().casts_shadow);
        ASSERT_TRUE(history.redo());
        ASSERT_TRUE(edit.begin({light.get_uuid(), "light", "shadow_distance"}));
        ASSERT_TRUE(edit.preview(60.0f));
        ASSERT_TRUE(edit.commit());
        EXPECT_EQ(light.get_component<LightComponent>().shadow_distance, 60);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(light.get_component<LightComponent>().shadow_distance, 100);
        ASSERT_TRUE(history.redo());
        const auto extracted = SceneExtractor::extract(scene);
        ASSERT_EQ(extracted.lights.size(), 1u);
        EXPECT_EQ(extracted.lights.front().shadow_distance, 60);
        auto cloned = serializer.clone(scene);
        ASSERT_TRUE(cloned) << cloned.error();
        EXPECT_TRUE(cloned.value()
                ->find_entity(light.get_uuid())
                .get_component<LightComponent>()
                .casts_shadow);
        EXPECT_EQ(cloned.value()
                      ->find_entity(light.get_uuid())
                      .get_component<LightComponent>()
                      .shadow_distance,
            60);
        auto saved = serializer.serialize(scene);
        ASSERT_TRUE(saved) << saved.error();
        auto reopened = serializer.deserialize(saved.value());
        ASSERT_TRUE(reopened) << reopened.error();
        EXPECT_EQ(reopened.value()
                      ->find_entity(light.get_uuid())
                      .get_component<LightComponent>()
                      .shadow_distance,
            60);
        auto legacy = serializer.deserialize(R"({"version": 2, "entities": [{
            "uuid": "672cd0cc-501f-419e-af5e-a883a0cd3d03",
            "components": {"name": "Legacy light", "light": {
                "type": "directional", "enabled": true, "color": [1, 1, 1],
                "intensity": 1, "range": 10, "inner_angle": 20, "outer_angle": 30
            }}
        }]})");
        ASSERT_TRUE(legacy) << legacy.error();
        const auto legacy_id = EntityUuid::parse("672cd0cc-501f-419e-af5e-a883a0cd3d03");
        ASSERT_TRUE(legacy_id);
        EXPECT_FALSE(
            legacy.value()->find_entity(*legacy_id).get_component<LightComponent>().casts_shadow);
        EXPECT_EQ(
            legacy.value()->find_entity(*legacy_id).get_component<LightComponent>().shadow_distance,
            100);
    }

    TEST(LightingTest, TypedEnumRoundTripsAndUsesExistingPropertyUndoAndClone) {
        auto registry = create_scene_component_registry();
        SceneSerializer serializer(registry);
        Scene scene;
        auto light = scene.create_entity("editable light");
        light.add_component<LightComponent>();
        const auto* descriptor = registry.find_component("light");
        const auto* type = descriptor->find_property("type");
        ASSERT_EQ(type->type, PropertyType::Enum);
        EXPECT_EQ(std::get<std::string>(*type->copy_value(descriptor->get_component(light))),
            "directional");
        CometEditor::CommandHistory history;
        history.bind_scene(&scene);
        CometEditor::PropertyEditTransaction edit(history, registry);
        ASSERT_TRUE(edit.begin({light.get_uuid(), "light", "type"}));
        ASSERT_TRUE(edit.preview(std::string("spot")));
        EXPECT_FALSE(edit.preview(std::string("unknown")));
        ASSERT_TRUE(edit.commit());
        EXPECT_EQ(light.get_component<LightComponent>().type, LightType::Spot);
        ASSERT_TRUE(history.undo());
        EXPECT_EQ(light.get_component<LightComponent>().type, LightType::Directional);
        ASSERT_TRUE(history.redo());
        auto cloned = serializer.clone(scene);
        ASSERT_TRUE(cloned) << cloned.error();
        auto clone = std::move(cloned).value();
        EXPECT_EQ(clone->find_entity(light.get_uuid()).get_component<LightComponent>().type,
            LightType::Spot);
        clone->find_entity(light.get_uuid()).edit_component<LightComponent>([&](auto& component) {
            component.intensity = 100;
        });
        EXPECT_EQ(light.get_component<LightComponent>().intensity, 1);
        auto serialized = serializer.serialize(scene);
        ASSERT_TRUE(serialized) << serialized.error();
        auto text = std::move(serialized).value();
        const auto offset = text.find("\"spot\"");
        ASSERT_NE(offset, std::string::npos);
        text.replace(offset, std::string("\"spot\"").size(), "\"invalid\"");
        EXPECT_FALSE(serializer.deserialize(text));
        light.edit_component<LightComponent>(
            [&](auto& component) { component.type = static_cast<LightType>(100); });
        EXPECT_FALSE(type->copy_value(descriptor->get_component(light)));
        EXPECT_FALSE(serializer.serialize(scene));
    }

    TEST(LightingTest, RejectsEnumMetadataWithDuplicateNamesOrMissingTypedAccess) {
        const auto builtins = create_scene_component_registry();
        auto light = *builtins.find_component("light");
        light.properties.front().enum_options.push_back(
            light.properties.front().enum_options.front());
        ComponentRegistry registry;
        EXPECT_FALSE(registry.register_component(light));
        light = *builtins.find_component("light");
        light.properties.front().write_enum = {};
        EXPECT_FALSE(registry.register_component(light));
    }
}

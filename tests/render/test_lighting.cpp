#include "render/lighting.h"
#include "render/scene/scene_extractor.h"
#include "render/scene/scene_resolver.h"
#include "asset/registry.h"
#include "scene/scene_serializer.h"
#include "scene/component_registry.h"
#include "command_history.h"
#include "graphics/pipeline/shader_interface.h"
#include "material_lit_frag.h"

#include <gtest/gtest.h>
#include <limits>

namespace Comet::Tests {
    TEST(LightingTest, OwnsExtractedWorldPoseAndDoesNotScaleLightDirectionLocally) {
        Scene scene;
        auto parent = scene.create_entity("parent");
        auto& transform = parent.get_component<TransformComponent>();
        transform.translation = {4, 2, 1};
        transform.rotation = {0, 90, 0};
        transform.scale = {2, 3, 4};
        auto light = scene.create_entity("light");
        light.add_component<LightComponent>().type = LightType::Spot;
        light.get_component<LightComponent>().casts_shadow = true;
        auto& child = light.get_component<TransformComponent>();
        child.translation = {0, 0, 2};
        child.scale = {0, -10, 0};
        ASSERT_TRUE(scene.set_parent(light, parent));
        auto extracted = SceneExtractor::extract(scene);
        ASSERT_EQ(extracted.lights.size(), 1);
        const auto expected = transform.to_matrix() * Math::Vec4(child.translation, 1);
        EXPECT_LT(
            Math::length(extracted.lights[0].position - Math::Vec3(expected)), 1e-5f);
        EXPECT_LT(Math::length(Math::normalize(extracted.lights[0].direction)
                               - Math::Vec3(-1, 0, 0)),
            1e-5f);
        light.get_component<LightComponent>().enabled = false;
        EXPECT_TRUE(SceneExtractor::extract(scene).lights.empty());
        EXPECT_EQ(extracted.lights[0].type, LightType::Spot);
        EXPECT_TRUE(extracted.lights[0].casts_shadow);
        AssetRegistry assets;
        SceneResolver resolver(assets);
        RenderView view{.render_size = {16, 16},
            .camera_selection = RenderView::CameraSelection::Override,
            .camera_override = RenderCamera{}};
        const auto submission = resolver.resolve(extracted, view);
        extracted.lights.clear();
        ASSERT_EQ(submission.lights.size(), 1);
        EXPECT_EQ(submission.lights[0].entity_id, light.get_id());
    }

    TEST(LightingTest, PacksDeterministicBoundedValidLightsAndReportsRejectedInputs) {
        std::vector<RenderLight> lights;
        for(unsigned index = 40; index > 0; --index)
            lights.push_back({.entity_id = index,
                .position = {float(index), 0, 0},
                .direction = {0, 0, -5}});
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
        EXPECT_EQ(data.counts, Math::Vec4(32, 8, 3, 0));
        for(const auto& light : data.lights) {
            EXPECT_EQ(light.direction_range, Math::Vec4(0, 0, -1, 0));
            EXPECT_TRUE(Math::is_finite(light.position_type));
        }
        for(auto& light : lights)
            light.type = LightType::Point;
        const auto points = LightingData::prepare(lights);
        EXPECT_EQ(points.lights.front().position_type.x, 1);
        EXPECT_EQ(points.lights.back().position_type.x, 32);
    }

    TEST(LightingTest, RejectsInvalidRangesConesAndEnergyWithoutPoisoningValidLights) {
        RenderLight light{.type = LightType::Spot};
        EXPECT_EQ(LightingData::prepare(std::span(&light, 1)).counts.x, 1);
        light.inner_angle = light.outer_angle;
        EXPECT_EQ(LightingData::prepare(std::span(&light, 1)).counts.z, 1);
        light.type = LightType::Point;
        light.range = -1;
        EXPECT_EQ(LightingData::prepare(std::span(&light, 1)).counts.z, 1);
        light.range = 10;
        light.intensity = std::numeric_limits<float>::infinity();
        EXPECT_EQ(LightingData::prepare(std::span(&light, 1)).counts.z, 1);
        light.intensity = 1;
        light.direction.x = std::numeric_limits<float>::quiet_NaN();
        const auto data = LightingData::prepare(std::span(&light, 1));
        EXPECT_EQ(data.counts.x, 1);
        EXPECT_TRUE(Math::is_finite(data.lights.front().direction_range));
    }

    TEST(LightingTest, ShaderFrameBlockMatchesCpuPacking) {
        const ShaderInterface shader(MATERIAL_LIT_FRAG);
        const auto found = std::ranges::find_if(shader.get_bindings(),
            [](const auto& binding) { return binding.set == 0 && binding.binding == 1; });
        ASSERT_NE(found, shader.get_bindings().end());
        EXPECT_EQ(found->block_size, sizeof(LightingData));
        ASSERT_EQ(found->members.size(), 4);
        EXPECT_EQ(found->members[1].offset, offsetof(LightingData, counts));
        EXPECT_EQ(
            found->members[2].offset, offsetof(LightingData, shadow_view_projection));
        EXPECT_EQ(found->members[3].offset, offsetof(LightingData, shadow_parameters));
    }

    TEST(LightingTest, FitsDirectionalShadowBoundsAndHandlesVerticalDirections) {
        const BoundingBox bounds{{-3, -1, -8}, {7, 4, 2}};
        for(const Math::Vec3 direction : {Math::Vec3(0, 0, -1), Math::Vec3(0, -1, 0),
                Math::Vec3(0, 1, 0), Math::Vec3(1, -2, 3)}) {
            RenderLight light{.direction = direction, .casts_shadow = true};
            auto data = LightingData::prepare(std::span(&light, 1));
            data.prepare_shadow(bounds, 1024);
            ASSERT_EQ(data.shadow_parameters.x, 0);
            EXPECT_FLOAT_EQ(data.shadow_parameters.z, 1.0f / 1024);
            for(int corner = 0; corner < 8; ++corner) {
                auto point = bounds.minimum;
                for(int axis = 0; axis < 3; ++axis)
                    if(corner & (1 << axis))
                        point[axis] = bounds.maximum[axis];
                const auto clip = data.shadow_view_projection * Math::Vec4(point, 1);
                ASSERT_TRUE(Math::is_finite(clip));
                EXPECT_LT(std::abs(clip.x), 1);
                EXPECT_LT(std::abs(clip.y), 1);
                EXPECT_GT(clip.z, 0);
                EXPECT_LT(clip.z, 1);
            }
        }
    }

    TEST(LightingTest, ShadowSelectsOnlyRetainedDirectionalLightAndResetsInvalidFit) {
        std::vector<RenderLight> lights{{.entity_id = 8, .casts_shadow = true},
            {.entity_id = 1, .type = LightType::Point, .casts_shadow = true},
            {.entity_id = 2}, {.entity_id = 4, .intensity = 0, .casts_shadow = true},
            {.entity_id = 6, .casts_shadow = true}};
        auto data = LightingData::prepare(lights);
        const BoundingBox bounds{{-1, -1, -1}, {1, 1, 1}};
        data.prepare_shadow(bounds, 1024);
        EXPECT_EQ(data.shadow_parameters.x, 3);
        data.prepare_shadow(bounds, 0);
        EXPECT_EQ(data.shadow_parameters.x, -1);
        data.prepare_shadow(BoundingBox::from_point({0, 0, 0}), 1024);
        EXPECT_EQ(data.shadow_parameters.x, -1);
        data.prepare_shadow({{2, 0, 0}, {1, 1, 1}}, 1024);
        EXPECT_EQ(data.shadow_parameters.x, -1);
        lights.clear();
        for(uint32_t id = 0; id < 33; ++id)
            lights.push_back({.entity_id = id, .casts_shadow = id == 32});
        data = LightingData::prepare(lights);
        data.prepare_shadow(bounds, 1024);
        EXPECT_EQ(data.shadow_parameters.x, -1);
    }

    TEST(LightingTest, TypedEnumRoundTripsAndUsesExistingPropertyUndoAndClone) {
        auto registry = create_scene_component_registry();
        SceneSerializer serializer(registry);
        Scene scene;
        auto light = scene.create_entity("editable light");
        light.add_component<LightComponent>().casts_shadow = true;
        const auto* descriptor = registry.find_component("light");
        const auto* type = descriptor->find_property("type");
        ASSERT_EQ(type->type, PropertyType::Enum);
        EXPECT_EQ(
            std::get<std::string>(*type->copy_value(descriptor->get_component(light))),
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
        auto clone = serializer.clone(scene);
        EXPECT_TRUE(clone->find_entity(light.get_uuid())
                .get_component<LightComponent>()
                .casts_shadow);
        EXPECT_EQ(
            clone->find_entity(light.get_uuid()).get_component<LightComponent>().type,
            LightType::Spot);
        clone->find_entity(light.get_uuid()).get_component<LightComponent>().intensity =
            100;
        EXPECT_EQ(light.get_component<LightComponent>().intensity, 1);
        auto text = serializer.serialize(scene);
        const auto offset = text.find("type: spot");
        ASSERT_NE(offset, std::string::npos);
        text.replace(offset, std::string("type: spot").size(), "type: invalid");
        EXPECT_THROW(static_cast<void>(serializer.deserialize(text)), std::runtime_error);
        light.get_component<LightComponent>().type = static_cast<LightType>(100);
        EXPECT_FALSE(type->copy_value(descriptor->get_component(light)));
        EXPECT_THROW(static_cast<void>(serializer.serialize(scene)), std::runtime_error);
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

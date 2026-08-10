#include "scene/component_registry.h"
#include "scene/scene.h"

#include <gtest/gtest.h>

namespace {
    const Comet::PropertyDescriptor& require_property(
        const Comet::ComponentDescriptor& component,
        const std::string_view property_id) {
        const Comet::PropertyDescriptor* property =
                component.find_property(property_id);
        EXPECT_NE(property, nullptr);
        return *property;
    }

    TEST(ComponentRegistryTest, RegistersBuiltInEditableComponents) {
        const Comet::ComponentRegistry registry =
                Comet::create_scene_component_registry();

        ASSERT_EQ(registry.components().size(), 3U);
        EXPECT_NE(registry.find_component("transform"), nullptr);
        EXPECT_NE(registry.find_component("mesh_renderer"), nullptr);
        EXPECT_NE(registry.find_component("camera"), nullptr);

        const auto& transform = *registry.find_component("transform");
        EXPECT_EQ(transform.display_name, "Transform");
        EXPECT_TRUE(transform.serializable);
        EXPECT_EQ(require_property(transform, "translation").type,
                  Comet::PropertyType::Vec3);
        EXPECT_EQ(require_property(transform, "rotation").numeric.speed, 1.0f);
        EXPECT_TRUE(require_property(transform, "rotation").editable);
        EXPECT_TRUE(require_property(transform, "rotation").serializable);
        EXPECT_FALSE(require_property(transform, "rotation").transient);

        const auto& mesh_renderer = *registry.find_component("mesh_renderer");
        EXPECT_EQ(require_property(mesh_renderer, "mesh").type,
                  Comet::PropertyType::AssetHandle);

        const auto& camera = *registry.find_component("camera");
        EXPECT_EQ(require_property(camera, "primary").type,
                  Comet::PropertyType::Bool);
        EXPECT_EQ(require_property(camera, "fov").type,
                  Comet::PropertyType::Float);
    }

    TEST(ComponentRegistryTest, AccessesAndNormalizesEntityComponentProperties) {
        Comet::Scene scene;
        Comet::Entity entity = scene.create_entity("Camera");
        entity.add_component<Comet::CameraComponent>();
        const Comet::ComponentRegistry registry =
                Comet::create_scene_component_registry();

        const auto& transform = *registry.find_component("transform");
        ASSERT_TRUE(transform.has_component(entity));
        void* transform_value = transform.get_component(entity);
        ASSERT_NE(transform_value, nullptr);

        const auto& rotation = require_property(transform, "rotation");
        auto& rotation_value = *static_cast<Comet::Math::Vec3*>(
            rotation.get_value(transform_value));
        rotation_value = {0.0f, 725.0f, -540.0f};
        rotation.notify_changed(&rotation_value);
        EXPECT_FLOAT_EQ(rotation_value.x, 0.0f);
        EXPECT_FLOAT_EQ(rotation_value.y, 5.0f);
        EXPECT_FLOAT_EQ(rotation_value.z, -180.0f);

        const auto& camera = *registry.find_component("camera");
        ASSERT_TRUE(camera.has_component(entity));
        const auto& primary = require_property(camera, "primary");
        *static_cast<bool*>(primary.get_value(camera.get_component(entity))) = true;
        EXPECT_TRUE(entity.get_component<Comet::CameraComponent>().primary);
    }

    TEST(ComponentRegistryTest, ReportsMissingOptionalComponents) {
        Comet::Scene scene;
        Comet::Entity entity = scene.create_entity();
        const Comet::ComponentRegistry registry =
                Comet::create_scene_component_registry();

        EXPECT_TRUE(registry.find_component("transform")->has_component(entity));
        EXPECT_FALSE(registry.find_component("camera")->has_component(entity));
        EXPECT_FALSE(registry.find_component("mesh_renderer")->has_component(entity));
        EXPECT_EQ(registry.find_component("camera")->get_component(entity), nullptr);

        const Comet::ComponentDescriptor& camera =
                *registry.find_component("camera");
        EXPECT_TRUE(camera.add_component(entity));
        EXPECT_TRUE(entity.has_component<Comet::CameraComponent>());
        EXPECT_FALSE(camera.add_component(entity));
        EXPECT_TRUE(camera.remove_component(entity));
        EXPECT_FALSE(entity.has_component<Comet::CameraComponent>());
    }

    TEST(ComponentRegistryTest, RejectsDuplicateStableIds) {
        Comet::ComponentRegistry registry;
        auto first = Comet::make_component_descriptor<Comet::TransformComponent>(
            "transform",
            "Transform",
            {Comet::make_property_descriptor(
                "translation",
                "Translation",
                &Comet::TransformComponent::translation)});
        auto duplicate = Comet::make_component_descriptor<Comet::CameraComponent>(
            "transform",
            "Camera",
            {Comet::make_property_descriptor(
                "primary", "Primary", &Comet::CameraComponent::primary)});

        EXPECT_TRUE(registry.register_component(std::move(first)));
        EXPECT_FALSE(registry.register_component(std::move(duplicate)));
        EXPECT_EQ(registry.components().size(), 1U);
    }
}

#include "scene/component_registry.h"
#include "scene/scene.h"

#include <gtest/gtest.h>

namespace {
    const Comet::PropertyDescriptor& require_property(
        const Comet::ComponentDescriptor& component, const std::string_view property_id) {
        const Comet::PropertyDescriptor* property = component.find_property(property_id);
        EXPECT_NE(property, nullptr);
        return *property;
    }

    TEST(ComponentRegistryTest, RegistersBuiltInEditableComponents) {
        const Comet::ComponentRegistry registry =
            Comet::create_scene_component_registry();

        ASSERT_EQ(registry.components().size(), 4U);
        EXPECT_NE(registry.find_component("name"), nullptr);
        EXPECT_NE(registry.find_component("transform"), nullptr);
        EXPECT_NE(registry.find_component("mesh_renderer"), nullptr);
        EXPECT_NE(registry.find_component("camera"), nullptr);

        const auto& transform = *registry.find_component("transform");
        EXPECT_EQ(transform.display_name, "Transform");
        EXPECT_TRUE(transform.serializable);
        EXPECT_EQ(
            require_property(transform, "translation").type, Comet::PropertyType::Vec3);
        EXPECT_EQ(require_property(transform, "rotation").numeric.speed, 1.0f);
        EXPECT_TRUE(require_property(transform, "rotation").editable);
        EXPECT_TRUE(require_property(transform, "rotation").serializable);
        EXPECT_FALSE(require_property(transform, "rotation").asset_type);
        const auto& mesh_renderer = *registry.find_component("mesh_renderer");
        EXPECT_EQ(
            require_property(mesh_renderer, "mesh").asset_type, Comet::AssetType::Mesh);
        EXPECT_EQ(require_property(mesh_renderer, "material").asset_type,
            Comet::AssetType::Material);
        EXPECT_FALSE(require_property(transform, "rotation").transient);

        EXPECT_EQ(require_property(mesh_renderer, "mesh").type,
            Comet::PropertyType::AssetHandle);

        const auto& camera = *registry.find_component("camera");
        EXPECT_EQ(require_property(camera, "primary").type, Comet::PropertyType::Bool);
        EXPECT_EQ(require_property(camera, "fov").type, Comet::PropertyType::Float);
    }

    TEST(ComponentRegistryTest, RejectsAssetTypeMetadataOnNonAssetAndUnknownType) {
        Comet::ComponentRegistry registry;
        auto invalid = Comet::make_component_descriptor<Comet::CameraComponent>("camera",
            "Camera",
            {Comet::make_property_descriptor("fov", "FOV", &Comet::CameraComponent::fov,
                {.asset_type = Comet::AssetType::Mesh})});
        EXPECT_FALSE(registry.register_component(std::move(invalid)));
        auto unknown =
            Comet::make_component_descriptor<Comet::MeshRendererComponent>("mesh", "Mesh",
                {Comet::make_property_descriptor("mesh", "Mesh",
                    &Comet::MeshRendererComponent::mesh,
                    {.asset_type = Comet::AssetType::Unknown})});
        EXPECT_FALSE(registry.register_component(std::move(unknown)));
    }

    TEST(ComponentRegistryTest, CollectsOwnedTypedReferencesIncludingReadOnlyFields) {
        const auto builtins = Comet::create_scene_component_registry();
        auto descriptor = *builtins.find_component("mesh_renderer");
        descriptor.properties[0].editable = false;
        descriptor.properties[0].read_only = true;
        descriptor.properties[0].serializable = false;
        descriptor.properties[0].transient = true;
        Comet::ComponentRegistry registry;
        ASSERT_TRUE(registry.register_component(std::move(descriptor)));
        Comet::Scene scene;
        auto first = scene.create_entity();
        auto second = scene.create_entity();
        first.add_component<Comet::MeshRendererComponent>(
            Comet::AssetHandle(42), Comet::AssetHandle(43));
        second.add_component<Comet::MeshRendererComponent>(
            Comet::AssetHandle(42), Comet::AssetHandle(43));
        const auto references = registry.collect_asset_references(scene);
        EXPECT_EQ(references, (std::vector<Comet::ComponentRegistry::AssetReference>{
                                  {Comet::AssetHandle(42), Comet::AssetType::Mesh},
                                  {Comet::AssetHandle(43), Comet::AssetType::Material}}));
        first.get_component<Comet::MeshRendererComponent>().material =
            Comet::AssetHandle(42);
        EXPECT_EQ(registry.collect_asset_references(scene).size(), 3);
        scene.destroy_entity(first);
        scene.destroy_entity(second);
        EXPECT_TRUE(registry.collect_asset_references(scene).empty());
        EXPECT_EQ(references.size(), 2);
        scene.create_entity().add_component<Comet::MeshRendererComponent>();
        EXPECT_TRUE(registry.collect_asset_references(scene).empty());
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
        auto& rotation_value =
            *static_cast<Comet::Math::Vec3*>(rotation.get_value(transform_value));
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

        const Comet::ComponentDescriptor& camera = *registry.find_component("camera");
        EXPECT_TRUE(camera.add_component(entity));
        EXPECT_TRUE(entity.has_component<Comet::CameraComponent>());
        EXPECT_FALSE(camera.add_component(entity));
        EXPECT_TRUE(camera.remove_component(entity));
        EXPECT_FALSE(entity.has_component<Comet::CameraComponent>());
    }

    TEST(ComponentRegistryTest, NameUsesStringValuesButCannotBeAddedOrRemoved) {
        Comet::Scene scene;
        auto entity = scene.create_entity("Before");
        const auto registry = Comet::create_scene_component_registry();
        const auto& component = *registry.find_component("name");
        const auto& name = require_property(component, "name");
        EXPECT_FALSE(component.serializable);
        EXPECT_FALSE(component.add_component_callback);
        EXPECT_FALSE(component.remove_component_callback);
        EXPECT_FALSE(component.add_component(entity));
        EXPECT_FALSE(component.remove_component(entity));
        EXPECT_EQ(name.type, Comet::PropertyType::String);
        ASSERT_TRUE(
            name.assign_value(component.get_component(entity), std::string("名称")));
        EXPECT_EQ(
            std::get<std::string>(*name.copy_value(component.get_component(entity))),
            "名称");
        EXPECT_FALSE(name.assign_value(component.get_component(entity), 1.0f));
        EXPECT_EQ(entity.get_component<Comet::NameComponent>().name, "名称");
    }

    TEST(ComponentRegistryTest, RestoresSnapshotConstructedOutsideSharedEngine) {
        Comet::Scene scene;
        auto entity = scene.create_entity();
        const auto registry = Comet::create_scene_component_registry();
        const std::any camera = Comet::CameraComponent{.fov = 67};
        ASSERT_TRUE(registry.find_component("camera")->restore_component(entity, camera));
        EXPECT_FLOAT_EQ(entity.get_component<Comet::CameraComponent>().fov, 67);
        const std::any transform =
            Comet::TransformComponent{.translation = Comet::Math::Vec3(9)};
        entity.remove_component<Comet::TransformComponent>();
        ASSERT_TRUE(
            registry.find_component("transform")->restore_component(entity, transform));
        EXPECT_EQ(entity.get_component<Comet::TransformComponent>().translation,
            Comet::Math::Vec3(9));
    }

    TEST(ComponentRegistryTest, ComponentSnapshotIsOwnedAndRejectsInvalidRestore) {
        Comet::Scene scene;
        auto entity = scene.create_entity();
        entity.add_component<Comet::CameraComponent>().fov = 73;
        const auto registry = Comet::create_scene_component_registry();
        const auto& camera = *registry.find_component("camera");
        const auto snapshot = camera.capture_component(entity);
        EXPECT_FALSE(camera.restore_component(entity, snapshot));
        entity.get_component<Comet::CameraComponent>().fov = 91;
        ASSERT_TRUE(camera.remove_component(entity));
        EXPECT_FALSE(camera.capture_component(entity).has_value());
        EXPECT_FALSE(camera.restore_component(entity, std::any(12)));
        EXPECT_FALSE(entity.has_component<Comet::CameraComponent>());
        ASSERT_TRUE(camera.restore_component(entity, snapshot));
        EXPECT_FLOAT_EQ(entity.get_component<Comet::CameraComponent>().fov, 73);
        EXPECT_FALSE(
            registry.find_component("name")->capture_component(entity).has_value());
        scene.destroy_entity(entity);
        EXPECT_FALSE(camera.restore_component(entity, snapshot));
    }

    TEST(ComponentRegistryTest, RejectsDuplicateStableIds) {
        Comet::ComponentRegistry registry;
        auto first = Comet::make_component_descriptor<Comet::TransformComponent>(
            "transform", "Transform",
            {Comet::make_property_descriptor(
                "translation", "Translation", &Comet::TransformComponent::translation)});
        auto duplicate = Comet::make_component_descriptor<Comet::CameraComponent>(
            "transform", "Camera",
            {Comet::make_property_descriptor(
                "primary", "Primary", &Comet::CameraComponent::primary)});

        EXPECT_TRUE(registry.register_component(std::move(first)));
        EXPECT_FALSE(registry.register_component(std::move(duplicate)));
        EXPECT_EQ(registry.components().size(), 1U);
    }
}

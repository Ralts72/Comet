#include <gtest/gtest.h>

#include "scene/component_registry.h"
#include "scene/components.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "../test_utils.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Comet::Tests {
    namespace {
        struct DescriptorTestComponent {
            float persisted = 0.0f;
            float runtime_only = 17.0f;
        };

        struct RuntimeOnlyTestComponent {
            bool enabled = true;
        };

        EntityUuid uuid(const std::string_view value) {
            return EntityUuid::parse(value).value();
        }

        const ComponentRegistry& component_registry() {
            static const ComponentRegistry registry =
                create_scene_component_registry();
            return registry;
        }

        SceneSerializer make_scene_serializer() {
            return SceneSerializer(component_registry());
        }

        class TemporarySceneFile final {
        public:
            TemporarySceneFile() {
                const auto id = std::random_device{}();
                m_path = std::filesystem::temp_directory_path()
                    / ("comet_scene_test_" + std::to_string(id) + ".scene");
            }

            ~TemporarySceneFile() {
                std::error_code error;
                std::filesystem::remove(m_path, error);
            }

            [[nodiscard]] std::string path() const {
                return m_path.string();
            }

        private:
            std::filesystem::path m_path;
        };

        class TemporarySceneDirectory final {
        public:
            TemporarySceneDirectory() {
                const auto id = std::random_device{}();
                m_root = std::filesystem::temp_directory_path()
                    / ("comet_scene_directory_test_" + std::to_string(id));
            }

            ~TemporarySceneDirectory() {
                std::error_code error;
                std::filesystem::remove_all(m_root, error);
            }

            [[nodiscard]] std::string scene_path() const {
                return (m_root / "nested" / "untitled.scene").string();
            }

        private:
            std::filesystem::path m_root;
        };

        void expect_scene_error(const SceneSerializer& serializer,
                                const std::string_view contents,
                                const std::string_view expected_detail) {
            try {
                static_cast<void>(serializer.deserialize(
                    contents, "invalid.scene"));
                FAIL() << "Expected scene deserialization to fail";
            } catch(const std::runtime_error& error) {
                const std::string message = error.what();
                EXPECT_NE(message.find("invalid.scene"), std::string::npos);
                EXPECT_NE(
                    message.find(expected_detail), std::string::npos)
                    << message;
            }
        }

        void expect_scene_error(const std::string_view contents,
                                const std::string_view expected_detail) {
            const SceneSerializer serializer = make_scene_serializer();
            expect_scene_error(serializer, contents, expected_detail);
        }
    }

    TEST(SceneSerializerTest, RoundTripsComponentsAndHierarchy) {
        const EntityUuid root_uuid = uuid(
            "00000000-0000-4000-8000-000000000010");
        const EntityUuid child_uuid = uuid(
            "00000000-0000-4000-8000-000000000020");
        Scene scene;
        Entity root = scene.create_entity_with_uuid(root_uuid, "Root");
        Entity child = scene.create_entity_with_uuid(child_uuid, "Child");
        ASSERT_TRUE(root);
        ASSERT_TRUE(child);

        auto& root_transform = root.get_component<TransformComponent>();
        root_transform.translation = Math::Vec3(3.0f, 4.0f, 5.0f);
        root_transform.rotation = Math::Vec3(10.0f, 20.0f, 30.0f);
        root_transform.scale = Math::Vec3(2.0f);
        auto& child_transform = child.get_component<TransformComponent>();
        child_transform.translation = Math::Vec3(1.0f, 2.0f, 3.0f);
        child_transform.rotation = Math::Vec3(-15.0f, 45.0f, 5.0f);
        child_transform.scale = Math::Vec3(0.5f, 1.5f, 2.0f);
        child.add_component<MeshRendererComponent>(
            AssetHandle(101), AssetHandle(202));
        auto& camera = root.add_component<CameraComponent>();
        camera.primary = true;
        camera.fov = 60.0f;
        camera.near_clip = 0.25f;
        camera.far_clip = 2500.0f;
        ASSERT_TRUE(scene.set_parent(child, root));

        const SceneSerializer serializer = make_scene_serializer();
        const std::string contents = serializer.serialize(scene);

        EXPECT_NE(contents.find("version: 1"), std::string::npos);
        EXPECT_EQ(contents.find("entity_id"), std::string::npos);
        EXPECT_EQ(contents.find("world_matrix"), std::string::npos);

        std::unique_ptr<Scene> loaded = serializer.deserialize(
            contents, "round-trip.scene");
        ASSERT_NE(loaded, nullptr);
        ASSERT_EQ(loaded->entity_count(), 2u);

        Entity loaded_root = loaded->find_entity(root_uuid);
        Entity loaded_child = loaded->find_entity(child_uuid);
        ASSERT_TRUE(loaded_root);
        ASSERT_TRUE(loaded_child);
        EXPECT_EQ(loaded_root.get_component<NameComponent>().name, "Root");
        EXPECT_EQ(loaded_child.get_component<NameComponent>().name, "Child");
        EXPECT_EQ(loaded->get_parent(loaded_child), loaded_root);

        const auto& loaded_root_transform =
            loaded_root.get_component<TransformComponent>();
        const auto& loaded_child_transform =
            loaded_child.get_component<TransformComponent>();
        EXPECT_VEC3_EQ(
            root_transform.translation, loaded_root_transform.translation);
        EXPECT_VEC3_EQ(root_transform.rotation, loaded_root_transform.rotation);
        EXPECT_VEC3_EQ(root_transform.scale, loaded_root_transform.scale);
        EXPECT_VEC3_EQ(
            child_transform.translation, loaded_child_transform.translation);
        EXPECT_VEC3_EQ(
            child_transform.rotation, loaded_child_transform.rotation);
        EXPECT_VEC3_EQ(child_transform.scale, loaded_child_transform.scale);

        const auto& mesh =
            loaded_child.get_component<MeshRendererComponent>();
        EXPECT_EQ(mesh.mesh, AssetHandle(101));
        EXPECT_EQ(mesh.material, AssetHandle(202));
        const auto& loaded_camera =
            loaded_root.get_component<CameraComponent>();
        EXPECT_TRUE(loaded_camera.primary);
        EXPECT_FLOAT_EQ(loaded_camera.fov, 60.0f);
        EXPECT_FLOAT_EQ(loaded_camera.near_clip, 0.25f);
        EXPECT_FLOAT_EQ(loaded_camera.far_clip, 2500.0f);

        const Math::Mat4 expected_world =
            root_transform.to_matrix() * child_transform.to_matrix();
        EXPECT_MAT4_EQ(
            expected_world,
            loaded_child.get_component<WorldTransformComponent>().world_matrix);
        EXPECT_EQ(serializer.serialize(*loaded), contents);
    }

    TEST(SceneSerializerTest, PreservesMissingOptionalComponents) {
        const EntityUuid entity_uuid = uuid(
            "00000000-0000-4000-8000-000000000030");
        Scene scene;
        Entity entity = scene.create_entity_with_uuid(entity_uuid, "Data Only");
        ASSERT_TRUE(entity);
        entity.remove_component<TransformComponent>();

        const SceneSerializer serializer = make_scene_serializer();
        const std::string contents = serializer.serialize(scene);
        std::unique_ptr<Scene> loaded = serializer.deserialize(contents);
        Entity loaded_entity = loaded->find_entity(entity_uuid);

        ASSERT_TRUE(loaded_entity);
        EXPECT_FALSE(loaded_entity.has_component<TransformComponent>());
        EXPECT_FALSE(loaded_entity.has_component<MeshRendererComponent>());
        EXPECT_FALSE(loaded_entity.has_component<CameraComponent>());
        EXPECT_EQ(contents.find("transform:"), std::string::npos);
    }

    TEST(SceneSerializerTest, OrdersEntitiesByUuid) {
        Scene scene;
        const EntityUuid later = uuid(
            "00000000-0000-4000-8000-000000000200");
        const EntityUuid earlier = uuid(
            "00000000-0000-4000-8000-000000000100");
        ASSERT_TRUE(scene.create_entity_with_uuid(later, "Later"));
        ASSERT_TRUE(scene.create_entity_with_uuid(earlier, "Earlier"));

        const std::string contents = make_scene_serializer().serialize(scene);

        EXPECT_LT(
            contents.find(earlier.to_string()),
            contents.find(later.to_string()));
    }

    TEST(SceneSerializerTest, SavesAndLoadsSceneFile) {
        const EntityUuid entity_uuid = uuid(
            "00000000-0000-4000-8000-000000000040");
        Scene scene;
        ASSERT_TRUE(scene.create_entity_with_uuid(entity_uuid, "Saved"));
        const TemporarySceneFile file;
        const SceneSerializer serializer = make_scene_serializer();

        serializer.save(scene, file.path());
        std::unique_ptr<Scene> loaded = serializer.load(file.path());

        ASSERT_NE(loaded, nullptr);
        Entity entity = loaded->find_entity(entity_uuid);
        ASSERT_TRUE(entity);
        EXPECT_EQ(entity.get_component<NameComponent>().name, "Saved");
    }

    TEST(SceneSerializerTest, CreatesMissingParentDirectoriesWhenSaving) {
        Scene scene;
        ASSERT_TRUE(scene.create_entity("Saved"));
        const TemporarySceneDirectory directory;
        const std::string path = directory.scene_path();
        ASSERT_FALSE(std::filesystem::exists(
            std::filesystem::path(path).parent_path()));

        const SceneSerializer serializer = make_scene_serializer();
        serializer.save(scene, path);

        EXPECT_TRUE(std::filesystem::is_regular_file(path));
        const std::unique_ptr<Scene> loaded = serializer.load(path);
        ASSERT_NE(loaded, nullptr);
        EXPECT_EQ(loaded->entity_count(), 1u);
    }

    TEST(SceneSerializerTest, RejectsDuplicateUuid) {
        expect_scene_error(R"(
version: 1
entities:
  - uuid: 00000000-0000-4000-8000-000000000001
    components: {name: First}
  - uuid: 00000000-0000-4000-8000-000000000001
    components: {name: Second}
)", "duplicate UUID");
    }

    TEST(SceneSerializerTest, RejectsMissingParent) {
        expect_scene_error(R"(
version: 1
entities:
  - uuid: 00000000-0000-4000-8000-000000000001
    parent: 00000000-0000-4000-8000-000000000099
    components: {name: Child}
)", "missing parent UUID");
    }

    TEST(SceneSerializerTest, RejectsParentCycle) {
        expect_scene_error(R"(
version: 1
entities:
  - uuid: 00000000-0000-4000-8000-000000000001
    parent: 00000000-0000-4000-8000-000000000002
    components: {name: First}
  - uuid: 00000000-0000-4000-8000-000000000002
    parent: 00000000-0000-4000-8000-000000000001
    components: {name: Second}
)", "forms a cycle");
    }

    TEST(SceneSerializerTest, RejectsUnknownAndMalformedFields) {
        expect_scene_error(R"(
version: 1
entities:
  - uuid: 00000000-0000-4000-8000-000000000001
    components:
      name: Invalid
      transform:
        translation: [1, 2]
        rotation: [0, 0, 0]
        scale: [1, 1, 1]
)", "exactly three numbers");

        expect_scene_error(R"(
version: 1
entities: []
runtime_id: 1
)", "unknown field 'runtime_id'");

        expect_scene_error(R"(
version: 1
entities:
  - uuid: 00000000-0000-4000-8000-000000000001
    components:
      name: Invalid
      mesh_renderer: {mesh: -1, material: 2}
)", "expected a non-negative integer");

        expect_scene_error(R"(
version: 1
entities:
  - uuid: 00000000-0000-4000-8000-000000000001
    components:
      name: First
      name: Second
)", "duplicate field 'name'");
    }

    TEST(SceneSerializerTest, RejectsUnsupportedVersionAndNonFiniteValues) {
        expect_scene_error("version: 2\nentities: []\n", "unsupported version 2");

        Scene scene;
        Entity entity = scene.create_entity("Invalid");
        entity.get_component<TransformComponent>().translation.x =
            std::numeric_limits<float>::infinity();
        EXPECT_THROW(
            static_cast<void>(make_scene_serializer().serialize(scene)),
            std::runtime_error);
    }

    TEST(SceneSerializerTest, UsesDescriptorIdsAndSerializationFlags) {
        ComponentRegistry registry = create_scene_component_registry();
        ASSERT_TRUE(registry.register_component(
            make_component_descriptor<DescriptorTestComponent>(
                "descriptor_component",
                "Descriptor Component",
                {
                    make_property_descriptor(
                        "persisted_value",
                        "Persisted Value",
                        &DescriptorTestComponent::persisted),
                    make_property_descriptor(
                        "runtime_value",
                        "Runtime Value",
                        &DescriptorTestComponent::runtime_only,
                        {.serializable = false})
                })));
        ASSERT_TRUE(registry.register_component(
            make_component_descriptor<RuntimeOnlyTestComponent>(
                "runtime_component",
                "Runtime Component",
                {
                    make_property_descriptor(
                        "enabled",
                        "Enabled",
                        &RuntimeOnlyTestComponent::enabled)
                },
                false)));

        Scene scene;
        const EntityUuid entity_uuid = uuid(
            "00000000-0000-4000-8000-000000000050");
        Entity entity = scene.create_entity_with_uuid(
            entity_uuid, "Descriptor Driven");
        auto& component = entity.add_component<DescriptorTestComponent>();
        component.persisted = 42.0f;
        component.runtime_only = 99.0f;
        entity.add_component<RuntimeOnlyTestComponent>().enabled = false;

        const SceneSerializer serializer(registry);
        const std::string contents = serializer.serialize(scene);
        EXPECT_NE(contents.find("descriptor_component:"), std::string::npos);
        EXPECT_NE(contents.find("persisted_value: 42"), std::string::npos);
        EXPECT_EQ(contents.find("runtime_value"), std::string::npos);
        EXPECT_EQ(contents.find("runtime_component"), std::string::npos);

        const std::unique_ptr<Scene> loaded = serializer.deserialize(contents);
        const Entity loaded_entity = loaded->find_entity(entity_uuid);
        ASSERT_TRUE(loaded_entity);
        ASSERT_TRUE(loaded_entity.has_component<DescriptorTestComponent>());
        const auto& loaded_component =
            loaded_entity.get_component<DescriptorTestComponent>();
        EXPECT_FLOAT_EQ(loaded_component.persisted, 42.0f);
        EXPECT_FLOAT_EQ(loaded_component.runtime_only, 17.0f);
        EXPECT_FALSE(loaded_entity.has_component<RuntimeOnlyTestComponent>());

        expect_scene_error(serializer, R"(
version: 1
entities:
  - uuid: 00000000-0000-4000-8000-000000000050
    components:
      name: Invalid
      descriptor_component:
        persisted_value: 42
        runtime_value: 99
)", "unknown field 'runtime_value'");
    }
}

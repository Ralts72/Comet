#include <gtest/gtest.h>

#include "scene/component_registry.h"
#include "scene/components.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "support/math_assertions.h"
#include "render/scene/scene_extractor.h"
#include "render/scene/scene_resolver.h"
#include "asset/registry.h"
#include "support/temporary_directory.h"

#include <filesystem>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Comet::Tests {
    TEST(ScenePostProcessTest, PersistsClonesAndTravelsThroughBothCameraPaths) {
        Scene scene;
        const PostProcessSettings settings{.exposure = 0.75f,
            .bloom_enabled = true,
            .bloom_strength = 0.5f,
            .bloom_threshold = 2.0f};
        ASSERT_TRUE(scene.set_post_process(settings));
        scene.create_entity("Camera").add_component<CameraComponent>().primary = true;
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
        RenderView view{.render_size = {160, 120}};
        EXPECT_EQ(resolver.resolve(snapshot, view).post_process, settings);
        view.camera_selection = RenderView::CameraSelection::Override;
        view.camera_override = RenderCamera{};
        EXPECT_EQ(resolver.resolve(snapshot, view).post_process, settings);

        auto disabled = settings;
        disabled.bloom_enabled = false;
        ASSERT_TRUE(cloned.value()->set_post_process(disabled));
        EXPECT_FALSE(cloned.value()->get_post_process().uses_bloom());
        EXPECT_EQ(scene.get_post_process(), settings);
        EXPECT_EQ(resolver.resolve(SceneExtractor::extract(*cloned.value()), view).post_process,
            disabled);
        EXPECT_EQ(resolver.resolve(RenderScene{}, view).post_process, PostProcessSettings{});
    }

    TEST(ScenePostProcessTest, DefaultsOffAndRejectsMalformedOrOutOfRangeSettings) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        const auto loaded = serializer.deserialize(R"({"version":2,"entities":[]})");
        ASSERT_TRUE(loaded);
        auto& scene = *loaded.value();
        EXPECT_EQ(scene.get_post_process(), PostProcessSettings{});
        EXPECT_FALSE(scene.get_post_process().uses_bloom());
        for(auto field : {&PostProcessSettings::exposure, &PostProcessSettings::bloom_strength,
                &PostProcessSettings::bloom_threshold}) {
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
                std::string(R"({"version":2,"entities":[],"post_process":)") + object + "}");
            ASSERT_FALSE(result) << object;
            EXPECT_NE(result.error().find("post_process"), std::string::npos);
        }
    }

    TEST(SceneEnvironmentTest, PersistsExtractsAndCollectsReferenceWithoutAnEntity) {
        Scene scene;
        const SceneEnvironment environment{
            AssetHandle(902), true, 2.0f, -90.0f, true, 0.75f, {0.25f, 0.5f, 2.0f}};
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
        RenderView view;
        EXPECT_EQ(resolver.resolve(SceneExtractor::extract(scene), view).environment,
            scene.get_environment());
        view.camera_selection = RenderView::CameraSelection::Override;
        view.camera_override = RenderCamera{};
        view.render_size = {64, 64};
        EXPECT_EQ(resolver.resolve(SceneExtractor::extract(scene), view).environment,
            scene.get_environment());
        const SceneSerializer serializer(registry);
        auto cloned = serializer.clone(scene);
        ASSERT_TRUE(cloned) << cloned.error();
        EXPECT_EQ(cloned.value()->get_environment(), scene.get_environment());
        EXPECT_EQ(cloned.value()->entity_count(), 0u);
        auto hidden = scene.get_environment();
        hidden.background = false;
        ASSERT_TRUE(scene.set_environment(hidden));
        EXPECT_EQ(registry.collect_asset_references(scene).size(), 1u);
    }

    TEST(SceneEnvironmentTest, LegacyScenesDefaultOffAndInvalidEnvironmentIsRejected) {
        const auto registry = create_scene_component_registry();
        const SceneSerializer serializer(registry);
        auto legacy = serializer.deserialize(R"({"version":2,"entities":[]})");
        ASSERT_TRUE(legacy);
        EXPECT_EQ(legacy.value()->get_environment(), SceneEnvironment{});
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

            [[nodiscard]] std::string path() const { return m_path.string(); }

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
        auto& camera = root.add_component<CameraComponent>();
        camera.primary = true;
        camera.fov = 60.0f;
        camera.near_clip = 0.25f;
        camera.far_clip = 2500.0f;
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
        EXPECT_FLOAT_EQ(loaded_camera.fov, 60.0f);
        EXPECT_FLOAT_EQ(loaded_camera.near_clip, 0.25f);
        EXPECT_FLOAT_EQ(loaded_camera.far_clip, 2500.0f);

        const Math::Mat4 expected_world = root_transform.to_matrix() * child_transform.to_matrix();
        EXPECT_MAT4_EQ(
            expected_world, loaded_child.get_component<WorldTransformComponent>().world_matrix);
        const auto serialized_again = serializer.serialize(*loaded);
        ASSERT_TRUE(serialized_again) << serialized_again.error();
        EXPECT_EQ(serialized_again.value(), contents);
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
        const TemporarySceneFile file;
        const SceneSerializer serializer = make_scene_serializer();

        ASSERT_TRUE(serializer.save(scene, file.path()));
        auto loaded_result = serializer.load(file.path());
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
        const TemporarySceneDirectory directory;
        const std::string path = directory.scene_path();
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
        const TemporarySceneFile file;
        ASSERT_TRUE(serializer.save(scene, file.path()));
        const auto loaded = serializer.load(file.path());
        ASSERT_TRUE(loaded) << loaded.error();
        EXPECT_EQ(loaded.value()->entity_count(), scene.entity_count());
        ASSERT_TRUE(scene.set_parent(scene.create_entity("Too deep"), parent));
        EXPECT_FALSE(serializer.save(scene, file.path()));
        const auto preserved = serializer.load(file.path());
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
            "exactly three numbers");

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
        entity.add_component<CameraComponent>().fov = std::numeric_limits<float>::infinity();
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
                    &DescriptorTestComponent::runtime_only, {.serializable = false})})));
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
}

#include <gtest/gtest.h>

#include "asset/registry.h"
#include "render/material/material.h"
#include "render/material/material_programs.h"
#include "scene/material_parameters.h"
#include "render/scene/scene_extractor.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "support/math_assertions.h"

#include <algorithm>

namespace Comet::Tests {
    TEST(SceneExtractorTest, EmptySceneProducesNoRenderItems) {
        Scene scene;

        const RenderScene render_scene = SceneExtractor::extract(scene);

        EXPECT_TRUE(render_scene.render_items.empty());
    }

    TEST(SceneExtractorTest, MaterialOverridesArePerEntityImmutableRenderSnapshots) {
        AssetRegistry assets;
        const AssetHandle material_handle{20};
        ASSERT_TRUE(
            assets.register_asset(material_handle, std::make_shared<Material>("shared", "pbr")));
        MaterialPrograms materials(assets);
        Scene scene;
        auto target = scene.create_entity("Target");
        auto peer = scene.create_entity("Peer");
        target.add_component<MeshRendererComponent>(AssetHandle{10}, material_handle);
        peer.add_component<MeshRendererComponent>(AssetHandle{10}, material_handle);
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(scene));
        const Math::Vec4 color(0.2f, 1, 0.25f, 1);
        ASSERT_TRUE(scene.set_material_vector(target, "base_color", color, materials));
        const auto original = scene.get_material_overrides(target);
        ASSERT_TRUE(original);
        const auto extracted = SceneExtractor::extract(scene);
        ASSERT_EQ(extracted.render_items.size(), 2u);
        const auto target_item =
            std::ranges::find(extracted.render_items, target.get_id(), &RenderItem::entity_id);
        const auto peer_item =
            std::ranges::find(extracted.render_items, peer.get_id(), &RenderItem::entity_id);
        ASSERT_NE(target_item, extracted.render_items.end());
        ASSERT_NE(peer_item, extracted.render_items.end());
        EXPECT_EQ(target_item->material_handle, peer_item->material_handle);
        EXPECT_EQ(target_item->material_overrides, original);
        EXPECT_FALSE(peer_item->material_overrides);

        ASSERT_TRUE(scene.set_material_scalar(target, "roughness", 0.25f, materials));
        const auto next = SceneExtractor::extract(scene);
        const auto updated =
            std::ranges::find(next.render_items, target.get_id(), &RenderItem::entity_id);
        ASSERT_NE(updated, next.render_items.end());
        ASSERT_TRUE(updated->material_overrides);
        EXPECT_NE(updated->material_overrides, original);
        EXPECT_FLOAT_EQ(updated->material_overrides->scalar_properties.at("roughness"), 0.25f);
        EXPECT_TRUE(original->scalar_properties.empty());
        EXPECT_EQ(original->vector_properties.at("base_color"), color);
        EXPECT_EQ(target_item->material_overrides, original);

        ASSERT_TRUE(runtime.stop());
        const auto stopped = SceneExtractor::extract(scene);
        for(const auto& item : stopped.render_items)
            EXPECT_FALSE(item.material_overrides);
        EXPECT_EQ(target_item->material_overrides, original);
        ASSERT_TRUE(runtime.start(scene));
        for(const auto& item : SceneExtractor::extract(scene).render_items)
            EXPECT_FALSE(item.material_overrides);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(SceneExtractorTest, MaterialRebindingAndComponentRemovalDiscardRuntimeOverrides) {
        AssetRegistry assets;
        for(const auto handle : {AssetHandle{20}, AssetHandle{30}})
            ASSERT_TRUE(assets.register_asset(handle, std::make_shared<Material>("shared", "pbr")));
        MaterialPrograms materials(assets);
        Scene scene;
        auto target = scene.create_entity();
        target.add_component<MeshRendererComponent>(AssetHandle{10}, AssetHandle{20});
        SceneRuntime runtime;
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(scene.set_material_scalar(target, "roughness", 0.25f, materials));
        const auto before_rebind = scene.get_material_overrides(target);
        ASSERT_TRUE(before_rebind);
        target.get_component<MeshRendererComponent>().material = AssetHandle{30};
        const auto rebound = SceneExtractor::extract(scene);
        ASSERT_EQ(rebound.render_items.size(), 1u);
        EXPECT_EQ(rebound.render_items.front().material_handle, AssetHandle{30});
        EXPECT_FALSE(rebound.render_items.front().material_overrides);
        ASSERT_TRUE(scene.set_material_scalar(target, "roughness", 0.5f, materials));
        const auto before_removal = scene.get_material_overrides(target);
        ASSERT_TRUE(before_removal);
        EXPECT_EQ(before_removal->material, AssetHandle{30});
        EXPECT_NE(before_removal->instance_id, before_rebind->instance_id);
        target.remove_component<MeshRendererComponent>();
        EXPECT_TRUE(SceneExtractor::extract(scene).render_items.empty());
        target.add_component<MeshRendererComponent>(AssetHandle{10}, AssetHandle{30});
        const auto restored = SceneExtractor::extract(scene);
        ASSERT_EQ(restored.render_items.size(), 1u);
        EXPECT_FALSE(restored.render_items.front().material_overrides);
        EXPECT_FLOAT_EQ(before_rebind->scalar_properties.at("roughness"), 0.25f);
        EXPECT_FLOAT_EQ(before_removal->scalar_properties.at("roughness"), 0.5f);
        ASSERT_TRUE(runtime.stop());
    }

    TEST(SceneExtractorTest, ExtractsOnlyEntitiesWithRequiredComponents) {
        Scene scene;

        Entity first = scene.create_entity("First");
        const auto& first_transform = first.get_component<TransformComponent>();
        EXPECT_TRUE(first.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(1.0f, 2.0f, 3.0f); }));
        EXPECT_TRUE(first.try_edit_transform(
            [&](auto& value) { value.rotation = Math::Vec3(10.0f, 20.0f, 30.0f); }));
        EXPECT_TRUE(first.try_edit_transform(
            [&](auto& value) { value.scale = Math::Vec3(2.0f, 2.0f, 2.0f); }));
        first.add_component<MeshRendererComponent>(AssetHandle(10), AssetHandle(20));

        Entity second = scene.create_entity("Second");
        const auto& second_transform = second.get_component<TransformComponent>();
        EXPECT_TRUE(second.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(-4.0f, 5.0f, 6.0f); }));
        EXPECT_TRUE(second.try_edit_transform(
            [&](auto& value) { value.rotation = Math::Vec3(0.0f, 90.0f, 0.0f); }));
        second.add_component<MeshRendererComponent>(AssetHandle(30), AssetHandle(40));

        scene.create_entity("No MeshRenderer");

        Entity no_transform = scene.create_entity("No Transform");
        no_transform.add_component<MeshRendererComponent>(AssetHandle(50), AssetHandle(60));
        no_transform.remove_component<TransformComponent>();

        const Math::Mat4 expected_first_model = Math::compose_trs(
            first_transform.translation, first_transform.rotation, first_transform.scale);
        const Math::Mat4 expected_second_model = Math::compose_trs(
            second_transform.translation, second_transform.rotation, second_transform.scale);
        const RenderScene render_scene = SceneExtractor::extract(scene);

        ASSERT_EQ(render_scene.render_items.size(), 2u);
        const auto find_item = [&render_scene](const EntityId id) {
            return std::find_if(render_scene.render_items.begin(), render_scene.render_items.end(),
                [id](const RenderItem& item) { return item.entity_id == id; });
        };

        const auto first_item = find_item(first.get_id());
        ASSERT_NE(first_item, render_scene.render_items.end());
        EXPECT_EQ(first_item->mesh_handle, AssetHandle(10));
        EXPECT_EQ(first_item->material_handle, AssetHandle(20));
        EXPECT_TRUE(TestUtils::Mat4Equal(first_item->model_matrix, expected_first_model));

        const auto second_item = find_item(second.get_id());
        ASSERT_NE(second_item, render_scene.render_items.end());
        EXPECT_EQ(second_item->mesh_handle, AssetHandle(30));
        EXPECT_EQ(second_item->material_handle, AssetHandle(40));
        EXPECT_TRUE(TestUtils::Mat4Equal(second_item->model_matrix, expected_second_model));
    }

    TEST(SceneExtractorTest, ExtractsWorldMatrixForChildEntity) {
        Scene scene;
        Entity parent = scene.create_entity("Parent");
        EXPECT_TRUE(parent.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(2.0f, 0.0f, 0.0f); }));

        Entity child = scene.create_entity("Child");
        EXPECT_TRUE(child.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(0.0f, 3.0f, 0.0f); }));
        child.add_component<MeshRendererComponent>(AssetHandle(10), AssetHandle(20));
        ASSERT_TRUE(scene.set_parent(child, parent));

        const Math::Mat4 expected = parent.get_component<TransformComponent>().to_matrix()
                                    * child.get_component<TransformComponent>().to_matrix();
        const RenderScene render_scene = SceneExtractor::extract(scene);

        ASSERT_EQ(render_scene.render_items.size(), 1u);
        EXPECT_EQ(render_scene.render_items.front().entity_id, child.get_id());
        EXPECT_TRUE(TestUtils::Mat4Equal(render_scene.render_items.front().model_matrix, expected));
    }

    TEST(SceneExtractorTest, ExtractsCameraViewWithoutTransformScale) {
        Scene scene;
        Entity camera_entity = scene.create_entity("Main Camera");
        const auto& transform = camera_entity.get_component<TransformComponent>();
        EXPECT_TRUE(camera_entity.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(1.0f, 2.0f, 3.0f); }));
        EXPECT_TRUE(camera_entity.try_edit_transform(
            [&](auto& value) { value.rotation = Math::Vec3(10.0f, 20.0f, 30.0f); }));
        EXPECT_TRUE(camera_entity.try_edit_transform(
            [&](auto& value) { value.scale = Math::Vec3(2.0f, 3.0f, 4.0f); }));
        auto& camera = camera_entity.add_component<CameraComponent>();
        camera.primary = true;
        camera.fov = 60.0f;
        camera.near_clip = 0.2f;
        camera.far_clip = 500.0f;

        Entity missing_transform = scene.create_entity("Missing Transform");
        missing_transform.add_component<CameraComponent>().primary = true;
        missing_transform.remove_component<TransformComponent>();

        Entity camera_parent = scene.create_entity("Camera Parent");
        const auto& parent_transform = camera_parent.get_component<TransformComponent>();
        EXPECT_TRUE(camera_parent.try_edit_transform(
            [&](auto& value) { value.translation = Math::Vec3(5.0f, 0.0f, 0.0f); }));
        EXPECT_TRUE(camera_parent.try_edit_transform(
            [&](auto& value) { value.rotation = Math::Vec3(0.0f, 15.0f, 0.0f); }));
        ASSERT_TRUE(scene.set_parent(camera_entity, camera_parent));

        TransformComponent camera_pose = transform;
        camera_pose.scale = Math::Vec3(1.0f);
        const Math::Mat4 expected_view =
            Math::inverse(parent_transform.to_matrix() * camera_pose.to_matrix());

        const RenderScene render_scene = SceneExtractor::extract(scene);

        ASSERT_EQ(render_scene.cameras.size(), 1u);
        const RenderCamera& extracted = render_scene.cameras.front();
        EXPECT_EQ(extracted.entity_id, camera_entity.get_id());
        EXPECT_TRUE(extracted.primary);
        EXPECT_TRUE(TestUtils::Mat4Equal(extracted.view_matrix, expected_view));
        EXPECT_EQ(extracted.projection, RenderCamera::Projection::Perspective);
        EXPECT_FLOAT_EQ(extracted.fov_degrees, 60.0f);
        EXPECT_FLOAT_EQ(extracted.near_clip, 0.2f);
        EXPECT_FLOAT_EQ(extracted.far_clip, 500.0f);
    }

    TEST(SceneExtractorTest, ExtractsSceneCameraOrthographicProjection) {
        Scene scene;
        auto camera_entity = scene.create_entity("Camera");
        auto& camera = camera_entity.add_component<CameraComponent>();
        camera.primary = true;
        camera.projection = CameraComponent::Projection::Orthographic;
        camera.orthographic_height = 8.0f;
        camera.near_clip = 0.2f;
        camera.far_clip = 50.0f;

        const auto snapshot = SceneExtractor::extract(scene);
        ASSERT_EQ(snapshot.cameras.size(), 1u);
        const auto& extracted = snapshot.cameras.front();
        EXPECT_EQ(extracted.projection, RenderCamera::Projection::Orthographic);
        EXPECT_FLOAT_EQ(extracted.orthographic_height, 8.0f);
        const auto matrix = extracted.projection_matrix(2.0f);
        ASSERT_TRUE(matrix);
        EXPECT_TRUE(
            TestUtils::Mat4Equal(*matrix, Math::ortho(-8.0f, 8.0f, -4.0f, 4.0f, 0.2f, 50.0f)));
    }
}

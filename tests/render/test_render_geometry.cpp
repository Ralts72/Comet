#include <gtest/gtest.h>

#include "asset/data/mesh_data.h"
#include "asset/registry.h"
#include "render/material/material.h"
#include "render/scene/render_geometry.h"
#include "render/scene/scene_extractor.h"
#include "render/scene/scene_resolver.h"
#include "render/resource/mesh.h"
#include "render/resource/render_resources.h"
#include "scene/scene.h"
#include "support/engine_fixture.h"

#include <algorithm>
#include <limits>

namespace Comet::Tests {
    using RenderGeometryTest = EngineTest;

    TEST_F(RenderGeometryTest, PreparesSharedWorldBoundsAndIgnoresInvalidGeometry) {
        auto uploaded = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-1, -1, 0}}, {{1, -1, 0}}, {{0, 1, 1}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(uploaded);
        const auto mesh = uploaded.value();
        RenderSubmission submission{
            .render_items = {
                {.model_matrix = Math::compose_trs({3, 0, 0}, {}, {-2, 1, 1}), .mesh = mesh},
                {.model_matrix = Math::translate(Math::Mat4(1), {-3, 0, 0}), .mesh = mesh},
                {.mesh = mesh}, {}}};
        auto& items = submission.render_items;
        items[2].model_matrix[0][0] = std::numeric_limits<float>::quiet_NaN();
        RenderGeometry geometry;
        geometry.prepare(submission);
        ASSERT_EQ(geometry.get_items().size(), items.size());
        EXPECT_EQ(geometry.get_items()[0].source, &items[0]);
        ASSERT_TRUE(geometry.get_items()[0].world_bounds);
        EXPECT_EQ(geometry.get_items()[0].world_bounds->minimum, Math::Vec3(1, -1, 0));
        EXPECT_EQ(geometry.get_items()[0].world_bounds->maximum, Math::Vec3(5, 1, 1));
        EXPECT_FALSE(geometry.get_items()[2].world_bounds);
        EXPECT_FALSE(geometry.get_items()[3].world_bounds);
        ASSERT_TRUE(geometry.get_scene_bounds());
        EXPECT_EQ(geometry.get_scene_bounds()->minimum, Math::Vec3(-4, -1, 0));
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(5, 1, 1));
    }

    TEST_F(RenderGeometryTest, RefreshesMovementMeshReplacementAndRemoval) {
        auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 1}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        RenderSubmission submission{.render_items = {{.mesh = mesh.value()}}};
        auto& items = submission.render_items;
        RenderGeometry geometry;
        geometry.prepare(submission);
        ASSERT_TRUE(geometry.get_scene_bounds());
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(1));

        items[0].model_matrix = Math::translate(Math::Mat4(1), {5, 0, 0});
        geometry.prepare(submission);
        EXPECT_EQ(geometry.get_scene_bounds()->minimum, Math::Vec3(5, 0, 0));
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(6, 1, 1));
        auto replacement = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-2, -2, -2}}, {{2, -2, -2}}, {{0, 2, 2}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(replacement);
        items[0].mesh = replacement.value();
        geometry.prepare(submission);
        EXPECT_EQ(geometry.get_scene_bounds()->minimum, Math::Vec3(3, -2, -2));
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(7, 2, 2));

        items.push_back(
            {.model_matrix = Math::translate(Math::Mat4(1), {-20, 0, 0}), .mesh = mesh.value()});
        geometry.prepare(submission);
        EXPECT_EQ(geometry.get_scene_bounds()->minimum, Math::Vec3(-20, -2, -2));
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(7, 2, 2));
        std::reverse(items.begin(), items.end());
        geometry.prepare(submission);
        EXPECT_EQ(geometry.get_scene_bounds()->minimum, Math::Vec3(-20, -2, -2));
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(7, 2, 2));
        items.erase(items.begin());
        geometry.prepare(submission);
        EXPECT_EQ(geometry.get_scene_bounds()->minimum, Math::Vec3(3, -2, -2));
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(7, 2, 2));

        geometry.clear();
        EXPECT_TRUE(geometry.get_items().empty());
        EXPECT_FALSE(geometry.get_scene_bounds());
        items.clear();
        geometry.prepare(submission);
        EXPECT_TRUE(geometry.get_items().empty());
        EXPECT_FALSE(geometry.get_scene_bounds());
    }

    TEST_F(RenderGeometryTest,
        IncrementalPreparationMatchesFullResolutionAcrossEditsAndSkippedBatches) {
        auto first = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-1, -1, -1}}, {{1, -1, -1}}, {{0, 1, 1}}}, .indices = {0, 1, 2}});
        auto second = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-2, 0, 0}}, {{2, 0, 0}}, {{0, 2, 2}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);
        AssetRegistry assets;
        ASSERT_TRUE(assets.register_asset(AssetHandle{10}, first.value()));
        ASSERT_TRUE(assets.register_asset(AssetHandle{11}, second.value()));
        for(const auto handle : {AssetHandle{20}, AssetHandle{21}, AssetHandle{22}})
            ASSERT_TRUE(assets.register_asset(handle, std::make_shared<Material>("item", "pbr")));
        Scene scene;
        const auto create = [&](float x) {
            auto entity = scene.create_entity();
            entity.add_component<MeshRendererComponent>(AssetHandle{10}, AssetHandle{20});
            entity.set_transform({.translation = {x, 0, 0}});
            return entity;
        };
        std::vector<Entity> entities;
        for(int index = 0; index < 32; ++index)
            entities.push_back(create(float(index - 16)));
        SceneExtractor extractor;
        SceneResolver resolver(assets);
        SceneResolver reference_resolver(assets);
        RenderScene snapshot;
        RenderSubmission submission;
        RenderGeometry geometry;
        const RenderView view{.render_size = {160, 120},
            .camera_selection = RenderView::CameraSelection::Override,
            .camera_override = RenderCamera{}};
        const auto check = [&] {
            resolver.resolve(snapshot, view, submission);
            geometry.prepare(submission);
            const auto fresh = reference_resolver.resolve(snapshot, view);
            RenderGeometry reference;
            reference.prepare(fresh);
            ASSERT_EQ(submission.render_items.size(), fresh.render_items.size());
            ASSERT_EQ(geometry.get_items().size(), fresh.render_items.size());
            for(std::size_t index = 0; index < fresh.render_items.size(); ++index) {
                const auto& actual = submission.render_items[index];
                const auto& expected = fresh.render_items[index];
                EXPECT_EQ(actual.entity_id, expected.entity_id);
                EXPECT_EQ(actual.model_matrix, expected.model_matrix);
                EXPECT_EQ(actual.mesh, expected.mesh);
                EXPECT_EQ(actual.mesh_revision, expected.mesh_revision);
                EXPECT_EQ(actual.material.resource, expected.material.resource);
                EXPECT_EQ(actual.material.asset_revision, expected.material.asset_revision);
                EXPECT_EQ(geometry.get_items()[index].source, &actual);
                const auto& bounds = geometry.get_items()[index].world_bounds;
                const auto& expected_bounds = reference.get_items()[index].world_bounds;
                ASSERT_EQ(bounds.has_value(), expected_bounds.has_value());
                if(bounds) {
                    EXPECT_EQ(bounds->minimum, expected_bounds->minimum);
                    EXPECT_EQ(bounds->maximum, expected_bounds->maximum);
                }
            }
            ASSERT_EQ(
                geometry.get_scene_bounds().has_value(), reference.get_scene_bounds().has_value());
            if(reference.get_scene_bounds()) {
                EXPECT_EQ(
                    geometry.get_scene_bounds()->minimum, reference.get_scene_bounds()->minimum);
                EXPECT_EQ(
                    geometry.get_scene_bounds()->maximum, reference.get_scene_bounds()->maximum);
            }
            geometry.clear();
            EXPECT_TRUE(geometry.get_items().empty());
            EXPECT_FALSE(geometry.get_scene_bounds());
        };
        extractor.update(scene, snapshot);
        check();
        const auto frozen = submission;
        const auto frozen_matrix = frozen.render_items.front().model_matrix;
        for(int step = 0; step < 80; ++step) {
            SCOPED_TRACE(step);
            switch(step % 8) {
                case 0:
                    entities[16].edit_component<MeshRendererComponent>([&](auto& item) {
                        item.material = AssetHandle{step % 16 == 0 ? 99u : 20u};
                    });
                    break;
                case 1:
                    entities[17].edit_transform([](auto& value) { value.rotation.y += 15; });
                    break;
                case 2:
                    entities.front().edit_transform(
                        [&](auto& value) { value.translation.x = step % 16 == 2 ? -30 : -5; });
                    break;
                case 3:
                    entities[18].edit_component<MeshRendererComponent>([&](auto& item) {
                        item.mesh = AssetHandle{step % 16 == 3 ? 11u : 10u};
                        item.material = AssetHandle{step % 16 == 3 ? 21u : 20u};
                    });
                    break;
                case 4:
                    if(step % 16 == 4) {
                        ASSERT_TRUE(assets.replace_asset(
                            AssetHandle{10}, step % 32 == 4 ? second.value() : first.value()));
                    } else {
                        ASSERT_TRUE(assets.replace_asset(
                            AssetHandle{22}, std::make_shared<Material>("unrelated", "pbr")));
                    }
                    resolver.refresh_assets(submission);
                    break;
                case 5:
                    for(auto entity : entities)
                        entity.edit_transform([](auto& value) { value.rotation.y += 1; });
                    break;
                case 6:
                    // 解析与几何准备分别漏过一批，也必须得到完整的新结果。
                    entities[19].edit_transform([](auto& value) { value.translation.z += 1; });
                    extractor.update(scene, snapshot);
                    resolver.resolve(snapshot, view, submission);
                    entities[20].edit_transform([](auto& value) { value.translation.z += 1; });
                    extractor.update(scene, snapshot);
                    resolver.resolve(snapshot, view, submission);
                    check();
                    entities[21].edit_transform([](auto& value) { value.translation.z += 1; });
                    extractor.update(scene, snapshot);
                    entities[22].edit_transform([](auto& value) { value.translation.z += 1; });
                    break;
                case 7:
                    scene.destroy_entity(entities.back());
                    entities.back() = create(float(step));
                    break;
            }
            extractor.update(scene, snapshot);
            if(step % 17 == 0) {
                SceneExtractor other;
                RenderScene other_snapshot;
                other.update(scene, other_snapshot);
                resolver.resolve(other_snapshot, view, submission);
                geometry.prepare(submission);
                geometry.clear();
            }
            check();
            check();
        }
        Scene replacement;
        auto entity = replacement.create_entity();
        entity.add_component<MeshRendererComponent>(AssetHandle{11}, AssetHandle{21});
        entity.set_transform({.translation = {100, 0, 0}});
        extractor.update(replacement, snapshot);
        check();
        extractor.update(scene, snapshot);
        check();
        EXPECT_EQ(frozen.render_items.front().model_matrix, frozen_matrix);
    }

    TEST_F(RenderGeometryTest, StructuralEditsMatchFreshSnapshotsAndGeometry) {
        AssetRegistry assets;
        auto first_mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-1, -1, -1}}, {{1, -1, -1}}, {{0, 1, 1}}}, .indices = {0, 1, 2}});
        auto second_mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-2, 0, 0}}, {{2, 0, 0}}, {{0, 2, 2}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(first_mesh);
        ASSERT_TRUE(second_mesh);
        ASSERT_TRUE(assets.register_asset(AssetHandle{10}, first_mesh.value()));
        ASSERT_TRUE(assets.register_asset(AssetHandle{11}, second_mesh.value()));
        for(uint64_t index = 0; index < 4; ++index)
            ASSERT_TRUE(assets.register_asset(
                AssetHandle{20 + index}, std::make_shared<Material>("shared", "pbr")));
        Scene scene;
        std::vector<Entity> entities;
        const auto create = [&](uint64_t index) {
            auto entity = scene.create_entity();
            entity.add_component<MeshRendererComponent>(
                AssetHandle{10 + index % 2}, AssetHandle{20 + index % 4});
            entity.set_transform({.translation = {float(index), 0, 0}});
            return entity;
        };
        for(uint64_t index = 0; index < 32; ++index)
            entities.push_back(create(index));
        RenderScene snapshot;
        RenderSubmission submission;
        SceneResolver resolver(assets);
        RenderGeometry geometry;
        const RenderView view{.render_size = {160, 120},
            .camera_selection = RenderView::CameraSelection::Override,
            .camera_override = RenderCamera{}};
        SceneExtractor::extract(scene, snapshot);
        const auto frozen = snapshot;
        for(uint64_t step = 0; step < 64; ++step) {
            SCOPED_TRACE(step);
            const auto index = step % entities.size();
            scene.destroy_entity(entities[index]);
            entities[index] = create(step + 32);
            const auto filtered = (index + 1) % entities.size();
            entities[filtered].remove_component<TransformComponent>();
            entities[(index + 2) % entities.size()].edit_component<MeshRendererComponent>(
                [&](auto& mesh) {
                    mesh.mesh = AssetHandle{10 + step % 2};
                    mesh.material = AssetHandle{20 + step % 4};
                    if(step % 5 == 0)
                        mesh.material = AssetHandle{99};
                });
            SceneExtractor::extract(scene, snapshot);
            const auto fresh = SceneExtractor::extract(scene);
            ASSERT_EQ(snapshot.render_items.size(), fresh.render_items.size());
            for(std::size_t item = 0; item < fresh.render_items.size(); ++item) {
                EXPECT_EQ(
                    snapshot.render_items[item].entity_id, fresh.render_items[item].entity_id);
                EXPECT_EQ(snapshot.render_items[item].model_matrix,
                    fresh.render_items[item].model_matrix);
                EXPECT_EQ(
                    snapshot.render_items[item].mesh_handle, fresh.render_items[item].mesh_handle);
                EXPECT_EQ(snapshot.render_items[item].material_handle,
                    fresh.render_items[item].material_handle);
            }
            if(step % 2 == 0)
                std::ranges::reverse(snapshot.render_items);
            else
                std::rotate(snapshot.render_items.begin(), snapshot.render_items.begin() + 1,
                    snapshot.render_items.end());
            resolver.resolve(snapshot, view, submission);
            const auto expected = resolver.resolve(snapshot, view);
            ASSERT_EQ(submission.render_items.size(), expected.render_items.size());
            geometry.prepare(submission);
            RenderGeometry reference;
            reference.prepare(expected);
            for(std::size_t item = 0; item < expected.render_items.size(); ++item) {
                EXPECT_EQ(
                    submission.render_items[item].entity_id, expected.render_items[item].entity_id);
                EXPECT_EQ(submission.render_items[item].model_matrix,
                    expected.render_items[item].model_matrix);
                EXPECT_EQ(submission.render_items[item].mesh, expected.render_items[item].mesh);
                EXPECT_EQ(submission.render_items[item].material.resource,
                    expected.render_items[item].material.resource);
                ASSERT_TRUE(geometry.get_items()[item].world_bounds);
                ASSERT_TRUE(reference.get_items()[item].world_bounds);
                EXPECT_EQ(geometry.get_items()[item].world_bounds->minimum,
                    reference.get_items()[item].world_bounds->minimum);
                EXPECT_EQ(geometry.get_items()[item].world_bounds->maximum,
                    reference.get_items()[item].world_bounds->maximum);
            }
            ASSERT_TRUE(geometry.get_scene_bounds());
            ASSERT_TRUE(reference.get_scene_bounds());
            EXPECT_EQ(geometry.get_scene_bounds()->minimum, reference.get_scene_bounds()->minimum);
            EXPECT_EQ(geometry.get_scene_bounds()->maximum, reference.get_scene_bounds()->maximum);
            geometry.clear();
            entities[filtered].add_component<TransformComponent>();
        }
        ASSERT_EQ(frozen.render_items.size(), 32u);
        for(const auto& item : frozen.render_items)
            EXPECT_EQ(item.model_matrix[3].x, float(item.entity_id - 1));
    }

    TEST_F(RenderGeometryTest, SingleInsertionRemovalAndLateMovementRefreshSceneBounds) {
        auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-1, -1, -1}}, {{1, -1, -1}}, {{0, 1, 1}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        AssetRegistry assets;
        const AssetHandle handle{10};
        ASSERT_TRUE(assets.register_asset(handle, mesh.value()));
        const auto make_item = [&](EntityId id, float x) {
            return ResolvedRenderItem{.entity_id = id,
                .transform_revision = 1,
                .model_matrix = Math::translate(Math::Mat4(1), {x, 0, 0}),
                .mesh_handle = handle,
                .mesh_revision = assets.get_revision(handle),
                .mesh = mesh.value()};
        };
        RenderSubmission submission{.scene_lifetime = 1,
            .render_items = {make_item(1, -10), make_item(2, 0), make_item(3, 10)}};
        auto& items = submission.render_items;
        RenderGeometry geometry;
        const auto verify = [&](float minimum, float maximum) {
            geometry.prepare(submission);
            ASSERT_EQ(geometry.get_items().size(), items.size());
            for(std::size_t index = 0; index < items.size(); ++index) {
                const auto expected =
                    transform_box(items[index].mesh->get_local_bounds(), items[index].model_matrix);
                ASSERT_TRUE(expected);
                ASSERT_TRUE(geometry.get_items()[index].world_bounds);
                EXPECT_EQ(geometry.get_items()[index].source, &items[index]);
                EXPECT_EQ(geometry.get_items()[index].world_bounds->minimum, expected->minimum);
                EXPECT_EQ(geometry.get_items()[index].world_bounds->maximum, expected->maximum);
            }
            ASSERT_TRUE(geometry.get_scene_bounds());
            EXPECT_EQ(geometry.get_scene_bounds()->minimum.x, minimum);
            EXPECT_EQ(geometry.get_scene_bounds()->maximum.x, maximum);
            geometry.clear();
            EXPECT_TRUE(geometry.get_items().empty());
            EXPECT_FALSE(geometry.get_scene_bounds());
        };
        verify(-11, 11);
        verify(-11, 11);
        items.erase(items.begin());
        verify(-1, 11);
        items.insert(items.begin(), make_item(4, -30));
        verify(-31, 11);
        items.erase(items.begin() + 1);
        verify(-31, 11);
        items.push_back(make_item(5, 40));
        verify(-31, 41);
        items.back().model_matrix = Math::translate(Math::Mat4(1), {-100, 0, 0});
        ++items.back().transform_revision;
        verify(-101, 11);
        items.back().model_matrix[0][0] = std::numeric_limits<float>::quiet_NaN();
        ++items.back().transform_revision;
        geometry.prepare(submission);
        EXPECT_FALSE(geometry.get_items().back().world_bounds);
        ASSERT_TRUE(geometry.get_scene_bounds());
        EXPECT_EQ(geometry.get_scene_bounds()->minimum.x, -31);
        items.clear();
        geometry.prepare(submission);
        EXPECT_TRUE(geometry.get_items().empty());
        EXPECT_FALSE(geometry.get_scene_bounds());
    }

    TEST_F(RenderGeometryTest, VersionedSubmissionsRefreshMovementPublicationOrderAndSceneSwitch) {
        AssetRegistry assets;
        auto original = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{0, 0, 0}}, {{1, 0, 0}}, {{0, 1, 1}}}, .indices = {0, 1, 2}});
        auto larger = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-2, -2, -2}}, {{2, -2, -2}}, {{0, 2, 2}}}, .indices = {0, 1, 2}});
        ASSERT_TRUE(original);
        ASSERT_TRUE(larger);
        const AssetHandle mesh{10}, material{20};
        ASSERT_TRUE(assets.register_asset(mesh, original.value()));
        auto first_material = std::make_shared<Material>("first", "pbr");
        ASSERT_TRUE(assets.register_asset(material, first_material));
        Scene scene;
        auto parent = scene.create_entity();
        auto child = scene.create_entity();
        parent.add_component<MeshRendererComponent>(mesh, material);
        child.add_component<MeshRendererComponent>(mesh, material);
        child.set_transform({.translation = {10, 0, 0}});
        ASSERT_TRUE(scene.set_parent(child, parent));
        SceneResolver resolver(assets);
        RenderScene extracted;
        RenderSubmission submission;
        RenderGeometry geometry;
        const RenderView view{.render_size = {160, 120},
            .camera_selection = RenderView::CameraSelection::Override,
            .camera_override = RenderCamera{}};
        const auto resolve = [&] {
            resolver.resolve(extracted, view, submission);
            geometry.prepare(submission);
            ASSERT_EQ(geometry.get_items().size(), submission.render_items.size());
            std::optional<BoundingBox> reference;
            for(std::size_t index = 0; index < submission.render_items.size(); ++index) {
                const auto& item = submission.render_items[index];
                const auto bounds = transform_box(item.mesh->get_local_bounds(), item.model_matrix);
                ASSERT_TRUE(bounds);
                ASSERT_TRUE(geometry.get_items()[index].world_bounds);
                EXPECT_EQ(geometry.get_items()[index].source, &item);
                EXPECT_EQ(geometry.get_items()[index].world_bounds->minimum, bounds->minimum);
                EXPECT_EQ(geometry.get_items()[index].world_bounds->maximum, bounds->maximum);
                if(reference)
                    reference->include(*bounds);
                else
                    reference = bounds;
            }
            ASSERT_EQ(geometry.get_scene_bounds().has_value(), reference.has_value());
            if(reference) {
                EXPECT_EQ(geometry.get_scene_bounds()->minimum, reference->minimum);
                EXPECT_EQ(geometry.get_scene_bounds()->maximum, reference->maximum);
            }
        };
        SceneExtractor::extract(scene, extracted);
        resolve();
        const auto frozen = submission;
        for(const auto& item : frozen.render_items)
            EXPECT_EQ(item.mesh_revision, assets.get_revision(mesh));
        geometry.clear();
        EXPECT_TRUE(geometry.get_items().empty());
        resolve();
        parent.set_transform({.translation = {5, 0, 0}});
        SceneExtractor::extract(scene, extracted);
        resolve();
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(16, 1, 1));
        std::ranges::reverse(extracted.render_items);
        resolve();
        auto next_material = std::make_shared<Material>("next", "pbr");
        ASSERT_TRUE(assets.replace_asset(material, next_material));
        resolve();
        for(const auto& item : submission.render_items) {
            EXPECT_EQ(item.mesh_revision, frozen.render_items.front().mesh_revision);
            EXPECT_EQ(item.material.resource, next_material);
        }
        ASSERT_TRUE(assets.replace_asset(mesh, larger.value()));
        resolve();
        for(const auto& item : submission.render_items) {
            EXPECT_NE(item.mesh_revision, frozen.render_items.front().mesh_revision);
            EXPECT_EQ(item.mesh, larger.value());
            EXPECT_EQ(item.material.resource, next_material);
        }
        EXPECT_EQ(frozen.render_items.front().mesh, original.value());
        EXPECT_EQ(frozen.render_items.front().material.resource, first_material);
        scene.destroy_entity(child);
        SceneExtractor::extract(scene, extracted);
        resolve();
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(7, 2, 2));
        Scene next_scene;
        auto replacement = next_scene.create_entity();
        replacement.add_component<MeshRendererComponent>(mesh, material);
        replacement.set_transform({.translation = {-5, 0, 0}});
        EXPECT_EQ(replacement.get_id(), parent.get_id());
        SceneExtractor::extract(next_scene, extracted);
        resolve();
        EXPECT_EQ(geometry.get_scene_bounds()->minimum, Math::Vec3(-7, -2, -2));
        ASSERT_TRUE(assets.unregister_asset(mesh));
        resolve();
        EXPECT_TRUE(submission.render_items.empty());
        ASSERT_TRUE(assets.register_asset(mesh, original.value()));
        resolve();
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(-4, 1, 1));
        assets.clear();
        resolve();
        EXPECT_TRUE(submission.render_items.empty());
    }
}

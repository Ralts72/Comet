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
        geometry.clear();
        EXPECT_TRUE(geometry.get_items().empty());
        resolve();
        parent.set_transform({.translation = {5, 0, 0}});
        SceneExtractor::extract(scene, extracted);
        resolve();
        EXPECT_EQ(geometry.get_scene_bounds()->maximum, Math::Vec3(16, 1, 1));
        std::ranges::reverse(extracted.render_items);
        resolve();
        ASSERT_TRUE(assets.replace_asset(mesh, larger.value()));
        auto next_material = std::make_shared<Material>("next", "pbr");
        ASSERT_TRUE(assets.replace_asset(material, next_material));
        resolve();
        for(const auto& item : submission.render_items) {
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

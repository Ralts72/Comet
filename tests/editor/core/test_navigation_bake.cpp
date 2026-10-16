#include "assets/navigation_bake.h"
#include "assets/editor_assets.h"
#include "asset/data/navigation_data.h"
#include "gameplay/navigation_mesh.h"
#include "scene/command_history.h"
#include "scene/scene.h"
#include "scene/scene_serializer.h"
#include "support/blocked_worker.h"
#include "support/render_resource_factory.h"
#include "asset/registry.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    class NavigationBakeTest: public testing::Test {
    protected:
        Comet::Tests::TemporaryDirectory directory;
        Comet::AssetRegistry registry;
        Comet::Tests::FakeRenderResourceFactory factory;
        Comet::TaskScheduler scheduler{1};
        EditorAssets assets{Comet::ProjectPaths(directory.path()), registry, factory, scheduler};
        Comet::ComponentRegistry components = Comet::create_scene_component_registry();
        Comet::Scene scene;
        CommandHistory history;
        PropertyEditTransaction edit{history, components};
        NavigationBake bake{assets, scheduler, components};
        Comet::EntityUuid floor, region;
        void SetUp() override {
            std::filesystem::create_directories(assets.database().paths().assets());
            ASSERT_TRUE(assets.refresh().succeeded());
            history.bind_scene(&scene);
            auto ground = scene.create_entity("ground");
            ground.edit_transform([](auto& value) { value.translation.y = -0.5f; });
            ground.add_component<Comet::RigidBodyComponent>(
                Comet::RigidBodyComponent{.motion = Comet::BodyMotion::Static});
            ground.add_component<Comet::ColliderComponent>(
                Comet::ColliderComponent{.half_extents = {10, 0.5f, 10}});
            floor = ground.get_uuid();
            auto area = scene.create_entity("region");
            area.add_component<Comet::NavigationRegionComponent>(
                Comet::NavigationRegionComponent{.half_extents = {10, 4, 10}});
            region = area.get_uuid();
        }
        void finish_worker() {
            auto task = scheduler.try_submit([] {});
            ASSERT_TRUE(task);
            task->get();
        }
        Comet::AssetHandle product() {
            return scene.find_entity(region).get_component<Comet::NavigationRegionComponent>().data;
        }
    };

    TEST_F(NavigationBakeTest, PublishesCpuProductAndReferenceUndoReusesUnchangedInputs) {
        const auto before = Comet::SceneSerializer(components).serialize(scene);
        ASSERT_TRUE(bake.start(scene, region, history));
        finish_worker();
        const auto completed = bake.update(scene, history, edit);
        ASSERT_TRUE(completed) << completed.error();
        ASSERT_TRUE(completed.value());
        const auto handle = product();
        ASSERT_TRUE(handle);
        const auto* record = assets.database().find(handle);
        ASSERT_TRUE(record);
        ASSERT_EQ(record->type, Comet::AssetType::Navigation);
        const auto path = assets.database().paths().assets() / record->path;
        EXPECT_EQ(path.extension(), ".nav");
        const auto data = Comet::NavigationData::load(path);
        ASSERT_TRUE(data) << data.error();
        Comet::Gameplay::NavigationMesh mesh;
        ASSERT_TRUE(mesh.load(data.value()));
        EXPECT_EQ(mesh.find_path({-5, 0, 0}, {5, 0, 0}).value().status,
            Comet::Gameplay::NavigationMesh::Status::Complete);
        ASSERT_TRUE(assets.load_reference(
            handle, Comet::AssetType::Navigation, assets.database().get_revision(handle)));
        EXPECT_TRUE(registry.contains<Comet::NavigationData>(handle));
        EXPECT_EQ(factory.mesh_creation_count(), 0u);
        const auto after = Comet::SceneSerializer(components).serialize(scene);
        ASSERT_TRUE(history.undo());
        EXPECT_FALSE(product());
        EXPECT_EQ(Comet::SceneSerializer(components).serialize(scene).value(), before.value());
        EXPECT_TRUE(std::filesystem::exists(path));
        ASSERT_TRUE(history.redo());
        EXPECT_EQ(product(), handle);
        EXPECT_EQ(Comet::SceneSerializer(components).serialize(scene).value(), after.value());
        const auto asset_count = assets.database().size();
        const auto cursor = history.undo_size();
        ASSERT_TRUE(bake.start(scene, region, history));
        finish_worker();
        ASSERT_TRUE(bake.update(scene, history, edit).value());
        EXPECT_EQ(product(), handle);
        EXPECT_EQ(assets.database().size(), asset_count);
        EXPECT_EQ(history.undo_size(), cursor);
        ASSERT_TRUE(bake.last_report());
        EXPECT_GT(bake.last_report()->triangles, 0u);
        EXPECT_GT(bake.last_report()->product_bytes, 0u);
    }

    TEST_F(NavigationBakeTest, RejectsChangedGeometryAndDiscardsCancelledOrReboundWork) {
        Comet::Tests::BlockedWorker blocked(scheduler);
        ASSERT_TRUE(bake.start(scene, region, history));
        ASSERT_TRUE(edit.apply({floor, "transform", "translation"}, Comet::Math::Vec3(0, -1, 0)));
        blocked.release();
        finish_worker();
        const auto changed = bake.update(scene, history, edit);
        EXPECT_FALSE(changed);
        EXPECT_FALSE(product());
        EXPECT_TRUE(assets.database().get_assets().empty());

        Comet::Tests::BlockedWorker cancelled(scheduler);
        ASSERT_TRUE(bake.start(scene, region, history));
        bake.cancel();
        EXPECT_FALSE(bake.busy());
        cancelled.release();
        finish_worker();
        EXPECT_FALSE(bake.update(scene, history, edit).value());
        EXPECT_FALSE(product());

        ASSERT_TRUE(bake.start(scene, region, history));
        history.bind_scene(nullptr);
        finish_worker();
        EXPECT_FALSE(bake.update(scene, history, edit).value());
        EXPECT_FALSE(product());
        EXPECT_TRUE(assets.database().get_assets().empty());
    }
}

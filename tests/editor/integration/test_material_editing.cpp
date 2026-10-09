#include "assets/editor_assets.h"
#include "assets/material_editing.h"
#include "assets/material_edit_session.h"
#include "asset/registry.h"
#include "asset/serialization/material_serializer.h"
#include "core/project_paths.h"
#include "core/task_scheduler.h"
#include "render/material/material.h"
#include "render/resource/render_resources.h"
#include "support/engine_fixture.h"
#include "support/temporary_directory.h"

namespace CometEditor::Tests {
    using MaterialPublicationTest = Comet::Tests::EngineTest;

    class MaterialGestureTest: public Comet::Tests::EngineTest {
    protected:
        Comet::Tests::TemporaryDirectory directory;
        Comet::ProjectPaths paths{directory.path()};
        std::unique_ptr<EditorAssets> assets;
        std::unique_ptr<MaterialEditSession> editing;
        Comet::MaterialData initial;
        Comet::AssetHandle handle;
        Comet::AssetRevision revision = 0;
        std::shared_ptr<Comet::Material> original;

        void SetUp() override {
            Comet::Tests::EngineTest::SetUp();
            ASSERT_TRUE(engine);
            std::filesystem::create_directories(paths.assets());
            assets = std::make_unique<EditorAssets>(paths, engine->get_asset_registry(),
                engine->get_render_resources(), engine->get_task_scheduler());
            initial = make_material_data(*Comet::MaterialLayout::find_builtin("pbr"));
            ASSERT_TRUE(assets->create_material("test.mat", initial).succeeded());
            handle = assets->database().find("test.mat")->handle;
            revision = assets->database().get_revision(handle);
            ASSERT_TRUE(assets->load_reference(handle, Comet::AssetType::Material, revision));
            original = engine->get_asset_registry().resolve<Comet::Material>(handle);
            editing = std::make_unique<MaterialEditSession>(*assets, engine->get_renderer());
        }

        void TearDown() override {
            if(editing)
                static_cast<void>(editing->cancel());
            editing.reset();
            assets.reset();
            original.reset();
            Comet::Tests::EngineTest::TearDown();
        }

        AssetEdit change(float roughness, AssetEdit::Action action = AssetEdit::Action::Preview) {
            auto data = initial;
            data.scalar_properties["roughness"] = roughness;
            return {handle, revision, MaterialEdit{initial, std::move(data)}, action};
        }

        Comet::MaterialData saved() {
            return Comet::MaterialSerializer{}.load(paths.assets() / "test.mat").value();
        }
    };

    TEST_F(MaterialGestureTest, PreviewsWithoutWritingThenSavesOnceAndReplaysMaterialHistory) {
        const auto timestamp = std::filesystem::last_write_time(paths.assets() / "test.mat");
        ASSERT_TRUE(editing->process(change(0.7f)));
        ASSERT_TRUE(editing->process(change(0.8f)));
        EXPECT_EQ(saved(), initial);
        EXPECT_EQ(std::filesystem::last_write_time(paths.assets() / "test.mat"), timestamp);
        EXPECT_EQ(assets->database().get_revision(handle), revision);
        EXPECT_FALSE(editing->can_undo(handle));
        auto preview = engine->get_asset_registry().resolve<Comet::Material>(handle);
        EXPECT_EQ(preview->get_scalar_property("roughness"), 0.8f);
        ASSERT_TRUE(editing->commit());
        EXPECT_EQ(saved().scalar_properties.at("roughness"), 0.8f);
        EXPECT_EQ(engine->get_asset_registry().resolve<Comet::Material>(handle), preview);
        const auto committed_revision = assets->database().get_revision(handle);
        ASSERT_TRUE(editing->commit());
        EXPECT_EQ(assets->database().get_revision(handle), committed_revision);
        ASSERT_TRUE(editing->undo(handle));
        EXPECT_EQ(saved(), initial);
        EXPECT_FALSE(editing->can_undo(handle));
        ASSERT_TRUE(editing->redo(handle));
        EXPECT_EQ(saved().scalar_properties.at("roughness"), 0.8f);
        EXPECT_FALSE(editing->can_redo(handle));
    }

    TEST_F(
        MaterialGestureTest, CancelAndReturningToOriginalValueWriteNothingAndRestoreSameVersion) {
        ASSERT_TRUE(editing->process(change(0.7f)));
        ASSERT_TRUE(editing->cancel());
        EXPECT_EQ(engine->get_asset_registry().resolve<Comet::Material>(handle), original);
        ASSERT_TRUE(editing->process(change(0.8f)));
        ASSERT_TRUE(editing->process(
            {handle, revision, MaterialEdit{initial, initial}, AssetEdit::Action::Commit}));
        ASSERT_TRUE(editing->process(
            {handle, revision, MaterialEdit{initial, initial}, AssetEdit::Action::Commit}));
        EXPECT_EQ(engine->get_asset_registry().resolve<Comet::Material>(handle), original);
        EXPECT_EQ(saved(), initial);
        EXPECT_EQ(assets->database().get_revision(handle), revision);
        EXPECT_FALSE(editing->active());
        EXPECT_FALSE(editing->can_undo(handle));
    }

    TEST_F(MaterialGestureTest, FailedSaveKeepsPreviewAndDraftForRetryWithoutPreparingAgain) {
        ASSERT_TRUE(editing->process(change(0.7f)));
        const auto preview = engine->get_asset_registry().resolve<Comet::Material>(handle);
        const auto path = paths.assets() / "test.mat";
        const auto backup = paths.assets() / "saved.mat";
        std::filesystem::rename(path, backup);
        std::filesystem::create_directory(path);
        EXPECT_FALSE(editing->commit());
        EXPECT_TRUE(editing->active());
        EXPECT_FALSE(editing->can_undo(handle));
        EXPECT_EQ(engine->get_asset_registry().resolve<Comet::Material>(handle), preview);
        std::filesystem::remove(path);
        std::filesystem::rename(backup, path);
        EXPECT_EQ(saved(), initial);
        ASSERT_TRUE(editing->commit());
        EXPECT_EQ(engine->get_asset_registry().resolve<Comet::Material>(handle), preview);
        EXPECT_EQ(saved().scalar_properties.at("roughness"), 0.7f);
        EXPECT_TRUE(editing->can_undo(handle));
        std::filesystem::rename(path, backup);
        std::filesystem::create_directory(path);
        EXPECT_FALSE(editing->undo(handle));
        EXPECT_TRUE(editing->can_undo(handle));
        EXPECT_FALSE(editing->can_redo(handle));
        EXPECT_EQ(engine->get_asset_registry().resolve<Comet::Material>(handle), preview);
        std::filesystem::remove(path);
        std::filesystem::rename(backup, path);
        ASSERT_TRUE(editing->undo(handle));
        EXPECT_EQ(saved(), initial);
    }

    TEST_F(
        MaterialGestureTest, FailedGpuPreparationAndStaleGestureNeverReplaceNewPublishedVersion) {
        ASSERT_TRUE(editing->process(change(0.7f)));
        const auto preview = engine->get_asset_registry().resolve<Comet::Material>(handle);
        auto invalid = change(0.8f);
        std::get<MaterialEdit>(invalid.value).after.template_name = "unsupported";
        EXPECT_FALSE(editing->process(invalid));
        EXPECT_EQ(engine->get_asset_registry().resolve<Comet::Material>(handle), preview);
        EXPECT_EQ(saved(), initial);
        ASSERT_TRUE(apply_material_edit(*assets, engine->get_renderer(), change(0.9f)));
        const auto replacement = engine->get_asset_registry().resolve<Comet::Material>(handle);
        EXPECT_FALSE(editing->commit());
        EXPECT_FALSE(editing->cancel());
        EXPECT_EQ(engine->get_asset_registry().resolve<Comet::Material>(handle), replacement);
        EXPECT_EQ(saved().scalar_properties.at("roughness"), 0.9f);
    }

    TEST_F(MaterialGestureTest, EachMaterialHasIndependentUndoAndRedo) {
        ASSERT_TRUE(editing->process(change(0.7f, AssetEdit::Action::Commit)));
        ASSERT_TRUE(editing->undo(handle));
        ASSERT_TRUE(assets->create_material("second.mat", initial).succeeded());
        const auto second = assets->database().find("second.mat")->handle;
        auto edit = change(0.8f, AssetEdit::Action::Commit);
        edit.handle = second;
        edit.revision = assets->database().get_revision(second);
        ASSERT_TRUE(editing->process(edit));
        EXPECT_TRUE(editing->can_redo(handle));
        EXPECT_TRUE(editing->can_undo(second));
        ASSERT_TRUE(editing->redo(handle));
        ASSERT_TRUE(editing->undo(second));
        EXPECT_EQ(saved().scalar_properties.at("roughness"), 0.7f);
        EXPECT_EQ(Comet::MaterialSerializer{}.load(paths.assets() / "second.mat").value(), initial);
    }

    TEST_F(MaterialPublicationTest, PreparesGpuBeforePublishingAndPreservesStateOnFailure) {
        Comet::Tests::TemporaryDirectory directory;
        Comet::ProjectPaths paths(directory.path());
        std::filesystem::create_directories(paths.assets());
        auto& registry = engine->get_asset_registry();
        auto& renderer = engine->get_renderer();
        EditorAssets assets(
            paths, registry, engine->get_render_resources(), engine->get_task_scheduler());
        const auto initial = make_material_data(*Comet::MaterialLayout::find_builtin("pbr"));
        ASSERT_TRUE(assets.create_material("test.mat", initial).succeeded());
        const auto handle = assets.database().find("test.mat")->handle;
        ASSERT_TRUE(assets.load_reference(
            handle, Comet::AssetType::Material, assets.database().get_revision(handle)));
        const auto original = registry.resolve<Comet::Material>(handle);
        const auto revision = assets.database().get_revision(handle);
        const auto path = paths.assets() / "test.mat";
        auto data = initial;
        data.template_name = "unsupported";
        EXPECT_FALSE(
            apply_material_edit(assets, renderer, {handle, revision, MaterialEdit{initial, data}}));
        EXPECT_EQ(registry.resolve<Comet::Material>(handle), original);
        EXPECT_EQ(assets.database().get_revision(handle), revision);
        EXPECT_EQ(Comet::MaterialSerializer{}.load(path).value(), initial);

        data = initial;
        data.scalar_properties["roughness"] = 0.7f;
        const AssetEdit edit{handle, revision, MaterialEdit{initial, data}};
        EXPECT_FALSE(assets.apply_texture_edit(edit));
        const auto backup = paths.assets() / "saved.mat";
        std::filesystem::rename(path, backup);
        std::filesystem::create_directory(path);
        EXPECT_FALSE(apply_material_edit(assets, renderer, edit));
        EXPECT_EQ(registry.resolve<Comet::Material>(handle), original);
        EXPECT_EQ(assets.database().get_revision(handle), revision);
        std::filesystem::remove(path);
        std::filesystem::rename(backup, path);
        EXPECT_EQ(Comet::MaterialSerializer{}.load(path).value(), initial);

        ASSERT_TRUE(apply_material_edit(assets, renderer, edit));
        const auto published = registry.resolve<Comet::Material>(handle);
        ASSERT_NE(published, original);
        EXPECT_EQ(published->get_scalar_property("roughness"), 0.7f);
        EXPECT_EQ(Comet::MaterialSerializer{}.load(path).value(), data);
        EXPECT_FALSE(apply_material_edit(assets, renderer, edit));
        EXPECT_EQ(registry.resolve<Comet::Material>(handle), published);
        EXPECT_EQ(Comet::MaterialSerializer{}.load(path).value(), data);
    }
}

#include "assets/editor_assets.h"
#include "assets/material_editing.h"
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

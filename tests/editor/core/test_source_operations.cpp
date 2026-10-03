#include "assets/source_operations.h"
#include "support/asset_manager_fixture.h"

namespace Comet::Tests {
    namespace SourceOperations = CometEditor::AssetSourceOperations;

    TEST(AssetSourceOperationsTest, NewScriptRejectsSourceOnlyModuleNamesWithoutWritingFiles) {
        const TemporaryProject project;
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());

        for(const auto* name : {"shared.module.lua", "other.MODULE.LUA"}) {
            const auto report = SourceOperations::create_script(database, name);

            EXPECT_FALSE(report.succeeded());
            EXPECT_FALSE(report.snapshot_updated);
            EXPECT_TRUE(has_issue_containing(report, "source-only Lua module"));
            EXPECT_EQ(database.size(), 0u);
            EXPECT_FALSE(std::filesystem::exists(project.paths().assets() / name));
            EXPECT_FALSE(std::filesystem::exists(metadata_path(project.paths().assets() / name)));
        }
        EXPECT_TRUE(SourceOperations::create_script(database, "actor.lua").succeeded());
        ASSERT_NE(database.find("actor.lua"), nullptr);
        EXPECT_EQ(database.find("actor.lua")->type, AssetType::Script);
    }

    TEST(AssetSourceOperationsTest, CreatesLuaModuleAsSourceOnlyWithoutMetadataOrAssetIdentity) {
        const TemporaryProject project;
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());
        std::filesystem::create_directories(project.paths().assets() / "scripts/tools");

        const auto report = SourceOperations::create_script(
            database, "scripts/tools/math.module.lua", SourceOperations::ScriptKind::Module);

        ASSERT_TRUE(report.succeeded());
        EXPECT_TRUE(report.snapshot_updated);
        EXPECT_EQ(report.generated_metadata, 0u);
        EXPECT_TRUE(report.added_assets.empty());
        EXPECT_EQ(database.size(), 0u);
        EXPECT_EQ(database.find("scripts/tools/math.module.lua"), nullptr);
        const auto path = project.paths().assets() / "scripts/tools/math.module.lua";
        const auto source = read_text_file(path);
        ASSERT_TRUE(source);
        EXPECT_EQ(source.value(), "local module = {}\n\nreturn module\n");
        EXPECT_FALSE(std::filesystem::exists(metadata_path(path)));
        EXPECT_FALSE(Script::load(path));

        AssetDatabase reopened(project.paths());
        ASSERT_TRUE(reopened.scan().succeeded());
        EXPECT_EQ(reopened.size(), 0u);
        EXPECT_FALSE(std::filesystem::exists(metadata_path(path)));
    }

    TEST(AssetSourceOperationsTest, LuaModuleCreationRejectsUnrequireablePathsBeforeWriting) {
        const TemporaryProject project;
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());
        std::filesystem::create_directory(project.paths().assets() / "scripts");
        std::filesystem::create_directory(project.paths().assets() / "bad directory");
        std::vector<std::filesystem::path> invalid{"", "plain.lua", "upper.MODULE.LUA",
            "a.b.module.lua", "9number.module.lua", "bad-name.module.lua",
            "bad directory/shared.module.lua", "../escape.module.lua",
            "scripts/../escape.module.lua", "scripts\\shared.module.lua",
            "missing/shared.module.lua", std::string(257, 'a') + ".module.lua",
            project.paths().assets() / "absolute.module.lua"};
        invalid.emplace_back(std::string("nul\0name.module.lua", 19));
        for(const auto& path : invalid) {
            const auto report = SourceOperations::create_script(
                database, path, SourceOperations::ScriptKind::Module);

            EXPECT_FALSE(report.succeeded()) << path;
            EXPECT_FALSE(report.snapshot_updated) << path;
            EXPECT_EQ(database.size(), 0u);
        }
        for(const auto& entry :
            std::filesystem::recursive_directory_iterator(project.paths().assets()))
            EXPECT_TRUE(entry.is_directory()) << entry.path();
        EXPECT_FALSE(std::filesystem::exists(project.paths().root() / "escape.module.lua"));
    }

    TEST(AssetSourceOperationsTest,
        LuaModuleCreationRejectsSourceMetadataAndDanglingSymlinkConflicts) {
        const TemporaryProject project;
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());
        const auto source = project.paths().assets() / "occupied.module.lua";
        const auto meta = project.paths().assets() / "reserved.module.lua.meta";
        ASSERT_TRUE(write_text_file_atomic(source, "return {keep = true}"));
        ASSERT_TRUE(write_text_file_atomic(meta, "reserved metadata"));
        for(const auto* path : {"occupied.module.lua", "reserved.module.lua"}) {
            const auto report = SourceOperations::create_script(
                database, path, SourceOperations::ScriptKind::Module);
            EXPECT_FALSE(report.succeeded());
            EXPECT_FALSE(report.snapshot_updated);
            EXPECT_TRUE(has_issue_containing(report, "not overwritten"));
        }
        EXPECT_EQ(read_text_file(source).value(), "return {keep = true}");
        EXPECT_EQ(read_text_file(meta).value(), "reserved metadata");
        EXPECT_FALSE(std::filesystem::exists(metadata_path(source)));
        EXPECT_FALSE(std::filesystem::exists(project.paths().assets() / "reserved.module.lua"));

        const auto missing = project.paths().assets() / "missing.module.lua";
        const auto link = project.paths().assets() / "link.module.lua";
        std::error_code error;
        std::filesystem::create_symlink(missing, link, error);
        if(error)
            GTEST_SKIP() << "Symlinks unavailable: " << error.message();
        EXPECT_FALSE(SourceOperations::create_script(
            database, "link.module.lua", SourceOperations::ScriptKind::Module)
                .succeeded());
        EXPECT_TRUE(std::filesystem::is_symlink(link));
        EXPECT_FALSE(std::filesystem::exists(missing));
        EXPECT_FALSE(std::filesystem::exists(metadata_path(link)));
    }

    TEST(AssetSourceOperationsTest, LuaModuleCreationRejectsInternalAndExternalDirectoryAliases) {
        const TemporaryProject project;
        const TemporaryDirectory outside;
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());
        const auto real = project.paths().assets() / "real";
        std::filesystem::create_directory(real);
        std::error_code error;
        std::filesystem::create_directory_symlink(real, project.paths().assets() / "alias", error);
        if(error)
            GTEST_SKIP() << "Symlinks unavailable: " << error.message();
        std::filesystem::create_directory_symlink(
            outside.path(), project.paths().assets() / "external", error);
        ASSERT_FALSE(error) << error.message();

        const auto internal = SourceOperations::create_script(
            database, "alias/shared.module.lua", SourceOperations::ScriptKind::Module);
        EXPECT_FALSE(internal.succeeded());
        EXPECT_TRUE(has_issue_containing(internal, "symlink aliases"));
        EXPECT_FALSE(SourceOperations::create_script(
            database, "external/shared.module.lua", SourceOperations::ScriptKind::Module)
                .succeeded());
        EXPECT_TRUE(std::filesystem::is_empty(real));
        EXPECT_TRUE(std::filesystem::is_empty(outside.path()));
        EXPECT_EQ(database.size(), 0u);
    }

    TEST(AssetSourceOperationsTest, TextCreationRollsBackAllKindsWhenCandidateScanFails) {
        const TemporaryProject project;
        AssetDatabase database(project.paths());
        ASSERT_TRUE(SourceOperations::create_script(database, "keep.lua").succeeded());
        const auto handle = database.find("keep.lua")->handle;
        const auto revision = database.get_revision(handle);
        const auto generation = database.generation();
        const auto broken = project.paths().assets() / "broken.mat";
        ASSERT_TRUE(write_text_file_atomic(broken, "{}"));
        ASSERT_TRUE(write_text_file_atomic(metadata_path(broken), "invalid metadata"));

        const MaterialData material{.template_name = "unlit_color", .texture_properties = {}};
        const std::array reports{SourceOperations::create_script(database, "component.lua"),
            SourceOperations::create_script(
                database, "shared.module.lua", SourceOperations::ScriptKind::Module),
            SourceOperations::create_material(database, "material.mat", material)};
        for(const auto& report : reports) {
            EXPECT_FALSE(report.succeeded());
            EXPECT_FALSE(report.snapshot_updated);
            EXPECT_TRUE(has_issue_containing(report, "creation rolled back"));
        }
        for(const auto* name : {"component.lua", "shared.module.lua", "material.mat"}) {
            EXPECT_FALSE(std::filesystem::exists(project.paths().assets() / name));
            EXPECT_FALSE(std::filesystem::exists(metadata_path(project.paths().assets() / name)));
            EXPECT_EQ(database.find(name), nullptr);
        }
        EXPECT_EQ(database.generation(), generation);
        EXPECT_EQ(database.size(), 1u);
        EXPECT_TRUE(database.is_current(handle, revision));
        EXPECT_TRUE(std::filesystem::is_empty(project.paths().cache() / "script-create"));
        EXPECT_TRUE(std::filesystem::is_empty(project.paths().cache() / "module-create"));
        EXPECT_TRUE(std::filesystem::is_empty(project.paths().cache() / "material-create"));

        ASSERT_TRUE(std::filesystem::remove(broken));
        ASSERT_TRUE(std::filesystem::remove(metadata_path(broken)));
        EXPECT_TRUE(SourceOperations::create_script(database, "component.lua").succeeded());
        EXPECT_TRUE(SourceOperations::create_script(
            database, "shared.module.lua", SourceOperations::ScriptKind::Module)
                .succeeded());
        EXPECT_TRUE(
            SourceOperations::create_material(database, "material.mat", material).succeeded());
        EXPECT_EQ(database.size(), 3u);
        EXPECT_TRUE(database.is_current(handle, revision));
        EXPECT_FALSE(std::filesystem::exists(project.paths().assets() / "shared.module.lua.meta"));
    }

    TEST(AssetSourceOperationsTest, ScriptMoveRejectsSourceOnlyModuleDestination) {
        const TemporaryProject project;
        AssetDatabase database(project.paths());
        ASSERT_TRUE(SourceOperations::create_script(database, "actor.lua").succeeded());
        ASSERT_NE(database.find("actor.lua"), nullptr);
        const auto handle = database.find("actor.lua")->handle;

        const auto report = SourceOperations::move(database, handle, "actor.module.lua");

        EXPECT_FALSE(report.succeeded());
        EXPECT_FALSE(report.snapshot_updated);
        EXPECT_TRUE(has_issue_containing(report, "source-only Lua modules"));
        EXPECT_EQ(database.find(handle)->path, "actor.lua");
        EXPECT_TRUE(std::filesystem::is_regular_file(project.paths().assets() / "actor.lua"));
        EXPECT_TRUE(std::filesystem::is_regular_file(project.paths().assets() / "actor.lua.meta"));
        EXPECT_FALSE(std::filesystem::exists(project.paths().assets() / "actor.module.lua"));
        EXPECT_FALSE(std::filesystem::exists(project.paths().assets() / "actor.module.lua.meta"));
    }

    TEST(AssetSourceOperationsTest, RejectsMoveWhenDestinationAlreadyExists) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const auto source = project.add_material(handle, "move_test");
        const auto target = project.paths().assets() / "occupied.mat";
        ASSERT_TRUE(MaterialSerializer{}.save(
            {.template_name = "occupied", .texture_properties = {}}, target));
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().snapshot_updated);

        const auto report = SourceOperations::move(database, handle, "occupied.mat");

        EXPECT_FALSE(report.snapshot_updated);
        EXPECT_TRUE(has_issue_containing(report, "destination already exists"));
        EXPECT_TRUE(std::filesystem::is_regular_file(source));
        EXPECT_TRUE(std::filesystem::is_regular_file(metadata_path(source)));
        ASSERT_NE(database.find(handle), nullptr);
        EXPECT_EQ(database.find(handle)->path, "materials/test.mat");
    }

    TEST(AssetSourceOperationsTest, RejectsMoveOutsideAssetRoot) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const auto source = project.add_material(handle, "move_test");
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().snapshot_updated);

        const auto report = SourceOperations::move(database, handle, "../outside.mat");

        EXPECT_FALSE(report.snapshot_updated);
        EXPECT_TRUE(has_issue_containing(report, "project-relative file path inside assets"));
        EXPECT_TRUE(std::filesystem::is_regular_file(source));
        EXPECT_TRUE(std::filesystem::is_regular_file(metadata_path(source)));
        EXPECT_FALSE(std::filesystem::exists(project.paths().root() / "outside.mat"));
    }

    TEST(AssetSourceOperationsTest, RollsBackMoveWhenScanFindsIdentityConflict) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const auto source = project.add_material(handle, "move_test");
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());

        const auto duplicate = project.paths().assets() / "duplicate.mat";
        ASSERT_TRUE(MaterialSerializer{}.save(
            {.template_name = "duplicate", .texture_properties = {}}, duplicate));
        ASSERT_TRUE(MetadataSerializer{}.save(
            {.handle = handle, .type = AssetType::Material}, metadata_path(duplicate)));

        const auto report = SourceOperations::move(database, handle, "renamed/moved.mat");

        EXPECT_FALSE(report.snapshot_updated);
        EXPECT_TRUE(has_issue_containing(report, "duplicate guid 42"));
        EXPECT_TRUE(has_issue_containing(report, "move was rolled back"));
        EXPECT_TRUE(std::filesystem::is_regular_file(source));
        EXPECT_TRUE(std::filesystem::is_regular_file(metadata_path(source)));
        EXPECT_FALSE(std::filesystem::exists(project.paths().assets() / "renamed/moved.mat"));
        EXPECT_FALSE(std::filesystem::exists(project.paths().assets() / "renamed"));
        ASSERT_NE(database.find(handle), nullptr);
        EXPECT_EQ(database.find(handle)->path, "materials/test.mat");

        ASSERT_TRUE(std::filesystem::remove(duplicate));
        ASSERT_TRUE(std::filesystem::remove(metadata_path(duplicate)));
        const auto retried = SourceOperations::move(database, handle, "renamed/moved.mat");
        ASSERT_TRUE(retried.succeeded());
        ASSERT_TRUE(retried.snapshot_updated);
        ASSERT_NE(database.find(handle), nullptr);
        EXPECT_EQ(database.find(handle)->path, "renamed/moved.mat");
        EXPECT_FALSE(std::filesystem::exists(source));
        EXPECT_FALSE(std::filesystem::exists(metadata_path(source)));
        EXPECT_TRUE(
            std::filesystem::is_regular_file(project.paths().assets() / "renamed/moved.mat"));
        EXPECT_TRUE(
            std::filesystem::is_regular_file(project.paths().assets() / "renamed/moved.mat.meta"));
    }
}

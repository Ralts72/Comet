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

    TEST(AssetSourceOperationsTest, LuaModuleRenamePreservesContentsWithoutCreatingAssetIdentity) {
        const TemporaryProject project;
        const auto root = project.paths().assets();
        std::filesystem::create_directory(root / "scripts");
        const auto source = root / "scripts/shared.module.lua";
        const auto target = root / "scripts/renamed.module.lua";
        ASSERT_TRUE(write_text_file_atomic(source, "return {}"));
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());
        constexpr std::string_view contents = "-- changed since scan\nreturn {score = 42}\n";
        ASSERT_TRUE(write_text_file_atomic(source, contents));

        const auto report = SourceOperations::rename_module(
            database, "scripts/shared.module.lua", "scripts/renamed.module.lua");

        ASSERT_TRUE(report.succeeded());
        EXPECT_TRUE(report.snapshot_updated);
        EXPECT_EQ(report.generated_metadata, 0u);
        EXPECT_TRUE(report.added_assets.empty());
        EXPECT_TRUE(report.removed_assets.empty());
        EXPECT_TRUE(report.modified_assets.empty());
        EXPECT_EQ(database.size(), 0u);
        EXPECT_EQ(database.find("scripts/shared.module.lua"), nullptr);
        EXPECT_EQ(database.find("scripts/renamed.module.lua"), nullptr);
        EXPECT_FALSE(std::filesystem::exists(source));
        ASSERT_TRUE(std::filesystem::is_regular_file(target));
        EXPECT_EQ(read_text_file(target).value(), contents);
        EXPECT_FALSE(std::filesystem::exists(metadata_path(source)));
        EXPECT_FALSE(std::filesystem::exists(metadata_path(target)));

        AssetDatabase reopened(project.paths());
        ASSERT_TRUE(reopened.scan().succeeded());
        EXPECT_EQ(reopened.size(), 0u);
        EXPECT_FALSE(std::filesystem::exists(metadata_path(target)));
    }

    TEST(AssetSourceOperationsTest, LuaModuleRenameRejectsInvalidPathsAndNonFileSources) {
        const TemporaryProject project;
        const auto root = project.paths().assets();
        std::filesystem::create_directory(root / "scripts");
        std::filesystem::create_directory(root / "other");
        std::filesystem::create_directory(root / "scripts/folder.module.lua");
        const auto source = root / "scripts/shared.module.lua";
        ASSERT_TRUE(write_text_file_atomic(source, "return {keep = true}"));
        ASSERT_TRUE(write_text_file_atomic(root / "scripts/bad-name.module.lua", "return {}"));
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());
        const auto generation = database.generation();
        using Paths = std::pair<std::filesystem::path, std::filesystem::path>;
        const std::vector<Paths> invalid{{"scripts/shared.module.lua", "scripts/shared.module.lua"},
            {"scripts/shared.module.lua", "other/renamed.module.lua"},
            {"scripts/shared.module.lua", "scripts/plain.lua"},
            {"scripts/shared.module.lua", "scripts/upper.MODULE.LUA"},
            {"scripts/shared.module.lua", "scripts/bad.name.module.lua"},
            {"scripts/shared.module.lua", "scripts/../escape.module.lua"},
            {"scripts/shared.module.lua", root / "scripts/absolute.module.lua"},
            {"scripts/bad-name.module.lua", "scripts/renamed.module.lua"},
            {"scripts/missing.module.lua", "scripts/renamed.module.lua"},
            {"scripts/folder.module.lua", "scripts/renamed.module.lua"}};
        for(const auto& [from, to] : invalid) {
            SCOPED_TRACE(from.string() + " -> " + to.string());
            const auto report = SourceOperations::rename_module(database, from, to);
            EXPECT_FALSE(report.succeeded());
            EXPECT_FALSE(report.snapshot_updated);
            EXPECT_EQ(database.generation(), generation);
            EXPECT_EQ(database.size(), 0u);
            EXPECT_EQ(read_text_file(source).value(), "return {keep = true}");
        }
        EXPECT_FALSE(std::filesystem::exists(root / "scripts/renamed.module.lua"));
        EXPECT_FALSE(std::filesystem::exists(metadata_path(source)));
        EXPECT_TRUE(std::filesystem::is_empty(root / "other"));
    }

    TEST(AssetSourceOperationsTest, LuaModuleRenameRejectsTargetAndMetadataConflicts) {
        for(const auto* conflict :
            {"target file", "target directory", "source metadata", "target metadata"}) {
            SCOPED_TRACE(conflict);
            const TemporaryProject project;
            const auto source = project.paths().assets() / "shared.module.lua";
            const auto target = project.paths().assets() / "renamed.module.lua";
            ASSERT_TRUE(write_text_file_atomic(source, "return {keep = true}"));
            AssetDatabase database(project.paths());
            ASSERT_TRUE(database.scan().succeeded());
            const auto generation = database.generation();
            std::filesystem::path reserved = target;
            if(std::string_view(conflict) == "source metadata")
                reserved = metadata_path(source);
            if(std::string_view(conflict) == "target metadata")
                reserved = metadata_path(target);
            const bool directory = std::string_view(conflict) == "target directory";
            if(directory)
                std::filesystem::create_directory(reserved);
            else
                ASSERT_TRUE(write_text_file_atomic(reserved, "reserved contents"));

            const auto report = SourceOperations::rename_module(
                database, "shared.module.lua", "renamed.module.lua");

            EXPECT_FALSE(report.succeeded());
            EXPECT_FALSE(report.snapshot_updated);
            EXPECT_TRUE(has_issue_containing(report, "not overwritten"));
            EXPECT_EQ(database.generation(), generation);
            EXPECT_EQ(read_text_file(source).value(), "return {keep = true}");
            if(directory)
                EXPECT_TRUE(std::filesystem::is_directory(reserved));
            else
                EXPECT_EQ(read_text_file(reserved).value(), "reserved contents");
            if(reserved != target)
                EXPECT_FALSE(std::filesystem::exists(target));
        }
    }

    TEST(AssetSourceOperationsTest, LuaModuleRenameRejectsFileAndDirectorySymlinkAliases) {
        const TemporaryProject project;
        const TemporaryDirectory outside;
        const auto root = project.paths().assets();
        std::filesystem::create_directory(root / "real");
        ASSERT_TRUE(write_text_file_atomic(root / "shared.module.lua", "return {keep = true}"));
        ASSERT_TRUE(write_text_file_atomic(root / "real/shared.module.lua", "return {}"));
        ASSERT_TRUE(write_text_file_atomic(outside.path() / "shared.module.lua", "return {}"));
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());
        const auto generation = database.generation();
        std::error_code error;
        std::filesystem::create_directory_symlink(root / "real", root / "alias", error);
        if(error)
            GTEST_SKIP() << "Symlinks unavailable: " << error.message();
        std::filesystem::create_directory_symlink(outside.path(), root / "external", error);
        ASSERT_FALSE(error) << error.message();
        std::filesystem::create_symlink(
            root / "shared.module.lua", root / "link.module.lua", error);
        ASSERT_FALSE(error) << error.message();
        std::filesystem::create_symlink(
            root / "missing.module.lua", root / "dangling.module.lua", error);
        ASSERT_FALSE(error) << error.message();

        using Paths = std::pair<std::filesystem::path, std::filesystem::path>;
        for(const auto& [from, to] :
            std::array{Paths{"alias/shared.module.lua", "alias/renamed.module.lua"},
                Paths{"external/shared.module.lua", "external/renamed.module.lua"},
                Paths{"link.module.lua", "renamed.module.lua"},
                Paths{"dangling.module.lua", "renamed.module.lua"},
                Paths{"shared.module.lua", "link.module.lua"},
                Paths{"shared.module.lua", "dangling.module.lua"}}) {
            SCOPED_TRACE(from.string() + " -> " + to.string());
            const auto report = SourceOperations::rename_module(database, from, to);
            EXPECT_FALSE(report.succeeded());
            EXPECT_FALSE(report.snapshot_updated);
            EXPECT_EQ(database.generation(), generation);
        }
        EXPECT_EQ(read_text_file(root / "shared.module.lua").value(), "return {keep = true}");
        EXPECT_EQ(read_text_file(root / "real/shared.module.lua").value(), "return {}");
        EXPECT_EQ(read_text_file(outside.path() / "shared.module.lua").value(), "return {}");
        EXPECT_TRUE(std::filesystem::is_symlink(root / "link.module.lua"));
        EXPECT_TRUE(std::filesystem::is_symlink(root / "dangling.module.lua"));
        EXPECT_FALSE(std::filesystem::exists(root / "renamed.module.lua"));
        EXPECT_FALSE(std::filesystem::exists(root / "real/renamed.module.lua"));
        EXPECT_FALSE(std::filesystem::exists(outside.path() / "renamed.module.lua"));
    }

    TEST(AssetSourceOperationsTest, LuaModuleRenameRollsBackFailedScanWithoutPublishingIndex) {
        const TemporaryProject project;
        AssetDatabase database(project.paths());
        ASSERT_TRUE(SourceOperations::create_script(database, "keep.lua").succeeded());
        const auto source = project.paths().assets() / "shared.module.lua";
        const auto target = project.paths().assets() / "renamed.module.lua";
        ASSERT_TRUE(write_text_file_atomic(source, "return {keep = true}"));
        const auto handle = database.find("keep.lua")->handle;
        ASSERT_TRUE(database.update_import_dependencies(handle, {"shared.module.lua"}));
        const auto revision = database.get_revision(handle);
        const auto generation = database.generation();
        const auto broken = project.paths().assets() / "broken.mat";
        ASSERT_TRUE(write_text_file_atomic(broken, "{}"));
        ASSERT_TRUE(write_text_file_atomic(metadata_path(broken), "invalid metadata"));

        const auto report =
            SourceOperations::rename_module(database, "shared.module.lua", "renamed.module.lua");

        EXPECT_FALSE(report.succeeded());
        EXPECT_FALSE(report.snapshot_updated);
        EXPECT_TRUE(has_issue_containing(report, "rename was rolled back"));
        EXPECT_TRUE(report.added_assets.empty());
        EXPECT_TRUE(report.removed_assets.empty());
        EXPECT_TRUE(report.modified_assets.empty());
        EXPECT_EQ(report.generated_metadata, 0u);
        EXPECT_EQ(report.indexed_assets, 1u);
        EXPECT_EQ(database.generation(), generation);
        EXPECT_EQ(database.size(), 1u);
        EXPECT_TRUE(database.is_current(handle, revision));
        EXPECT_EQ(database.find("broken.mat"), nullptr);
        EXPECT_EQ(read_text_file(source).value(), "return {keep = true}");
        EXPECT_FALSE(std::filesystem::exists(target));
        EXPECT_FALSE(std::filesystem::exists(metadata_path(source)));
        EXPECT_FALSE(std::filesystem::exists(metadata_path(target)));

        ASSERT_TRUE(std::filesystem::remove(broken));
        ASSERT_TRUE(std::filesystem::remove(metadata_path(broken)));
        const auto retried =
            SourceOperations::rename_module(database, "shared.module.lua", "renamed.module.lua");
        ASSERT_TRUE(retried.succeeded());
        EXPECT_TRUE(retried.snapshot_updated);
        EXPECT_TRUE(contains_handle(retried.modified_assets, handle));
        EXPECT_FALSE(database.is_current(handle, revision));
        EXPECT_FALSE(std::filesystem::exists(source));
        EXPECT_EQ(read_text_file(target).value(), "return {keep = true}");
        EXPECT_EQ(database.size(), 1u);
    }

    class AssetSourceModuleRemovalTest: public ::testing::Test {
    protected:
        TemporaryProject project;
        const ProjectPaths paths = project.paths();
        AssetDatabase database{paths};
        const std::filesystem::path relative = "scripts/shared.module.lua";
        const std::filesystem::path source = paths.assets() / relative;
        const std::filesystem::path trashed = paths.root() / "fake-system-trash/shared.module.lua";
        static constexpr std::string_view CONTENTS = "return {score = 42}\n";
        AssetHandle consumer;
        AssetRevision revision = 0;
        std::uint64_t generation = 0;
        int trash_requests = 0;

        void SetUp() override {
            std::filesystem::create_directories(source.parent_path());
            std::filesystem::create_directory(trashed.parent_path());
            ASSERT_TRUE(write_text_file_atomic(source, CONTENTS));
            ASSERT_TRUE(SourceOperations::create_script(database, "keep.lua").succeeded());
            consumer = database.find("keep.lua")->handle;
            ASSERT_TRUE(database.update_import_dependencies(consumer, {relative}));
            revision = database.get_revision(consumer);
            generation = database.generation();
        }

        AssetScanReport remove(const SourceOperations::TrashMover& mover) {
            return SourceOperations::remove_module(
                database, relative, [&](const std::filesystem::path& entry) {
                    ++trash_requests;
                    EXPECT_EQ(entry, source);
                    return mover(entry);
                });
        }

        Result<void> move_to_trash(const std::filesystem::path& entry) const {
            std::error_code error;
            std::filesystem::rename(entry, trashed, error);
            if(error)
                return Result<void>::failure(error.message());
            return Result<void>::success();
        }

        void expect_unchanged_index(const AssetScanReport& report) const {
            EXPECT_FALSE(report.succeeded());
            EXPECT_FALSE(report.snapshot_updated);
            EXPECT_EQ(database.generation(), generation);
            EXPECT_TRUE(database.is_current(consumer, revision));
            EXPECT_EQ(database.size(), 1u);
            EXPECT_EQ(database.find(relative), nullptr);
            EXPECT_TRUE(report.added_assets.empty());
            EXPECT_TRUE(report.removed_assets.empty());
            EXPECT_TRUE(report.modified_assets.empty());
            EXPECT_EQ(report.generated_metadata, 0u);
            EXPECT_FALSE(std::filesystem::exists(metadata_path(source)));
        }

        void expect_restored(const AssetScanReport& report) const {
            expect_unchanged_index(report);
            ASSERT_TRUE(std::filesystem::is_regular_file(source));
            EXPECT_EQ(read_text_file(source).value(), CONTENTS);
            const auto pending = paths.local_data() / "pending-deletions";
            if(std::filesystem::exists(pending))
                EXPECT_TRUE(std::filesystem::is_empty(pending));
        }
    };

    TEST_F(AssetSourceModuleRemovalTest, MovesOnlySourceToTrashWithoutAssetIdentity) {
        const auto report = remove([&](const auto& entry) { return move_to_trash(entry); });

        ASSERT_TRUE(report.succeeded());
        EXPECT_TRUE(report.snapshot_updated);
        EXPECT_EQ(trash_requests, 1);
        EXPECT_FALSE(std::filesystem::exists(source));
        EXPECT_EQ(read_text_file(trashed).value(), CONTENTS);
        EXPECT_FALSE(std::filesystem::exists(metadata_path(source)));
        EXPECT_FALSE(std::filesystem::exists(metadata_path(trashed)));
        EXPECT_FALSE(std::filesystem::exists(paths.local_data() / "pending-deletions"));
        EXPECT_EQ(database.size(), 1u);
        EXPECT_EQ(database.find(relative), nullptr);
        EXPECT_EQ(report.generated_metadata, 0u);
        EXPECT_TRUE(report.added_assets.empty());
        EXPECT_TRUE(report.removed_assets.empty());
        EXPECT_EQ(report.modified_assets, std::vector{consumer});
    }

    TEST_F(AssetSourceModuleRemovalTest, ScanFailureRestoresBeforeCallingTrash) {
        const auto broken = paths.assets() / "broken.mat";
        ASSERT_TRUE(write_text_file_atomic(broken, "{}"));
        ASSERT_TRUE(write_text_file_atomic(metadata_path(broken), "invalid metadata"));

        const auto report = remove([&](const auto& entry) { return move_to_trash(entry); });

        expect_restored(report);
        EXPECT_EQ(trash_requests, 0);
        EXPECT_FALSE(std::filesystem::exists(trashed));
        EXPECT_EQ(database.find("broken.mat"), nullptr);
    }

    TEST_F(AssetSourceModuleRemovalTest, TrashFailureAndFalseSuccessRestoreSourceAndIndex) {
        for(const auto* behavior : {"reject", "leave source", "move then fail"}) {
            SCOPED_TRACE(behavior);
            const auto report = remove([&](const auto& entry) {
                if(std::string_view(behavior) == "leave source")
                    return Result<void>::success();
                if(std::string_view(behavior) == "move then fail") {
                    auto moved = move_to_trash(entry);
                    if(!moved)
                        return moved;
                }
                return Result<void>::failure("trash operation failed");
            });

            expect_restored(report);
            if(std::string_view(behavior) == "leave source")
                EXPECT_TRUE(has_issue_containing(report, "left the source"));
            if(std::string_view(behavior) == "reject")
                EXPECT_FALSE(has_issue_containing(report, "partial operation"));
            if(std::string_view(behavior) == "move then fail")
                EXPECT_TRUE(has_issue_containing(report, "partial operation"));
        }
        EXPECT_EQ(trash_requests, 3);
        ASSERT_TRUE(std::filesystem::is_regular_file(trashed));
        EXPECT_EQ(read_text_file(trashed).value(), CONTENTS);
        EXPECT_FALSE(std::filesystem::equivalent(source, trashed));
    }

    TEST_F(
        AssetSourceModuleRemovalTest, NewSourceDuringTrashKeepsBothReplacementAndStagedContents) {
        const auto report = remove([&](const auto& entry) {
            auto moved = move_to_trash(entry);
            if(!moved)
                return moved;
            return write_text_file_atomic(entry, "return {replacement = true}");
        });

        expect_unchanged_index(report);
        EXPECT_EQ(trash_requests, 1);
        EXPECT_TRUE(has_issue_containing(report, "left the source"));
        EXPECT_TRUE(has_issue_containing(report, "rollback was incomplete"));
        EXPECT_EQ(read_text_file(source).value(), "return {replacement = true}");
        EXPECT_EQ(read_text_file(trashed).value(), CONTENTS);
        int retained = 0;
        for(const auto& entry : std::filesystem::recursive_directory_iterator(
                paths.local_data() / "pending-deletions")) {
            if(entry.is_regular_file()) {
                ++retained;
                EXPECT_EQ(read_text_file(entry.path()).value(), CONTENTS);
            }
        }
        EXPECT_EQ(retained, 1);
    }

    TEST_F(AssetSourceModuleRemovalTest, DanglingSymlinkLeftByTrashIsNotCommittedOrOverwritten) {
        const auto missing = paths.assets() / "missing.module.lua";
        const auto probe = paths.assets() / "probe.module.lua";
        std::error_code error;
        std::filesystem::create_symlink(missing, probe, error);
        if(error)
            GTEST_SKIP() << "Symlinks unavailable: " << error.message();
        ASSERT_TRUE(std::filesystem::remove(probe));
        const auto report = remove([&](const auto& entry) {
            auto moved = move_to_trash(entry);
            if(!moved)
                return moved;
            std::filesystem::create_symlink(missing, entry, error);
            if(error)
                return Result<void>::failure(error.message());
            return Result<void>::success();
        });

        expect_unchanged_index(report);
        EXPECT_EQ(trash_requests, 1);
        EXPECT_TRUE(has_issue_containing(report, "left the source"));
        EXPECT_TRUE(has_issue_containing(report, "rollback was incomplete"));
        EXPECT_TRUE(std::filesystem::is_symlink(source));
        EXPECT_FALSE(std::filesystem::exists(missing));
        EXPECT_EQ(read_text_file(trashed).value(), CONTENTS);
        EXPECT_FALSE(std::filesystem::is_empty(paths.local_data() / "pending-deletions"));
    }

    TEST_F(AssetSourceModuleRemovalTest, InvalidPathsAndMetadataAreRejectedBeforeTrash) {
        const auto reserved = paths.assets() / "reserved.module.lua";
        ASSERT_TRUE(write_text_file_atomic(reserved, "return {}"));
        ASSERT_TRUE(write_text_file_atomic(metadata_path(reserved), "reserved metadata"));
        std::filesystem::create_directory(paths.assets() / "directory.module.lua");
        ASSERT_TRUE(write_text_file_atomic(paths.assets() / "bad-name.module.lua", "return {}"));
        const std::vector<std::filesystem::path> invalid{"", "keep.lua", "UPPER.MODULE.LUA",
            "bad-name.module.lua", "../escape.module.lua", "scripts/../shared.module.lua",
            "missing.module.lua", "directory.module.lua", "reserved.module.lua", source};
        for(const auto& path : invalid) {
            SCOPED_TRACE(path.string());
            const auto report =
                SourceOperations::remove_module(database, path, [&](const auto& entry) {
                    ++trash_requests;
                    return move_to_trash(entry);
                });
            expect_restored(report);
        }
        expect_restored(SourceOperations::remove_module(database, relative, {}));
        EXPECT_EQ(trash_requests, 0);
        EXPECT_EQ(read_text_file(reserved).value(), "return {}");
        EXPECT_EQ(read_text_file(metadata_path(reserved)).value(), "reserved metadata");
    }

    TEST_F(AssetSourceModuleRemovalTest, FileAndDirectoryAliasesAreRejectedBeforeTrash) {
        const TemporaryDirectory outside;
        ASSERT_TRUE(write_text_file_atomic(outside.path() / "shared.module.lua", CONTENTS));
        std::error_code error;
        std::filesystem::create_symlink(source, paths.assets() / "alias.module.lua", error);
        if(error)
            GTEST_SKIP() << "Symlinks unavailable: " << error.message();
        std::filesystem::create_directory_symlink(
            source.parent_path(), paths.assets() / "internal", error);
        ASSERT_FALSE(error) << error.message();
        std::filesystem::create_directory_symlink(
            outside.path(), paths.assets() / "external", error);
        ASSERT_FALSE(error) << error.message();
        std::filesystem::create_symlink(
            paths.assets() / "missing.module.lua", paths.assets() / "dangling.module.lua", error);
        ASSERT_FALSE(error) << error.message();
        for(const auto* path : {"alias.module.lua", "internal/shared.module.lua",
                "external/shared.module.lua", "dangling.module.lua"}) {
            const auto report =
                SourceOperations::remove_module(database, path, [&](const auto& entry) {
                    ++trash_requests;
                    return move_to_trash(entry);
                });
            expect_restored(report);
        }
        EXPECT_EQ(trash_requests, 0);
        EXPECT_EQ(read_text_file(outside.path() / "shared.module.lua").value(), CONTENTS);
        EXPECT_TRUE(std::filesystem::is_symlink(paths.assets() / "alias.module.lua"));
        EXPECT_TRUE(std::filesystem::is_symlink(paths.assets() / "dangling.module.lua"));
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

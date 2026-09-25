#include "assets/source_operations.h"
#include "support/asset_manager_fixture.h"

namespace Comet::Tests {
    namespace SourceOperations = CometEditor::AssetSourceOperations;

    TEST(AssetSourceOperationsTest, RejectsMoveWhenDestinationAlreadyExists) {
        const TemporaryProject project;
        constexpr AssetHandle handle(42);
        const auto source = project.add_material(handle, "move_test");
        const auto target = project.paths().assets() / "occupied.mat";
        ASSERT_TRUE(MaterialSerializer{}.save(
            {.template_name = "occupied", .texture_properties = {}}, target));
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().snapshot_updated);

        const auto report =
            SourceOperations::move(database, project.paths(), handle, "occupied.mat");

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

        const auto report =
            SourceOperations::move(database, project.paths(), handle, "../outside.mat");

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

        const auto report =
            SourceOperations::move(database, project.paths(), handle, "renamed/moved.mat");

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
        const auto retried =
            SourceOperations::move(database, project.paths(), handle, "renamed/moved.mat");
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

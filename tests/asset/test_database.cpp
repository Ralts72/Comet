#include "asset/database.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace Comet::Tests {
    namespace {
        class TemporaryProject final {
        public:
            TemporaryProject() {
                m_root = std::filesystem::temp_directory_path()
                    / ("comet_asset_database_test_"
                       + std::to_string(AssetHandle::generate().value()));
                std::filesystem::create_directories(paths().assets());
            }

            ~TemporaryProject() {
                std::error_code error;
                std::filesystem::remove_all(m_root, error);
            }

            [[nodiscard]] ProjectPaths paths() const {
                return ProjectPaths(m_root);
            }

            std::filesystem::path add_file(
                const std::filesystem::path& relative_path,
                const std::string& contents = "asset") const {
                const std::filesystem::path path = paths().assets() / relative_path;
                std::filesystem::create_directories(path.parent_path());
                std::ofstream output(path, std::ios::binary);
                output << contents;
                return path;
            }

        private:
            std::filesystem::path m_root;
        };

        bool has_issue_containing(
            const AssetScanReport& report,
            const std::string_view text) {
            return std::ranges::any_of(report.issues, [&](const AssetScanIssue& issue) {
                return issue.message.find(text) != std::string::npos;
            });
        }
    }

    TEST(AssetDatabaseTest, GeneratesMetadataAndBuildsBothIndexes) {
        const TemporaryProject project;
        project.add_file("Textures/albedo.PNG");
        project.add_file("Materials/default.mat");
        AssetDatabase database(project.paths());

        const AssetScanReport report = database.scan();

        EXPECT_TRUE(report.succeeded());
        EXPECT_EQ(report.indexed_assets, 2u);
        EXPECT_EQ(report.generated_metadata, 2u);
        ASSERT_EQ(database.size(), 2u);

        const AssetRecord* texture = database.find("Textures/albedo.PNG");
        ASSERT_NE(texture, nullptr);
        EXPECT_EQ(texture->type, AssetType::Texture);
        EXPECT_EQ(database.find(texture->handle), texture);
        EXPECT_TRUE(std::filesystem::exists(
            metadata_path(project.paths().assets() / texture->path)));
    }

    TEST(AssetDatabaseTest, KeepsIdentityWhenAssetAndMetadataMoveTogether) {
        const TemporaryProject project;
        const std::filesystem::path source = project.add_file("old.png");
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());
        const AssetHandle original_handle = database.find("old.png")->handle;

        const std::filesystem::path moved = project.paths().assets() / "Textures/new.png";
        std::filesystem::create_directories(moved.parent_path());
        std::filesystem::rename(source, moved);
        std::filesystem::rename(metadata_path(source), metadata_path(moved));

        const AssetScanReport report = database.scan();

        EXPECT_TRUE(report.succeeded());
        EXPECT_EQ(report.generated_metadata, 0u);
        EXPECT_EQ(database.find(original_handle)->path, "Textures/new.png");
        EXPECT_EQ(database.find("old.png"), nullptr);
    }

    TEST(AssetDatabaseTest, ReportsDuplicateGuidAndKeepsDeterministicFirstAsset) {
        const TemporaryProject project;
        const std::filesystem::path first = project.add_file("a.png");
        const std::filesystem::path second = project.add_file("b.png");
        const AssetMetadata metadata{
            .handle = AssetHandle(42),
            .type = AssetType::Texture
        };
        const AssetMetadataSerializer serializer;
        serializer.save(metadata, metadata_path(first));
        serializer.save(metadata, metadata_path(second));
        AssetDatabase database(project.paths());

        const AssetScanReport report = database.scan();

        EXPECT_FALSE(report.succeeded());
        EXPECT_TRUE(has_issue_containing(report, "duplicate guid 42"));
        ASSERT_EQ(database.size(), 1u);
        EXPECT_EQ(database.find(AssetHandle(42))->path, "a.png");
    }

    TEST(AssetDatabaseTest, ReportsInvalidAssetsWithoutDroppingValidOnes) {
        const TemporaryProject project;
        project.add_file("valid.scene");
        const std::filesystem::path invalid = project.add_file("invalid.png");
        project.add_file("notes.txt");
        project.add_file("orphan.mat.meta", "version: 1\nguid: 8\ntype: material\n");
        std::ofstream(metadata_path(invalid)) << "version: nope\n";
        AssetDatabase database(project.paths());

        const AssetScanReport report = database.scan();

        EXPECT_FALSE(report.succeeded());
        EXPECT_TRUE(has_issue_containing(report, "metadata has no matching source asset"));
        EXPECT_TRUE(has_issue_containing(report, "unsupported asset extension '.txt'"));
        EXPECT_TRUE(has_issue_containing(report, "Invalid asset metadata"));
        EXPECT_EQ(database.size(), 1u);
        EXPECT_NE(database.find("valid.scene"), nullptr);
    }

    TEST(AssetDatabaseTest, ReportsMissingAssetsDirectory) {
        const TemporaryProject project;
        std::filesystem::remove_all(project.paths().assets());
        AssetDatabase database(project.paths());

        const AssetScanReport report = database.scan();

        EXPECT_FALSE(report.succeeded());
        EXPECT_TRUE(has_issue_containing(report, "Assets directory does not exist"));
        EXPECT_EQ(database.size(), 0u);
    }
}

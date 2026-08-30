#include "asset/database.h"
#include "asset/serialization/metadata_serializer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

        constexpr std::string_view EMPTY_MATERIAL =
            "version: 1\ntemplate: cube_texture\nproperties: {}\n";
    }

    TEST(AssetDatabaseTest, GeneratesMetadataAndBuildsBothIndexes) {
        const TemporaryProject project;
        project.add_file("textures/albedo.PNG");
        project.add_file("Materials/default.mat", std::string(EMPTY_MATERIAL));
        project.add_file(".comet-tmp-interrupted-write.1", "partial");
        AssetDatabase database(project.paths());

        const AssetScanReport report = database.scan();

        EXPECT_TRUE(report.succeeded());
        EXPECT_EQ(report.indexed_assets, 2u);
        EXPECT_EQ(report.generated_metadata, 2u);
        ASSERT_EQ(database.size(), 2u);

        const AssetRecord* texture = database.find("textures/albedo.PNG");
        ASSERT_NE(texture, nullptr);
        EXPECT_EQ(texture->type, AssetType::Texture);
        EXPECT_EQ(database.find(texture->handle), texture);
        const std::filesystem::path sidecar = metadata_path(
            project.paths().assets() / texture->path);
        EXPECT_TRUE(std::filesystem::exists(sidecar));
        const AssetMetadata metadata = AssetMetadataSerializer{}.load(sidecar);
        EXPECT_EQ(
            metadata.import_settings,
            AssetImportSettings(TextureImportSettings{}));
        EXPECT_EQ(texture->import_settings, metadata.import_settings);
    }

    TEST(AssetDatabaseTest, KeepsIdentityWhenAssetAndMetadataMoveTogether) {
        const TemporaryProject project;
        const std::filesystem::path source = project.add_file("old.png");
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());
        const AssetHandle original_handle = database.find("old.png")->handle;

        const std::filesystem::path moved = project.paths().assets() / "textures/new.png";
        std::filesystem::create_directories(moved.parent_path());
        std::filesystem::rename(source, moved);
        std::filesystem::rename(metadata_path(source), metadata_path(moved));

        const AssetScanReport report = database.scan();

        EXPECT_TRUE(report.succeeded());
        EXPECT_EQ(report.generated_metadata, 0u);
        EXPECT_EQ(database.find(original_handle)->path, "textures/new.png");
        EXPECT_EQ(database.find("old.png"), nullptr);
    }

    TEST(AssetDatabaseTest, UpdatesImportSettingsInIndexAndMetadata) {
        const TemporaryProject project;
        project.add_file("textures/albedo.png");
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());
        const AssetRecord* original = database.find("textures/albedo.png");
        ASSERT_NE(original, nullptr);
        const AssetHandle handle = original->handle;
        const TextureImportSettings settings{
            .color_space = TextureColorSpace::Linear,
            .flip_y = true
        };

        database.update_import_settings(handle, settings);

        const AssetRecord* updated = database.find(handle);
        ASSERT_NE(updated, nullptr);
        EXPECT_EQ(updated->import_settings, AssetImportSettings(settings));
        const AssetMetadata metadata = AssetMetadataSerializer{}.load(
            metadata_path(project.paths().assets() / updated->path));
        EXPECT_EQ(metadata.import_settings, AssetImportSettings(settings));
    }

    TEST(AssetDatabaseTest, RejectsImportSettingsForAnotherAssetType) {
        const TemporaryProject project;
        project.add_file("materials/default.mat", std::string(EMPTY_MATERIAL));
        AssetDatabase database(project.paths());
        ASSERT_TRUE(database.scan().succeeded());
        const AssetRecord* material = database.find("materials/default.mat");
        ASSERT_NE(material, nullptr);
        const AssetMetadata original_metadata = AssetMetadataSerializer{}.load(
            metadata_path(project.paths().assets() / material->path));

        EXPECT_THROW(
            database.update_import_settings(
                material->handle,
                TextureImportSettings{}),
            std::runtime_error);
        EXPECT_EQ(
            database.find(material->handle)->import_settings,
            AssetImportSettings(std::monostate{}));
        EXPECT_EQ(
            AssetMetadataSerializer{}.load(
                metadata_path(project.paths().assets() / material->path)),
            original_metadata);
    }

    TEST(AssetDatabaseTest, BuildsAndUpdatesDependencyIndexes) {
        const TemporaryProject project;
        const std::filesystem::path first_texture =
            project.add_file("textures/first.png");
        const std::filesystem::path second_texture =
            project.add_file("textures/second.png");
        const std::filesystem::path material = project.add_file(
            "materials/default.mat",
            "version: 1\ntemplate: cube_texture\nproperties:\n"
            "  first:\n    type: texture\n    asset: 42\n"
            "  repeated:\n    type: texture\n    asset: 42\n"
            "  second:\n    type: texture\n    asset: 73\n");
        const AssetMetadataSerializer serializer;
        serializer.save(
            {
                .handle = AssetHandle(42),
                .type = AssetType::Texture,
                .import_settings = TextureImportSettings{}
            },
            metadata_path(first_texture));
        serializer.save(
            {
                .handle = AssetHandle(73),
                .type = AssetType::Texture,
                .import_settings = TextureImportSettings{}
            },
            metadata_path(second_texture));
        serializer.save(
            {
                .handle = AssetHandle(100),
                .type = AssetType::Material
            },
            metadata_path(material));
        AssetDatabase database(project.paths());

        const AssetScanReport report = database.scan();

        EXPECT_TRUE(report.succeeded());
        EXPECT_EQ(
            std::vector<AssetHandle>(
                database.get_dependencies(AssetHandle(100)).begin(),
                database.get_dependencies(AssetHandle(100)).end()),
            (std::vector{AssetHandle(42), AssetHandle(73)}));
        EXPECT_EQ(
            std::vector<AssetHandle>(
                database.get_dependents(AssetHandle(42)).begin(),
                database.get_dependents(AssetHandle(42)).end()),
            (std::vector{AssetHandle(100)}));

        database.update_dependencies(
            AssetHandle(100),
            {AssetHandle(73), AssetHandle(73)});

        EXPECT_TRUE(database.get_dependents(AssetHandle(42)).empty());
        EXPECT_EQ(
            std::vector<AssetHandle>(
                database.get_dependencies(AssetHandle(100)).begin(),
                database.get_dependencies(AssetHandle(100)).end()),
            (std::vector{AssetHandle(73)}));
        EXPECT_EQ(
            std::vector<AssetHandle>(
                database.get_dependents(AssetHandle(73)).begin(),
                database.get_dependents(AssetHandle(73)).end()),
            (std::vector{AssetHandle(100)}));
    }

    TEST(AssetDatabaseTest, ReportsMissingAndWrongTypeDependencies) {
        const TemporaryProject project;
        const std::filesystem::path referenced_material = project.add_file(
            "materials/referenced.mat", std::string(EMPTY_MATERIAL));
        const std::filesystem::path owner_material = project.add_file(
            "materials/owner.mat",
            "version: 1\ntemplate: cube_texture\nproperties:\n"
            "  missing:\n    type: texture\n    asset: 999\n"
            "  wrong_type:\n    type: texture\n    asset: 73\n");
        const AssetMetadataSerializer serializer;
        serializer.save(
            {
                .handle = AssetHandle(73),
                .type = AssetType::Material
            },
            metadata_path(referenced_material));
        serializer.save(
            {
                .handle = AssetHandle(100),
                .type = AssetType::Material
            },
            metadata_path(owner_material));
        AssetDatabase database(project.paths());

        const AssetScanReport report = database.scan();

        EXPECT_FALSE(report.succeeded());
        EXPECT_TRUE(has_issue_containing(
            report, "dependency handle 999 is not indexed"));
        EXPECT_TRUE(has_issue_containing(
            report,
            "dependency handle 73 has type 'material', expected 'texture'"));
        EXPECT_EQ(
            std::vector<AssetHandle>(
                database.get_dependents(AssetHandle(999)).begin(),
                database.get_dependents(AssetHandle(999)).end()),
            (std::vector{AssetHandle(100)}));
    }

    TEST(AssetDatabaseTest, ReportsDuplicateGuidAndKeepsDeterministicFirstAsset) {
        const TemporaryProject project;
        const std::filesystem::path first = project.add_file("a.png");
        const std::filesystem::path second = project.add_file("b.png");
        const AssetMetadata metadata{
            .handle = AssetHandle(42),
            .type = AssetType::Texture,
            .import_settings = TextureImportSettings{}
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
        project.add_file("orphan.mat.meta", "version: 2\nguid: 8\ntype: material\n");
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
        EXPECT_TRUE(has_issue_containing(report, "assets directory does not exist"));
        EXPECT_EQ(database.size(), 0u);
    }

    TEST(AssetDatabaseTest, PreservesPreviousSnapshotWhenRescanCannotDiscoverAssets) {
        const TemporaryProject project;
        project.add_file("valid.scene");
        AssetDatabase database(project.paths());
        const AssetScanReport initial_scan = database.scan();
        ASSERT_TRUE(initial_scan.snapshot_updated);
        ASSERT_EQ(database.size(), 1u);
        const AssetHandle handle = database.find("valid.scene")->handle;

        std::filesystem::remove_all(project.paths().assets());
        const AssetScanReport failed_rescan = database.scan();

        EXPECT_FALSE(failed_rescan.snapshot_updated);
        EXPECT_TRUE(has_issue_containing(
            failed_rescan, "assets directory does not exist"));
        EXPECT_EQ(database.size(), 1u);
        ASSERT_NE(database.find(handle), nullptr);
        EXPECT_EQ(database.find(handle)->path, "valid.scene");
    }
}

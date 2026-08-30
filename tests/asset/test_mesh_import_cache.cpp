#include "asset/cache/mesh_import_cache.h"

#include "asset/handle.h"
#include "asset/import/mesh_importer.h"
#include "common/file_io.h"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace Comet::Tests {
    namespace {
        class TemporaryCacheProject final {
        public:
            TemporaryCacheProject() {
                m_root = std::filesystem::temp_directory_path()
                    / ("comet_mesh_import_cache_test_"
                       + std::to_string(AssetHandle::generate().value()));
                std::filesystem::create_directories(asset_root());
            }

            ~TemporaryCacheProject() {
                std::error_code error;
                std::filesystem::remove_all(m_root, error);
            }

            [[nodiscard]] std::filesystem::path asset_root() const {
                return m_root / "assets";
            }

            [[nodiscard]] std::filesystem::path cache_path() const {
                return m_root / ".comet/cache/imported/mesh/42.bin";
            }

            [[nodiscard]] std::filesystem::path write_source(
                const std::string_view contents = "mesh source") const {
                const std::filesystem::path path =
                    asset_root() / "meshes/model.gltf";
                write_text_file_atomic(path, contents);
                return path;
            }

            [[nodiscard]] std::filesystem::path write_dependency(
                const std::string_view contents = "buffer data") const {
                const std::filesystem::path path =
                    asset_root() / "buffers/model.bin";
                write_text_file_atomic(path, contents);
                return path;
            }

        private:
            std::filesystem::path m_root;
        };

        [[nodiscard]] MeshData make_mesh_data() {
            const MeshVertex vertex{
                .position = {1.0f, 2.0f, 3.0f},
                .texcoord = {0.25f, 0.75f},
                .normal = {0.0f, 1.0f, 0.0f}
            };
            return {
                .vertices = {vertex, vertex, vertex},
                .indices = {0, 1, 2}
            };
        }

        void expect_mesh_equal(
            const MeshData& actual,
            const MeshData& expected) {
            ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
            ASSERT_EQ(actual.indices, expected.indices);
            EXPECT_EQ(
                actual.vertices.front().position,
                expected.vertices.front().position);
            EXPECT_EQ(
                actual.vertices.front().texcoord,
                expected.vertices.front().texcoord);
            EXPECT_EQ(
                actual.vertices.front().normal,
                expected.vertices.front().normal);
        }
    }

    TEST(MeshImportCacheTest, RoundTripsCurrentEntry) {
        const TemporaryCacheProject project;
        const std::filesystem::path source = project.write_source();
        const MeshData expected = make_mesh_data();

        MeshImportCache::store(
            project.cache_path(),
            project.asset_root(),
            source,
            {},
            MeshImporter::OUTPUT_VERSION,
            expected);
        const auto loaded = MeshImportCache::load_if_current(
            project.cache_path(),
            project.asset_root(),
            source,
            MeshImporter::OUTPUT_VERSION);

        ASSERT_TRUE(loaded.has_value());
        expect_mesh_equal(loaded->data, expected);
        EXPECT_TRUE(loaded->source_dependencies.empty());
    }

    TEST(MeshImportCacheTest, InvalidatesWhenSourceContentChanges) {
        const TemporaryCacheProject project;
        const std::filesystem::path source = project.write_source("source-a");
        MeshImportCache::store(
            project.cache_path(),
            project.asset_root(),
            source,
            {},
            MeshImporter::OUTPUT_VERSION,
            make_mesh_data());

        static_cast<void>(project.write_source("source-b"));

        EXPECT_FALSE(MeshImportCache::load_if_current(
            project.cache_path(),
            project.asset_root(),
            source,
            MeshImporter::OUTPUT_VERSION));
    }

    TEST(MeshImportCacheTest, InvalidatesWhenExternalDependencyChanges) {
        const TemporaryCacheProject project;
        const std::filesystem::path source = project.write_source();
        const std::filesystem::path dependency =
            project.write_dependency("buffer-a");
        const std::array dependencies{dependency};
        MeshImportCache::store(
            project.cache_path(),
            project.asset_root(),
            source,
            dependencies,
            MeshImporter::OUTPUT_VERSION,
            make_mesh_data());

        const auto loaded = MeshImportCache::load_if_current(
            project.cache_path(),
            project.asset_root(),
            source,
            MeshImporter::OUTPUT_VERSION);

        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(
            loaded->source_dependencies,
            (std::vector<std::filesystem::path>{dependency}));

        static_cast<void>(project.write_dependency("buffer-b"));

        EXPECT_FALSE(MeshImportCache::load_if_current(
            project.cache_path(),
            project.asset_root(),
            source,
            MeshImporter::OUTPUT_VERSION));
    }

    TEST(MeshImportCacheTest, RejectsCorruptionAndImporterVersionMismatch) {
        const TemporaryCacheProject project;
        const std::filesystem::path source = project.write_source();
        MeshImportCache::store(
            project.cache_path(),
            project.asset_root(),
            source,
            {},
            MeshImporter::OUTPUT_VERSION,
            make_mesh_data());

        EXPECT_FALSE(MeshImportCache::load_if_current(
            project.cache_path(),
            project.asset_root(),
            source,
            MeshImporter::OUTPUT_VERSION + 1));

        write_text_file_atomic(project.cache_path(), "corrupted");
        EXPECT_FALSE(MeshImportCache::load_if_current(
            project.cache_path(),
            project.asset_root(),
            source,
            MeshImporter::OUTPUT_VERSION));
    }
}

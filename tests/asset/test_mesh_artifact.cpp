#include "asset/artifact/mesh_artifact.h"

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
        class TemporaryArtifactProject final {
        public:
            TemporaryArtifactProject() {
                m_root = std::filesystem::temp_directory_path()
                         / ("comet_mesh_artifact_test_"
                             + std::to_string(AssetHandle::generate().value()));
                std::filesystem::create_directories(asset_root());
            }

            ~TemporaryArtifactProject() {
                std::error_code error;
                std::filesystem::remove_all(m_root, error);
            }

            [[nodiscard]] std::filesystem::path asset_root() const {
                return m_root / "assets";
            }

            [[nodiscard]] std::filesystem::path artifact_path() const {
                return m_root / ".comet/cache/imported/mesh/42.bin";
            }

            [[nodiscard]] std::filesystem::path write_source(
                const std::string_view contents = "mesh source") const {
                const std::filesystem::path path = asset_root() / "meshes/model.gltf";
                write_text_file_atomic(path, contents);
                return path;
            }

            [[nodiscard]] std::filesystem::path write_dependency(
                const std::string_view contents = "buffer data") const {
                const std::filesystem::path path = asset_root() / "buffers/model.bin";
                write_text_file_atomic(path, contents);
                return path;
            }

        private:
            std::filesystem::path m_root;
        };

        [[nodiscard]] MeshData make_mesh_data() {
            const MeshVertex vertex{.position = {1.0f, 2.0f, 3.0f},
                .texcoord = {0.25f, 0.75f},
                .normal = {0.0f, 1.0f, 0.0f}};
            return {.vertices = {vertex, vertex, vertex}, .indices = {0, 1, 2}};
        }

        void expect_mesh_equal(const MeshData& actual, const MeshData& expected) {
            ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
            ASSERT_EQ(actual.indices, expected.indices);
            EXPECT_EQ(
                actual.vertices.front().position, expected.vertices.front().position);
            EXPECT_EQ(
                actual.vertices.front().texcoord, expected.vertices.front().texcoord);
            EXPECT_EQ(actual.vertices.front().normal, expected.vertices.front().normal);
        }
    }

    TEST(MeshArtifactTest, RoundTripsArtifact) {
        const TemporaryArtifactProject project;
        const std::filesystem::path source = project.write_source();
        const MeshData expected = make_mesh_data();

        const MeshArtifact artifact{.handle = AssetHandle(42),
            .importer_version = MeshImporter::VERSION,
            .source_inputs = capture_import_inputs(project.asset_root(), source, {}),
            .data = expected};
        artifact.publish_atomic(project.artifact_path());
        EXPECT_FALSE(MeshArtifact::load(project.artifact_path(), AssetHandle(43)));
        const auto loaded = MeshArtifact::load(project.artifact_path(), AssetHandle(42));

        ASSERT_TRUE(loaded.has_value());
        expect_mesh_equal(loaded->data, expected);
        EXPECT_EQ(loaded->importer_version, MeshImporter::VERSION);
        EXPECT_EQ(loaded->handle, AssetHandle(42));
        EXPECT_TRUE(
            import_inputs_are_current(project.asset_root(), loaded->source_inputs));
    }

    TEST(MeshArtifactTest, LoadingDoesNotInspectSourceFiles) {
        const TemporaryArtifactProject project;
        const std::filesystem::path source = project.write_source("source-a");
        const MeshArtifact artifact{.handle = AssetHandle(42),
            .importer_version = MeshImporter::VERSION,
            .source_inputs = capture_import_inputs(project.asset_root(), source, {}),
            .data = make_mesh_data()};
        artifact.publish_atomic(project.artifact_path());

        static_cast<void>(project.write_source("source-b"));

        EXPECT_TRUE(
            MeshArtifact::load(project.artifact_path(), AssetHandle(42)).has_value());
    }

    TEST(MeshArtifactTest, PersistsExternalSourceInputs) {
        const TemporaryArtifactProject project;
        const std::filesystem::path source = project.write_source();
        const std::filesystem::path dependency = project.write_dependency("buffer-a");
        const std::array dependencies{dependency};
        const MeshArtifact artifact{.handle = AssetHandle(42),
            .importer_version = MeshImporter::VERSION,
            .source_inputs =
                capture_import_inputs(project.asset_root(), source, dependencies),
            .data = make_mesh_data()};
        artifact.publish_atomic(project.artifact_path());

        const auto loaded = MeshArtifact::load(project.artifact_path(), AssetHandle(42));

        ASSERT_TRUE(loaded.has_value());
        ASSERT_EQ(loaded->source_inputs.files.size(), 2);
        EXPECT_EQ(loaded->source_inputs.files[1].relative_path, "buffers/model.bin");
        EXPECT_EQ(loaded->source_dependencies(),
            (std::vector<std::filesystem::path>{"buffers/model.bin"}));
    }

    TEST(MeshArtifactTest, RejectsCorruption) {
        const TemporaryArtifactProject project;
        const std::filesystem::path source = project.write_source();
        const MeshArtifact artifact{.handle = AssetHandle(42),
            .importer_version = MeshImporter::VERSION,
            .source_inputs = capture_import_inputs(project.asset_root(), source, {}),
            .data = make_mesh_data()};
        artifact.publish_atomic(project.artifact_path());

        write_text_file_atomic(project.artifact_path(), "corrupted");
        EXPECT_FALSE(MeshArtifact::load(project.artifact_path(), AssetHandle(42)));
    }
}

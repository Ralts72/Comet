#include "asset/import/input_snapshot.h"

#include "asset/handle.h"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>

namespace Comet::Tests {
    namespace {
        class TemporaryInputProject final {
        public:
            TemporaryInputProject() {
                m_root = std::filesystem::temp_directory_path()
                         / ("comet_import_input_snapshot_test_"
                             + std::to_string(AssetHandle::generate().value()));
                std::filesystem::create_directories(m_root / "assets");
            }

            ~TemporaryInputProject() {
                std::error_code error;
                std::filesystem::remove_all(m_root, error);
            }

            [[nodiscard]] std::filesystem::path assets() const {
                return m_root / "assets";
            }

            [[nodiscard]] std::filesystem::path write(
                const std::filesystem::path& relative_path,
                const std::string& contents) const {
                const std::filesystem::path path = assets() / relative_path;
                std::filesystem::create_directories(path.parent_path());
                std::ofstream output(path, std::ios::binary);
                output << contents;
                return path;
            }

        private:
            std::filesystem::path m_root;
        };
    }

    TEST(ImportInputSnapshotTest, CapturesSourceAndSortedDependencies) {
        const TemporaryInputProject project;
        const std::filesystem::path source = project.write("meshes/model.gltf", "source");
        const std::filesystem::path first = project.write("buffers/a.bin", "first");
        const std::filesystem::path second = project.write("buffers/b.bin", "second");

        const std::array dependencies{second, first, second};
        const ImportInputSnapshot snapshot =
            capture_import_inputs(project.assets(), source, dependencies);

        ASSERT_EQ(snapshot.files.size(), 3u);
        EXPECT_EQ(snapshot.files[0].relative_path, "meshes/model.gltf");
        EXPECT_EQ(snapshot.files[1].relative_path, "buffers/a.bin");
        EXPECT_EQ(snapshot.files[2].relative_path, "buffers/b.bin");
        EXPECT_TRUE(import_inputs_are_current(project.assets(), snapshot));
    }

    TEST(ImportInputSnapshotTest, DetectsContentChangesWithStableFileSize) {
        const TemporaryInputProject project;
        const std::filesystem::path source = project.write("mesh.gltf", "source-a");
        const ImportInputSnapshot snapshot =
            capture_import_inputs(project.assets(), source, {});

        static_cast<void>(project.write("mesh.gltf", "source-b"));

        EXPECT_FALSE(import_inputs_are_current(project.assets(), snapshot));
    }

    TEST(ImportInputSnapshotTest, RejectsInputsOutsideAssetRoot) {
        const TemporaryInputProject project;
        const std::filesystem::path source = project.write("mesh.gltf", "source");
        const std::filesystem::path outside =
            project.assets().parent_path() / "outside.bin";
        std::ofstream(outside) << "outside";

        const std::array dependencies{outside};
        EXPECT_THROW(static_cast<void>(
                         capture_import_inputs(project.assets(), source, dependencies)),
            std::runtime_error);
    }
}

#include "asset/import/input_snapshot.h"

#include "asset/handle.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace Comet::Tests {
    namespace {
        class TemporaryInputProject final {
        public:
            TemporaryInputProject() { std::filesystem::create_directories(assets()); }

            [[nodiscard]] std::filesystem::path assets() const {
                return m_directory.path() / "assets";
            }

            [[nodiscard]] std::filesystem::path write(
                const std::filesystem::path& relative_path, const std::string& contents) const {
                const std::filesystem::path path = assets() / relative_path;
                std::filesystem::create_directories(path.parent_path());
                std::ofstream output(path, std::ios::binary);
                output << contents;
                return path;
            }

        private:
            TemporaryDirectory m_directory;
        };
    }

    TEST(ImportInputSnapshotTest, CapturesSourceAndSortedDependencies) {
        const TemporaryInputProject project;
        const std::filesystem::path source = project.write("meshes/model.gltf", "source");
        const std::filesystem::path first = project.write("buffers/a.bin", "first");
        const std::filesystem::path second = project.write("buffers/b.bin", "second");

        const std::array dependencies{second, first, second};
        const ImportInputSnapshot snapshot =
            capture_import_inputs(project.assets(), source, dependencies).value();

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
            capture_import_inputs(project.assets(), source, {}).value();

        static_cast<void>(project.write("mesh.gltf", "source-b"));

        EXPECT_FALSE(import_inputs_are_current(project.assets(), snapshot));
    }

    TEST(ImportInputSnapshotTest, RejectsInputsOutsideAssetRoot) {
        const TemporaryInputProject project;
        const std::filesystem::path source = project.write("mesh.gltf", "source");
        const std::filesystem::path outside = project.assets().parent_path() / "outside.bin";
        std::ofstream(outside) << "outside";

        const std::array dependencies{outside};
        EXPECT_FALSE(capture_import_inputs(project.assets(), source, dependencies));
    }

    TEST(ImportInputSnapshotTest, RejectsMalformedManifestEvenWhenFilesExist) {
        const TemporaryInputProject project;
        const auto source = project.write("meshes/model.gltf", "source");
        const std::array dependencies{
            project.write("buffers/a.bin", "first"), project.write("buffers/b.bin", "second")};
        const auto captured = capture_import_inputs(project.assets(), source, dependencies);
        ASSERT_TRUE(captured);

        for(const auto& path :
            {std::filesystem::path{}, source, std::filesystem::path("buffers/../buffers/a.bin"),
                std::filesystem::path("meshes/model.gltf")}) {
            auto invalid = captured.value();
            invalid.files[1].relative_path = path;
            EXPECT_FALSE(import_inputs_are_current(project.assets(), invalid))
                << path.generic_string();
        }
        auto duplicate = captured.value();
        duplicate.files.push_back(duplicate.files.back());
        EXPECT_FALSE(import_inputs_are_current(project.assets(), duplicate));
        auto reordered = captured.value();
        std::swap(reordered.files[1], reordered.files[2]);
        EXPECT_FALSE(import_inputs_are_current(project.assets(), reordered));
        EXPECT_FALSE(import_inputs_are_current(project.assets(), {}));
    }
}

#include "asset/import/mesh_importer.h"
#include "asset/handle.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Comet::Tests {
    namespace {
        constexpr std::string_view TRIANGLE_BUFFER =
            "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA";

        class TemporaryDirectory final {
        public:
            TemporaryDirectory() {
                m_path = std::filesystem::temp_directory_path()
                    / ("comet_mesh_importer_test_"
                       + std::to_string(AssetHandle::generate().value()));
                std::filesystem::create_directories(m_path);
            }

            ~TemporaryDirectory() {
                std::error_code error;
                std::filesystem::remove_all(m_path, error);
            }

            [[nodiscard]] std::filesystem::path write_text(
                const std::string_view name,
                const std::string_view contents) const {
                const std::filesystem::path path = m_path / name;
                std::ofstream output(path, std::ios::binary);
                output << contents;
                return path;
            }

            [[nodiscard]] std::filesystem::path write_glb(
                const std::string_view name,
                std::string json) const {
                while(json.size() % 4 != 0) {
                    json.push_back(' ');
                }

                const std::filesystem::path path = m_path / name;
                std::ofstream output(path, std::ios::binary);
                write_u32(output, 0x46546C67);
                write_u32(output, 2);
                write_u32(
                    output,
                    static_cast<std::uint32_t>(12 + 8 + json.size()));
                write_u32(output, static_cast<std::uint32_t>(json.size()));
                write_u32(output, 0x4E4F534A);
                output.write(
                    json.data(), static_cast<std::streamsize>(json.size()));
                return path;
            }

        private:
            static void write_u32(
                std::ofstream& output,
                const std::uint32_t value) {
                const char bytes[] = {
                    static_cast<char>(value & 0xFF),
                    static_cast<char>((value >> 8) & 0xFF),
                    static_cast<char>((value >> 16) & 0xFF),
                    static_cast<char>((value >> 24) & 0xFF)
                };
                output.write(bytes, sizeof(bytes));
            }

            std::filesystem::path m_path;
        };

        [[nodiscard]] std::string make_triangle_gltf(
            const std::string_view primitive) {
            return std::string(
                R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"data:application/octet-stream;base64,)"
            ) + std::string(TRIANGLE_BUFFER)
                + R"("}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[)"
                + std::string(primitive) + "]}]}";
        }
    }

    TEST(MeshImporterTest, ImportsProjectCubeAsset) {
        const std::filesystem::path source =
            std::filesystem::path(PROJECT_ROOT_DIR)
            / "assets/meshes/cube.gltf";

        const MeshData data = MeshImporter{}.import(source);

        EXPECT_EQ(data.vertices.size(), 24);
        EXPECT_EQ(data.indices.size(), 36);
    }

    TEST(MeshImporterTest, ImportsGltfAndGeneratesMissingVertexData) {
        const TemporaryDirectory directory;
        const std::filesystem::path source = directory.write_text(
            "triangle.gltf",
            make_triangle_gltf(
                R"({"attributes":{"POSITION":0},"indices":1})"));

        const MeshData data = MeshImporter{}.import(source);

        ASSERT_EQ(data.vertices.size(), 3);
        ASSERT_EQ(data.indices.size(), 3);
        EXPECT_EQ(data.indices, (std::vector<std::uint32_t>{0, 1, 2}));
        EXPECT_EQ(data.vertices[1].position, Math::Vec3(1.0f, 0.0f, 0.0f));
        EXPECT_EQ(data.vertices[2].texcoord, Math::Vec2(0.0f));
        for(const MeshVertex& vertex: data.vertices) {
            EXPECT_NEAR(vertex.normal.x, 0.0f, 1.0e-6f);
            EXPECT_NEAR(vertex.normal.y, 0.0f, 1.0e-6f);
            EXPECT_NEAR(vertex.normal.z, 1.0f, 1.0e-6f);
        }
    }

    TEST(MeshImporterTest, ImportsGlbContainer) {
        const TemporaryDirectory directory;
        const std::filesystem::path source = directory.write_glb(
            "triangle.glb",
            make_triangle_gltf(
                R"({"attributes":{"POSITION":0},"indices":1})"));

        const MeshData data = MeshImporter{}.import(source);

        EXPECT_EQ(data.vertices.size(), 3);
        EXPECT_EQ(data.indices, (std::vector<std::uint32_t>{0, 1, 2}));
    }

    TEST(MeshImporterTest, ConcatenatesTrianglePrimitives) {
        const TemporaryDirectory directory;
        const std::filesystem::path source = directory.write_text(
            "two_primitives.gltf",
            make_triangle_gltf(
                R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})"));

        const MeshData data = MeshImporter{}.import(source);

        EXPECT_EQ(data.vertices.size(), 6);
        EXPECT_EQ(
            data.indices,
            (std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5}));
    }

    TEST(MeshImporterTest, RejectsUnsupportedPrimitiveTopology) {
        const TemporaryDirectory directory;
        const std::filesystem::path source = directory.write_text(
            "lines.gltf",
            make_triangle_gltf(
                R"({"attributes":{"POSITION":0},"indices":1,"mode":1})"));

        EXPECT_THROW(
            static_cast<void>(MeshImporter{}.import(source)),
            std::runtime_error);
    }

    TEST(MeshImporterTest, RejectsPrimitiveWithoutPosition) {
        const TemporaryDirectory directory;
        const std::filesystem::path source = directory.write_text(
            "missing_position.gltf",
            make_triangle_gltf(R"({"attributes":{},"indices":1})"));

        EXPECT_THROW(
            static_cast<void>(MeshImporter{}.import(source)),
            std::runtime_error);
    }

    TEST(MeshImporterTest, RejectsCorruptedInput) {
        const TemporaryDirectory directory;
        const std::filesystem::path source = directory.write_text(
            "corrupted.gltf", "not glTF");

        EXPECT_THROW(
            static_cast<void>(MeshImporter{}.import(source)),
            std::runtime_error);
    }
}

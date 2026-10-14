#include "asset/import/mesh_importer.h"
#include "asset/data/mesh_data.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace Comet::Tests {
    namespace {
        constexpr std::string_view TRIANGLE_BUFFER =
            "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA";

        class MeshImportInputs final {
        public:
            [[nodiscard]] std::filesystem::path write_text(
                const std::string_view name, const std::string_view contents) const {
                const std::filesystem::path path = m_directory.path() / name;
                std::ofstream output(path, std::ios::binary);
                output << contents;
                return path;
            }

            [[nodiscard]] std::filesystem::path write_glb(
                const std::string_view name, std::string json) const {
                while(json.size() % 4 != 0) {
                    json.push_back(' ');
                }

                const std::filesystem::path path = m_directory.path() / name;
                std::ofstream output(path, std::ios::binary);
                write_u32(output, 0x46546C67);
                write_u32(output, 2);
                write_u32(output, static_cast<std::uint32_t>(12 + 8 + json.size()));
                write_u32(output, static_cast<std::uint32_t>(json.size()));
                write_u32(output, 0x4E4F534A);
                output.write(json.data(), static_cast<std::streamsize>(json.size()));
                return path;
            }

            [[nodiscard]] std::filesystem::path write_triangle_buffer(
                const std::string_view name) const {
                const std::filesystem::path path = m_directory.path() / name;
                std::ofstream output(path, std::ios::binary);
                constexpr std::array<float, 9> positions{
                    0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
                for(const float value : positions) {
                    write_u32(output, std::bit_cast<std::uint32_t>(value));
                }
                write_u16(output, 0);
                write_u16(output, 1);
                write_u16(output, 2);
                return path;
            }

            [[nodiscard]] std::filesystem::path write_tangent_mesh(
                Math::Vec4 tangent, bool with_normals = true) const {
                std::ofstream output(m_directory.path() / "tangent.bin", std::ios::binary);
                for(const auto position :
                    std::array{Math::Vec3(0, 0, 0), Math::Vec3(1, 0, 0), Math::Vec3(0, 1, 0)}) {
                    for(const float value : std::array{position.x, position.y, position.z, 0.f, 0.f,
                            1.f, tangent.x, tangent.y, tangent.z, tangent.w})
                        write_u32(output, std::bit_cast<uint32_t>(value));
                }
                output.close();
                const std::string attributes = with_normals
                                                   ? R"({"POSITION":0,"NORMAL":1,"TANGENT":2})"
                                                   : R"({"POSITION":0,"TANGENT":2})";
                return write_text("tangent.gltf",
                    R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":120,"uri":"tangent.bin"}],
                    "bufferViews":[{"buffer":0,"byteLength":120,"byteStride":40}],
                    "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
                    {"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC3"},
                    {"bufferView":0,"byteOffset":24,"componentType":5126,"count":3,"type":"VEC4"}],
                    "meshes":[{"primitives":[{"attributes":)"
                        + attributes + "}]}]}");
            }

            [[nodiscard]] std::filesystem::path write_surface(
                const MeshData& mesh, bool with_normals = true, bool with_uv = true) const {
                std::ofstream output(m_directory.path() / "surface.bin", std::ios::binary);
                for(size_t index = 0; index < mesh.vertices.size(); ++index) {
                    const auto& vertex = mesh.vertices[index];
                    for(const float value : std::array{vertex.position.x, vertex.position.y,
                            vertex.position.z, vertex.normal.x, vertex.normal.y, vertex.normal.z,
                            vertex.texcoord.x, vertex.texcoord.y})
                        write_u32(output, std::bit_cast<uint32_t>(value));
                    const auto skin = mesh.skin.empty() ? SkinInfluences{{0, 0, 0, 0}, {1, 0, 0, 0}}
                                                        : mesh.skin[index];
                    for(const auto joint : skin.joints)
                        write_u16(output, uint16_t(joint));
                    for(int component = 0; component < 4; ++component)
                        write_u32(output, std::bit_cast<uint32_t>(skin.weights[component]));
                }
                for(const auto index : mesh.indices)
                    write_u32(output, index);
                output.close();
                const auto vertex_bytes = std::to_string(mesh.vertices.size() * 56);
                const auto index_bytes = std::to_string(mesh.indices.size() * 4);
                const auto count = std::to_string(mesh.vertices.size());
                std::string attributes = R"("POSITION":0)";
                if(with_normals)
                    attributes += R"(,"NORMAL":1)";
                if(with_uv)
                    attributes += R"(,"TEXCOORD_0":2)";
                if(!mesh.skin.empty())
                    attributes += R"(,"JOINTS_0":3,"WEIGHTS_0":4)";
                const std::string skin =
                    mesh.skin.empty()
                        ? ""
                        : R"(,"nodes":[{"mesh":0,"skin":0},{},{}],"skins":[{"joints":[1,2]}])";
                return write_text("surface.gltf",
                    R"({"asset":{"version":"2.0"},"buffers":[{"uri":"surface.bin","byteLength":)"
                        + std::to_string(mesh.vertices.size() * 56 + mesh.indices.size() * 4)
                        + R"(}],"bufferViews":[{"buffer":0,"byteLength":)" + vertex_bytes
                        + R"(,"byteStride":56},{"buffer":0,"byteOffset":)" + vertex_bytes
                        + R"(,"byteLength":)" + index_bytes + R"(}],"accessors":[
                    {"bufferView":0,"componentType":5126,"count":)"
                        + count + R"(,"type":"VEC3","min":[-1,-1,0],"max":[1,1,1]},
                    {"bufferView":0,"byteOffset":12,"componentType":5126,"count":)"
                        + count + R"(,"type":"VEC3"},
                    {"bufferView":0,"byteOffset":24,"componentType":5126,"count":)"
                        + count + R"(,"type":"VEC2"},
                    {"bufferView":0,"byteOffset":32,"componentType":5123,"count":)"
                        + count + R"(,"type":"VEC4"},
                    {"bufferView":0,"byteOffset":40,"componentType":5126,"count":)"
                        + count + R"(,"type":"VEC4"},
                    {"bufferView":1,"componentType":5125,"count":)"
                        + std::to_string(mesh.indices.size()) + R"(,"type":"SCALAR"}],
                    "meshes":[{"primitives":[{"attributes":{)"
                        + attributes + R"(},"indices":5}]}])" + skin + "}");
            }

        private:
            static void write_u16(std::ofstream& output, const std::uint16_t value) {
                const char bytes[] = {
                    static_cast<char>(value & 0xFF), static_cast<char>((value >> 8) & 0xFF)};
                output.write(bytes, sizeof(bytes));
            }

            static void write_u32(std::ofstream& output, const std::uint32_t value) {
                const char bytes[] = {static_cast<char>(value & 0xFF),
                    static_cast<char>((value >> 8) & 0xFF), static_cast<char>((value >> 16) & 0xFF),
                    static_cast<char>((value >> 24) & 0xFF)};
                output.write(bytes, sizeof(bytes));
            }

            TemporaryDirectory m_directory;
        };

        [[nodiscard]] std::string make_triangle_gltf(
            const std::string_view primitive, const std::string_view buffer_uri = {}) {
            std::string uri(buffer_uri);
            if(buffer_uri.empty()) {
                uri = "data:application/octet-stream;base64," + std::string(TRIANGLE_BUFFER);
            }
            return std::string(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":")")
                   + uri
                   + R"("}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[)"
                   + std::string(primitive) + "]}]}";
        }
    }

    TEST(MeshImporterTest, GeneratesMikkTangentsAndSplitsMirroredSeamsWithSkinWeights) {
        const MeshImportInputs directory;
        MeshData source{
            .vertices = {{{-1, -1, 0}, {0, 0}, {0, 0, 1}}, {{1, -1, 0}, {1, 0}, {0, 0, 1}},
                {{1, 1, 0}, {1, 1}, {0, 0, 1}}, {{-1, 1, 0}, {0, 1}, {0, 0, 1}}},
            .indices = {0, 1, 2, 0, 2, 3}};
        const auto plain = MeshImporter{}.import(directory.write_surface(source));
        ASSERT_TRUE(plain) << plain.error();
        EXPECT_EQ(plain.value().vertices.size(), 4u);
        EXPECT_EQ(plain.value().indices, source.indices);
        for(const auto& vertex : plain.value().vertices)
            EXPECT_LT(glm::length(vertex.tangent - Math::Vec4(1, 0, 0, 1)), 1e-5f);

        source.vertices[3].texcoord = {1, 0};
        source.skin = {{{0, 1, 0, 0}, {.25f, .75f, 0, 0}}, {{1, 0, 0, 0}, {1, 0, 0, 0}},
            {{0, 1, 0, 0}, {.6f, .4f, 0, 0}}, {{0, 0, 0, 0}, {1, 0, 0, 0}}};
        const auto path = directory.write_surface(source);
        const auto estimate = MeshImporter::working_bytes(path);
        ASSERT_TRUE(estimate);
        EXPECT_FALSE(MeshImporter{}.import(path, estimate.value() - 1));
        const auto imported = MeshImporter{}.import(path, estimate.value());
        ASSERT_TRUE(imported) << imported.error();
        const auto& mesh = imported.value();
        ASSERT_EQ(mesh.vertices.size(), 6u);
        ASSERT_EQ(mesh.skin.size(), mesh.vertices.size());
        ASSERT_TRUE(mesh.animation);
        for(size_t corner = 0; corner < mesh.indices.size(); ++corner) {
            const auto source_index = source.indices[corner], result_index = mesh.indices[corner];
            const auto& vertex = mesh.vertices[result_index];
            EXPECT_EQ(vertex.position, source.vertices[source_index].position);
            EXPECT_EQ(mesh.skin[result_index].joints, source.skin[source_index].joints);
            EXPECT_LT(
                glm::length(mesh.skin[result_index].weights - source.skin[source_index].weights),
                1e-6f);
            const auto expected = corner < 3 ? Math::Vec4(1, 0, 0, 1) : Math::Vec4(0, 1, 0, -1);
            EXPECT_LT(glm::length(vertex.tangent - expected), 1e-5f);
        }
        // 三角形输入顺序不影响切线基。
        source.indices = {0, 2, 3, 0, 1, 2};
        const auto reversed = MeshImporter{}.import(directory.write_surface(source));
        ASSERT_TRUE(reversed) << reversed.error();
        for(size_t corner = 0; corner < 6; ++corner)
            EXPECT_EQ(reversed.value().vertices[reversed.value().indices[corner]].tangent,
                mesh.vertices[mesh.indices[(corner + 3) % 6]].tangent);
    }

    TEST(MeshImporterTest, GeneratesFlatNormalsAndHandlesMissingOrDegenerateUvs) {
        const MeshImportInputs directory;
        MeshData source{.vertices = {{{-1, -1, 0}, {0, 0}}, {{1, -1, 0}, {1, 0}},
                            {{1, 1, 0}, {1, 1}}, {{-1, 1, 1}, {0, 1}}},
            .indices = {0, 1, 2, 0, 2, 3}};
        for(bool uv : {true, false}) {
            const auto result = MeshImporter{}.import(directory.write_surface(source, false, uv));
            ASSERT_TRUE(result) << result.error();
            ASSERT_EQ(result.value().vertices.size(), 6u);
            for(size_t corner = 0; corner < 6; ++corner) {
                const auto& vertex = result.value().vertices[result.value().indices[corner]];
                const auto expected =
                    corner < 3 ? Math::Vec3(0, 0, 1) : glm::normalize(Math::Vec3(2, -2, 4));
                EXPECT_LT(glm::length(vertex.normal - expected), 1e-5f);
                if(uv) {
                    EXPECT_NEAR(glm::length(Math::Vec3(vertex.tangent)), 1.f, 1e-5f);
                    EXPECT_NEAR(glm::dot(vertex.normal, Math::Vec3(vertex.tangent)), 0.f, 1e-5f);
                } else
                    EXPECT_EQ(vertex.tangent, Math::Vec4(0));
            }
        }
        for(auto& vertex : source.vertices)
            vertex.texcoord = Math::Vec2(0);
        const auto degenerate = MeshImporter{}.import(directory.write_surface(source, false));
        ASSERT_TRUE(degenerate) << degenerate.error();
        for(const auto& vertex : degenerate.value().vertices) {
            EXPECT_TRUE(Math::is_finite(vertex.tangent));
            EXPECT_TRUE(Math::is_finite(vertex.normal));
        }
    }

    TEST(MeshImporterTest, PreservesAuthoredTangentsAndRejectsInvalidDirections) {
        const MeshImportInputs directory;
        for(const auto tangent : std::array{Math::Vec4(0, 1, 0, 1), Math::Vec4(1, 0, 0, -1)}) {
            const auto result = MeshImporter{}.import(directory.write_tangent_mesh(tangent));
            ASSERT_TRUE(result) << result.error();
            for(const auto& vertex : result.value().vertices)
                EXPECT_EQ(vertex.tangent, tangent);
        }
        for(const auto tangent :
            std::array{Math::Vec4(1, 0, 0, 0), Math::Vec4(0, 0, 0, 1), Math::Vec4(0, 0, 1, 1),
                Math::Vec4(std::numeric_limits<float>::quiet_NaN(), 0, 0, 1)}) {
            const auto result = MeshImporter{}.import(directory.write_tangent_mesh(tangent));
            ASSERT_FALSE(result);
            EXPECT_NE(result.error().find("TANGENT"), std::string::npos);
        }
        const auto generated =
            MeshImporter{}.import(directory.write_tangent_mesh({1, 0, 0, 1}, false));
        ASSERT_TRUE(generated) << generated.error();
        for(const auto& vertex : generated.value().vertices)
            EXPECT_EQ(vertex.tangent, Math::Vec4(0));
    }

    TEST(MeshImporterTest, ImportsProjectCubeAsset) {
        const std::filesystem::path source =
            std::filesystem::path(COMET_SAMPLE_PROJECT_DIRECTORY) / "assets/meshes/cube.gltf";

        const MeshData data = MeshImporter{}.import(source).value();

        EXPECT_EQ(data.vertices.size(), 24);
        EXPECT_EQ(data.indices.size(), 36);
        for(std::size_t index = 0; index + 2 < data.indices.size(); index += 3) {
            const auto i0 = data.indices[index];
            const auto i1 = data.indices[index + 1];
            const auto i2 = data.indices[index + 2];
            ASSERT_LT(i0, data.vertices.size());
            ASSERT_LT(i1, data.vertices.size());
            ASSERT_LT(i2, data.vertices.size());
            const auto& v0 = data.vertices[i0];
            const auto& v1 = data.vertices[i1];
            const auto& v2 = data.vertices[i2];
            const auto face_normal =
                Math::cross(v1.position - v0.position, v2.position - v0.position);
            for(const auto normal : std::array{v0.normal, v1.normal, v2.normal})
                EXPECT_GT(Math::dot(face_normal, normal), 0.0f) << "Triangle " << index / 3;
        }
    }

    TEST(MeshImporterTest, ImportsGltfAndGeneratesMissingVertexData) {
        const MeshImportInputs directory;
        const std::filesystem::path source = directory.write_text(
            "triangle.gltf", make_triangle_gltf(R"({"attributes":{"POSITION":0},"indices":1})"));

        const MeshData data = MeshImporter{}.import(source).value();

        ASSERT_EQ(data.vertices.size(), 3);
        ASSERT_EQ(data.indices.size(), 3);
        EXPECT_EQ(data.indices, (std::vector<std::uint32_t>{0, 1, 2}));
        EXPECT_EQ(data.vertices[1].position, Math::Vec3(1.0f, 0.0f, 0.0f));
        EXPECT_EQ(data.vertices[2].texcoord, Math::Vec2(0.0f));
        for(const MeshVertex& vertex : data.vertices) {
            EXPECT_NEAR(vertex.normal.x, 0.0f, 1.0e-6f);
            EXPECT_NEAR(vertex.normal.y, 0.0f, 1.0e-6f);
            EXPECT_NEAR(vertex.normal.z, 1.0f, 1.0e-6f);
        }
    }

    TEST(MeshImporterTest, ChecksWorkingSetBeforeBuildingMeshData) {
        const MeshImportInputs directory;
        const auto source = directory.write_text(
            "triangle.gltf", make_triangle_gltf(R"({"attributes":{"POSITION":0},"indices":1})"));
        const auto estimate = MeshImporter::working_bytes(source);
        ASSERT_TRUE(estimate);
        EXPECT_GT(estimate.value(), std::filesystem::file_size(source));
        EXPECT_FALSE(MeshImporter{}.import(source, estimate.value() - 1));
        EXPECT_TRUE(MeshImporter{}.import(source, estimate.value()));

        AssetImportLimits limits;
        limits.source_bytes = std::filesystem::file_size(source) - 1;
        EXPECT_FALSE(MeshImporter::working_bytes(source, limits));
        limits.source_bytes = AssetImportLimits{}.source_bytes;
        limits.mesh_working_bytes = estimate.value() - 1;
        EXPECT_FALSE(MeshImporter::working_bytes(source, limits));
        EXPECT_FALSE(MeshImporter{}.import(source, estimate.value(), limits));

        auto oversized = make_triangle_gltf(R"({"attributes":{"POSITION":0},"indices":1})");
        const auto count = oversized.find("\"count\":3");
        ASSERT_NE(count, std::string::npos);
        oversized.replace(count, std::string("\"count\":3").size(), "\"count\":4294967295");
        const auto huge_source = directory.write_text("huge.gltf", oversized);
        const auto rejected = MeshImporter::working_bytes(huge_source);
        ASSERT_FALSE(rejected);
        EXPECT_NE(rejected.error().find("working-set budget"), std::string::npos);
    }

    TEST(MeshImporterTest, IncludesExternalBufferSizeInWorkingSet) {
        const MeshImportInputs directory;
        const auto buffer = directory.write_triangle_buffer("triangle.bin");
        const auto source = directory.write_text("triangle.gltf",
            make_triangle_gltf(R"({"attributes":{"POSITION":0},"indices":1})", "triangle.bin"));
        const auto before = MeshImporter::working_bytes(source);
        ASSERT_TRUE(before);
        std::filesystem::resize_file(buffer, 1024 * 1024);
        const auto after = MeshImporter::working_bytes(source);
        ASSERT_TRUE(after);
        EXPECT_EQ(after.value() - before.value(), 3 * (1024 * 1024 - 42));
    }

    TEST(MeshImporterTest, ImportsGlbContainer) {
        const MeshImportInputs directory;
        const std::filesystem::path source = directory.write_glb(
            "triangle.glb", make_triangle_gltf(R"({"attributes":{"POSITION":0},"indices":1})"));

        const MeshData data = MeshImporter{}.import(source).value();

        EXPECT_EQ(data.vertices.size(), 3);
        EXPECT_EQ(data.indices, (std::vector<std::uint32_t>{0, 1, 2}));
    }

    TEST(MeshImporterTest, ReportsExternalBufferDependencies) {
        const MeshImportInputs directory;
        const std::filesystem::path buffer = directory.write_triangle_buffer("triangle.bin");
        const std::filesystem::path source = directory.write_text("triangle.gltf",
            make_triangle_gltf(R"({"attributes":{"POSITION":0},"indices":1})", "triangle.bin"));

        const MeshImportData result = MeshImporter{}.import_with_dependencies(source).value();

        EXPECT_EQ(result.data.vertices.size(), 3);
        ASSERT_EQ(result.source_dependencies.size(), 1);
        EXPECT_EQ(result.source_dependencies.front(), buffer);
    }

    TEST(MeshImporterTest, ConcatenatesTrianglePrimitives) {
        const MeshImportInputs directory;
        const std::filesystem::path source = directory.write_text("two_primitives.gltf",
            make_triangle_gltf(
                R"({"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1})"));

        const MeshData data = MeshImporter{}.import(source).value();

        EXPECT_EQ(data.vertices.size(), 6);
        EXPECT_EQ(data.indices, (std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5}));
    }

    TEST(MeshImporterTest, RejectsUnsupportedPrimitiveTopology) {
        const MeshImportInputs directory;
        const std::filesystem::path source = directory.write_text("lines.gltf",
            make_triangle_gltf(R"({"attributes":{"POSITION":0},"indices":1,"mode":1})"));

        EXPECT_FALSE(MeshImporter{}.import(source));
    }

    TEST(MeshImporterTest, RejectsPrimitiveWithoutPosition) {
        const MeshImportInputs directory;
        const std::filesystem::path source = directory.write_text(
            "missing_position.gltf", make_triangle_gltf(R"({"attributes":{},"indices":1})"));

        EXPECT_FALSE(MeshImporter{}.import(source));
    }

    TEST(MeshImporterTest, RejectsCorruptedInput) {
        const MeshImportInputs directory;
        const std::filesystem::path source = directory.write_text("corrupted.gltf", "not glTF");

        const auto result = MeshImporter{}.import(source);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().find(source.string()), std::string::npos);
    }

    TEST(MeshImporterTest, ReturnsFailureForNonFiniteVerticesAndInvalidIndices) {
        const MeshImportInputs directory;
        const auto buffer = directory.write_triangle_buffer("triangle.bin");
        const auto source = directory.write_text("triangle.gltf",
            make_triangle_gltf(R"({"attributes":{"POSITION":0},"indices":1})", "triangle.bin"));
        {
            std::fstream output(buffer, std::ios::binary | std::ios::in | std::ios::out);
            const char nan[] = {0, 0, static_cast<char>(0xc0), 0x7f};
            output.write(nan, sizeof(nan));
        }
        const auto non_finite = MeshImporter{}.import(source);
        ASSERT_FALSE(non_finite);
        EXPECT_NE(
            non_finite.error().find("POSITION contains a non-finite value"), std::string::npos);

        static_cast<void>(directory.write_triangle_buffer("triangle.bin"));
        {
            std::fstream output(buffer, std::ios::binary | std::ios::in | std::ios::out);
            output.seekp(36);
            const char index[] = {3, 0};
            output.write(index, sizeof(index));
        }
        const auto invalid_index = MeshImporter{}.import(source);
        ASSERT_FALSE(invalid_index);
        EXPECT_NE(invalid_index.error().find("out-of-range vertex index"), std::string::npos);
    }
}

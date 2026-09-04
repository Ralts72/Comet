#include "asset/import/mesh_importer.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Comet {
    namespace {
        [[noreturn]] void fail_import(
            const std::filesystem::path& source_path, const std::string_view message) {
            throw std::runtime_error("Failed to import mesh '" + source_path.string()
                                     + "': " + std::string(message));
        }

        [[nodiscard]] const fastgltf::Accessor& get_accessor(const fastgltf::Asset& asset,
            const std::size_t index, const std::filesystem::path& source_path,
            const std::string_view semantic) {
            if(index >= asset.accessors.size()) {
                fail_import(source_path,
                    std::string(semantic) + " references an invalid accessor");
            }
            return asset.accessors[index];
        }

        [[nodiscard]] bool is_finite(const Math::Vec2& value) {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool is_finite(const Math::Vec3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y)
                   && std::isfinite(value.z);
        }

        void generate_normals(MeshData& data, const std::size_t first_vertex,
            const std::size_t vertex_count, const std::size_t first_index,
            const std::filesystem::path& source_path) {
            const std::size_t index_count = data.indices.size() - first_index;
            if(index_count % 3 != 0) {
                fail_import(source_path,
                    "triangle primitive index count must be divisible by three");
            }

            for(std::size_t index = first_index; index < data.indices.size();
                index += 3) {
                const std::size_t i0 = data.indices[index];
                const std::size_t i1 = data.indices[index + 1];
                const std::size_t i2 = data.indices[index + 2];
                const std::size_t vertex_end = first_vertex + vertex_count;
                if(i0 < first_vertex || i0 >= vertex_end || i1 < first_vertex
                    || i1 >= vertex_end || i2 < first_vertex || i2 >= vertex_end) {
                    fail_import(
                        source_path, "primitive contains an out-of-range vertex index");
                }

                const Math::Vec3 edge_a =
                    data.vertices[i1].position - data.vertices[i0].position;
                const Math::Vec3 edge_b =
                    data.vertices[i2].position - data.vertices[i0].position;
                const Math::Vec3 face_normal = Math::cross(edge_a, edge_b);
                data.vertices[i0].normal += face_normal;
                data.vertices[i1].normal += face_normal;
                data.vertices[i2].normal += face_normal;
            }

            constexpr float NORMAL_EPSILON = 1.0e-12f;
            for(std::size_t index = first_vertex; index < first_vertex + vertex_count;
                ++index) {
                Math::Vec3& normal = data.vertices[index].normal;
                const float length = Math::length(normal);
                normal = std::isfinite(length) && length > NORMAL_EPSILON
                             ? normal / length
                             : Math::Vec3(0.0f, 1.0f, 0.0f);
            }
        }

        [[nodiscard]] std::vector<std::filesystem::path> collect_source_dependencies(
            const std::filesystem::path& source_path) {
            auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
            if(!source) {
                fail_import(source_path, fastgltf::getErrorMessage(source.error()));
            }

            fastgltf::Parser parser;
            auto parsed = parser.loadGltf(source.get(), source_path.parent_path(),
                fastgltf::Options::None, fastgltf::Category::Buffers);
            if(!parsed) {
                fail_import(source_path, fastgltf::getErrorMessage(parsed.error()));
            }

            std::vector<std::filesystem::path> dependencies;
            for(const fastgltf::Buffer& buffer : parsed.get().buffers) {
                const auto* uri = std::get_if<fastgltf::sources::URI>(&buffer.data);
                if(!uri || !uri->uri.isLocalPath()) {
                    continue;
                }

                const std::filesystem::path dependency =
                    (source_path.parent_path() / uri->uri.fspath()).lexically_normal();
                if(std::ranges::find(dependencies, dependency) == dependencies.end()) {
                    dependencies.push_back(dependency);
                }
            }
            return dependencies;
        }
    }

    MeshData MeshImporter::import(const std::filesystem::path& source_path) const {
        auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
        if(!source) {
            fail_import(source_path, fastgltf::getErrorMessage(source.error()));
        }

        constexpr auto options = fastgltf::Options::LoadExternalBuffers;
        fastgltf::Parser parser;
        auto parsed = parser.loadGltf(
            source.get(), source_path.parent_path(), options, fastgltf::Category::Meshes);
        if(!parsed) {
            fail_import(source_path, fastgltf::getErrorMessage(parsed.error()));
        }

        fastgltf::Asset asset = std::move(parsed.get());
        const fastgltf::Error validation_error = fastgltf::validate(asset);
        if(validation_error != fastgltf::Error::None) {
            fail_import(source_path, fastgltf::getErrorMessage(validation_error));
        }
        if(asset.meshes.size() != 1) {
            fail_import(
                source_path, "exactly one glTF mesh is required per Comet Mesh asset");
        }

        MeshData data;
        const fastgltf::Mesh& mesh = asset.meshes.front();
        for(const fastgltf::Primitive& primitive : mesh.primitives) {
            if(primitive.type != fastgltf::PrimitiveType::Triangles) {
                fail_import(source_path, "only triangle-list primitives are supported");
            }

            const auto* position_attribute = primitive.findAttribute("POSITION");
            if(position_attribute == primitive.attributes.end()) {
                fail_import(
                    source_path, "each primitive must provide a POSITION attribute");
            }
            const fastgltf::Accessor& position_accessor = get_accessor(
                asset, position_attribute->accessorIndex, source_path, "POSITION");
            if(position_accessor.type != fastgltf::AccessorType::Vec3
                || position_accessor.count == 0) {
                fail_import(source_path, "POSITION must be a non-empty VEC3 accessor");
            }

            const std::size_t first_vertex = data.vertices.size();
            if(position_accessor.count
                > std::numeric_limits<std::uint32_t>::max() - first_vertex) {
                fail_import(source_path, "vertex count exceeds 32-bit indices");
            }
            data.vertices.resize(first_vertex + position_accessor.count);
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset,
                position_accessor,
                [&](const fastgltf::math::fvec3 position, const std::size_t index) {
                    const Math::Vec3 value(position.x(), position.y(), position.z());
                    if(!is_finite(value)) {
                        fail_import(source_path, "POSITION contains a non-finite value");
                    }
                    data.vertices[first_vertex + index].position = value;
                });

            bool has_normals = false;
            if(const auto* normal_attribute = primitive.findAttribute("NORMAL");
                normal_attribute != primitive.attributes.end()) {
                const fastgltf::Accessor& normal_accessor = get_accessor(
                    asset, normal_attribute->accessorIndex, source_path, "NORMAL");
                if(normal_accessor.type != fastgltf::AccessorType::Vec3
                    || normal_accessor.count != position_accessor.count) {
                    fail_import(source_path,
                        "NORMAL must be a VEC3 accessor matching POSITION count");
                }
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset,
                    normal_accessor,
                    [&](const fastgltf::math::fvec3 normal, const std::size_t index) {
                        const Math::Vec3 value(normal.x(), normal.y(), normal.z());
                        if(!is_finite(value)) {
                            fail_import(
                                source_path, "NORMAL contains a non-finite value");
                        }
                        data.vertices[first_vertex + index].normal = value;
                    });
                has_normals = true;
            }

            if(const auto* texcoord_attribute = primitive.findAttribute("TEXCOORD_0");
                texcoord_attribute != primitive.attributes.end()) {
                const fastgltf::Accessor& texcoord_accessor = get_accessor(
                    asset, texcoord_attribute->accessorIndex, source_path, "TEXCOORD_0");
                if(texcoord_accessor.type != fastgltf::AccessorType::Vec2
                    || texcoord_accessor.count != position_accessor.count) {
                    fail_import(source_path,
                        "TEXCOORD_0 must be a VEC2 accessor matching POSITION count");
                }
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset,
                    texcoord_accessor,
                    [&](const fastgltf::math::fvec2 texcoord, const std::size_t index) {
                        const Math::Vec2 value(texcoord.x(), texcoord.y());
                        if(!is_finite(value)) {
                            fail_import(
                                source_path, "TEXCOORD_0 contains a non-finite value");
                        }
                        data.vertices[first_vertex + index].texcoord = value;
                    });
            }

            const std::size_t first_index = data.indices.size();
            if(primitive.indicesAccessor.has_value()) {
                const fastgltf::Accessor& index_accessor = get_accessor(
                    asset, primitive.indicesAccessor.value(), source_path, "indices");
                if(index_accessor.type != fastgltf::AccessorType::Scalar
                    || (index_accessor.componentType
                            != fastgltf::ComponentType::UnsignedByte
                        && index_accessor.componentType
                               != fastgltf::ComponentType::UnsignedShort
                        && index_accessor.componentType
                               != fastgltf::ComponentType::UnsignedInt)) {
                    fail_import(
                        source_path, "indices must use an unsigned scalar accessor");
                }
                if(index_accessor.count
                    > std::numeric_limits<std::uint32_t>::max() - data.indices.size()) {
                    fail_import(source_path, "index count exceeds 32-bit draw limits");
                }
                data.indices.reserve(data.indices.size() + index_accessor.count);
                fastgltf::iterateAccessor<std::uint32_t>(
                    asset, index_accessor, [&](const std::uint32_t local_index) {
                        if(local_index >= position_accessor.count) {
                            fail_import(source_path,
                                "primitive contains an out-of-range vertex index");
                        }
                        data.indices.push_back(
                            static_cast<std::uint32_t>(first_vertex) + local_index);
                    });
            } else {
                if(position_accessor.count
                    > std::numeric_limits<std::uint32_t>::max() - data.indices.size()) {
                    fail_import(source_path, "index count exceeds 32-bit draw limits");
                }
                data.indices.reserve(data.indices.size() + position_accessor.count);
                for(std::size_t index = 0; index < position_accessor.count; ++index) {
                    data.indices.push_back(
                        static_cast<std::uint32_t>(first_vertex + index));
                }
            }

            if((data.indices.size() - first_index) % 3 != 0) {
                fail_import(source_path,
                    "triangle primitive index count must be divisible by three");
            }
            if(!has_normals) {
                generate_normals(data, first_vertex, position_accessor.count, first_index,
                    source_path);
            }
        }

        if(data.vertices.empty() || data.indices.empty()) {
            fail_import(source_path, "mesh contains no drawable triangles");
        }
        return data;
    }

    MeshImportResult MeshImporter::import_with_dependencies(
        const std::filesystem::path& source_path) const {
        std::vector<std::filesystem::path> dependencies =
            collect_source_dependencies(source_path);
        return {
            .data = import(source_path), .source_dependencies = std::move(dependencies)};
    }
}

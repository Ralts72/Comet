#include "asset/import/mesh_geometry.h"

#include <mikktspace.h>

#include <algorithm>
#include <limits>
#include <tuple>

namespace Comet {
    namespace {
        struct Corner {
            uint32_t source;
            uint32_t index;
            Math::Vec3 normal;
            Math::Vec4 tangent;

            auto key() const {
                return std::tie(source, normal.x, normal.y, normal.z, tangent.x, tangent.y,
                    tangent.z, tangent.w);
            }
        };

        struct TangentContext {
            const MeshData& mesh;
            std::vector<Corner>& corners;

            static TangentContext& get(const SMikkTSpaceContext* context) {
                return *static_cast<TangentContext*>(context->m_pUserData);
            }
            Corner& corner(int face, int vertex) { return corners[std::size_t(face) * 3 + vertex]; }
        };

        bool generate_tangents(const MeshData& mesh, std::vector<Corner>& corners) {
            SMikkTSpaceInterface callbacks{};
            callbacks.m_getNumFaces = [](const SMikkTSpaceContext* context) {
                return int(TangentContext::get(context).corners.size() / 3);
            };
            callbacks.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) { return 3; };
            callbacks.m_getPosition = [](const SMikkTSpaceContext* context, float output[],
                                          int face, int vertex) {
                auto& data = TangentContext::get(context);
                const auto position = data.mesh.vertices[data.corner(face, vertex).source].position;
                for(int i = 0; i < 3; ++i)
                    output[i] = position[i];
            };
            callbacks.m_getNormal = [](const SMikkTSpaceContext* context, float output[], int face,
                                        int vertex) {
                const auto normal = TangentContext::get(context).corner(face, vertex).normal;
                for(int i = 0; i < 3; ++i)
                    output[i] = normal[i];
            };
            callbacks.m_getTexCoord = [](const SMikkTSpaceContext* context, float output[],
                                          int face, int vertex) {
                auto& data = TangentContext::get(context);
                const auto uv = data.mesh.vertices[data.corner(face, vertex).source].texcoord;
                for(int i = 0; i < 2; ++i)
                    output[i] = uv[i];
            };
            callbacks.m_setTSpaceBasic = [](const SMikkTSpaceContext* context,
                                             const float tangent[], float sign, int face,
                                             int vertex) {
                TangentContext::get(context).corner(face, vertex).tangent = {
                    tangent[0], tangent[1], tangent[2], sign};
            };
            TangentContext data{mesh, corners};
            const SMikkTSpaceContext context{&callbacks, &data};
            return genTangSpaceDefault(&context) != 0;
        }
    }

    Result<void> complete_mesh_geometry(
        MeshData& mesh, std::span<const MeshPrimitiveRange> primitives) {
        for(const auto& primitive : primitives) {
            if(!primitive.generate_normals && !primitive.generate_tangents)
                continue;
            // MikkTSpace 使用有符号面索引及两个低位存储面角编号。
            if(primitive.index_count > std::size_t(std::numeric_limits<int>::max()) / 4)
                return Result<void>::failure("Too many mesh corners for tangent generation");
            std::vector<Corner> corners;
            corners.reserve(primitive.index_count);
            for(std::size_t offset = 0; offset < primitive.index_count; offset += 3) {
                const auto first = primitive.first_index + offset;
                auto normal = Math::Vec3(0);
                if(primitive.generate_normals) {
                    const auto a = mesh.vertices[mesh.indices[first]].position;
                    const auto b = mesh.vertices[mesh.indices[first + 1]].position;
                    const auto c = mesh.vertices[mesh.indices[first + 2]].position;
                    normal = glm::cross(b - a, c - a);
                    const float length = glm::length(normal);
                    if(std::isfinite(length) && length > 1e-12f)
                        normal /= length;
                    else
                        normal = Math::Vec3(0, 1, 0);
                }
                for(uint32_t corner = 0; corner < 3; ++corner) {
                    const auto index = uint32_t(first + corner);
                    const auto source = mesh.indices[index];
                    const auto& vertex = mesh.vertices[source];
                    corners.push_back({source, index,
                        primitive.generate_normals ? normal : vertex.normal, vertex.tangent});
                }
            }
            if(primitive.generate_tangents && !generate_tangents(mesh, corners))
                return Result<void>::failure("MikkTSpace could not generate mesh tangents");
            for(const auto& corner : corners)
                if(!Math::is_finite(corner.normal) || !Math::is_finite(corner.tangent))
                    return Result<void>::failure("Generated mesh geometry is not finite");

            // 先按源顶点与属性分组，保留原顶点身份，只为不同面角属性新增顶点。
            std::ranges::sort(
                corners, [](const Corner& a, const Corner& b) { return a.key() < b.key(); });
            uint32_t current = 0;
            for(std::size_t index = 0; index < corners.size(); ++index) {
                const auto& corner = corners[index];
                if(index == 0 || corner.key() != corners[index - 1].key()) {
                    current = corner.source;
                    if(index != 0 && corner.source == corners[index - 1].source) {
                        if(mesh.vertices.size() >= std::numeric_limits<uint32_t>::max())
                            return Result<void>::failure(
                                "Generated mesh exceeds 32-bit vertex indices");
                        current = uint32_t(mesh.vertices.size());
                        const auto vertex = mesh.vertices[corner.source];
                        mesh.vertices.push_back(vertex);
                        if(!mesh.skin.empty()) {
                            const auto skin = mesh.skin[corner.source];
                            mesh.skin.push_back(skin);
                        }
                    }
                    mesh.vertices[current].normal = corner.normal;
                    mesh.vertices[current].tangent = corner.tangent;
                }
                mesh.indices[corner.index] = current;
            }
        }
        return Result<void>::success();
    }
}

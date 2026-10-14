#include "asset/import/gltf_mesh.h"
#include "asset/import/mesh_geometry.h"

#include <fastgltf/tools.hpp>
#include <algorithm>
#include <limits>
#include <span>

namespace Comet {
    namespace {

        std::string import_error(const std::filesystem::path& path, std::string_view message) {
            return "Failed to import mesh '" + path.string() + "': " + std::string(message);
        }

        Result<void> validate_selection(
            const fastgltf::Asset& asset, const GltfMeshSelection& selection) {
            if(selection.mesh >= asset.meshes.size())
                return Result<void>::failure("Invalid glTF mesh selection");
            if(selection.primitive
                && *selection.primitive >= asset.meshes[selection.mesh].primitives.size())
                return Result<void>::failure("Invalid glTF primitive selection");
            if(selection.skin_node) {
                if(*selection.skin_node >= asset.nodes.size())
                    return Result<void>::failure("Invalid glTF skin node selection");
                const auto& node = asset.nodes[*selection.skin_node];
                if(!node.meshIndex || *node.meshIndex != selection.mesh || !node.skinIndex
                    || *node.skinIndex >= asset.skins.size())
                    return Result<void>::failure(
                        "Selected glTF node does not bind this mesh to a skin");
            }
            return Result<void>::success();
        }

        std::span<const fastgltf::Primitive> selected_primitives(
            const fastgltf::Asset& asset, const GltfMeshSelection& selection) {
            const auto& primitives = asset.meshes[selection.mesh].primitives;
            const std::span<const fastgltf::Primitive> range(primitives.data(), primitives.size());
            return selection.primitive ? range.subspan(*selection.primitive, 1) : range;
        }

        Result<std::shared_ptr<const AnimationData>> import_animation(
            const fastgltf::Asset& asset, const GltfMeshSelection& selection, MeshData& mesh) {
            using Imported = Result<std::shared_ptr<const AnimationData>>;
            if(!selection.skin_node)
                return Imported::success(nullptr);
            auto data = std::make_shared<AnimationData>();
            data->mesh_node = uint32_t(*selection.skin_node);
            data->nodes.resize(asset.nodes.size());
            for(std::size_t index = 0; index < asset.nodes.size(); ++index) {
                const auto& source = asset.nodes[index];
                auto& node = data->nodes[index];
                node.name = source.name;
                const auto* trs = std::get_if<fastgltf::TRS>(&source.transform);
                if(!trs)
                    return Imported::failure("Cannot decompose skeleton node transform");
                node.rest.translation = {
                    trs->translation[0], trs->translation[1], trs->translation[2]};
                node.rest.scale = {trs->scale[0], trs->scale[1], trs->scale[2]};
                node.rest.rotation = {
                    trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]};
                for(const auto child : source.children) {
                    if(child >= data->nodes.size()
                        || data->nodes[child].parent != NO_ANIMATION_NODE)
                        return Imported::failure("Invalid skeleton node hierarchy");
                    data->nodes[child].parent = uint32_t(index);
                }
            }
            std::vector<uint8_t> visited(data->nodes.size());
            const auto visit = [&](auto&& self, uint32_t index) -> bool {
                if(visited[index] == 1)
                    return false;
                if(visited[index] == 2)
                    return true;
                visited[index] = 1;
                const auto parent = data->nodes[index].parent;
                if(parent != NO_ANIMATION_NODE && !self(self, parent))
                    return false;
                visited[index] = 2;
                data->evaluation_order.push_back(index);
                return true;
            };
            for(uint32_t index = 0; index < data->nodes.size(); ++index)
                if(!visit(visit, index))
                    return Imported::failure("Skeleton contains a cycle");
            const auto& skin = asset.skins[*asset.nodes[*selection.skin_node].skinIndex];
            for(const auto node : skin.joints)
                data->joints.push_back({.node = uint32_t(node)});
            if(skin.inverseBindMatrices) {
                const auto& accessor = asset.accessors[*skin.inverseBindMatrices];
                if(accessor.type != fastgltf::AccessorType::Mat4
                    || accessor.count != data->joints.size())
                    return Imported::failure("Invalid inverse bind matrix accessor");
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fmat4x4>(
                    asset, accessor, [&](const auto& value, std::size_t index) {
                        for(int column = 0; column < 4; ++column)
                            for(int row = 0; row < 4; ++row)
                                data->joints[index].inverse_bind[column][row] = value[column][row];
                    });
            }
            for(std::size_t index = 0; index < asset.animations.size(); ++index) {
                const auto& source = asset.animations[index];
                AnimationClip clip;
                clip.name = source.name;
                if(clip.name.empty())
                    clip.name = "clip_" + std::to_string(index);
                for(const auto& input : source.channels) {
                    if(!input.nodeIndex || *input.nodeIndex >= data->nodes.size()
                        || input.samplerIndex >= source.samplers.size())
                        return Imported::failure("Invalid animation channel target");
                    AnimationChannel channel;
                    channel.node = uint32_t(*input.nodeIndex);
                    switch(input.path) {
                        case fastgltf::AnimationPath::Translation:
                            channel.path = AnimationChannel::Path::Translation;
                            break;
                        case fastgltf::AnimationPath::Rotation:
                            channel.path = AnimationChannel::Path::Rotation;
                            break;
                        case fastgltf::AnimationPath::Scale:
                            channel.path = AnimationChannel::Path::Scale;
                            break;
                        default:
                            return Imported::failure("Morph animation is not supported yet");
                    }
                    const auto& sampler = source.samplers[input.samplerIndex];
                    switch(sampler.interpolation) {
                        case fastgltf::AnimationInterpolation::Linear:
                            channel.interpolation = AnimationChannel::Interpolation::Linear;
                            break;
                        case fastgltf::AnimationInterpolation::Step:
                            channel.interpolation = AnimationChannel::Interpolation::Step;
                            break;
                        case fastgltf::AnimationInterpolation::CubicSpline:
                            channel.interpolation = AnimationChannel::Interpolation::CubicSpline;
                            break;
                        default:
                            return Imported::failure("Unsupported animation interpolation");
                    }
                    const auto& times = asset.accessors[sampler.inputAccessor];
                    const auto& values = asset.accessors[sampler.outputAccessor];
                    const bool rotation = channel.path == AnimationChannel::Path::Rotation;
                    if(times.type != fastgltf::AccessorType::Scalar
                        || values.type
                               != (rotation ? fastgltf::AccessorType::Vec4
                                            : fastgltf::AccessorType::Vec3))
                        return Imported::failure("Invalid animation key accessor");
                    channel.times.reserve(times.count);
                    channel.values.reserve(values.count);
                    fastgltf::iterateAccessor<float>(asset, times, [&](float value) {
                        channel.times.push_back(value);
                        clip.duration = std::max(clip.duration, value);
                    });
                    if(rotation) {
                        fastgltf::iterateAccessor<fastgltf::math::fvec4>(
                            asset, values, [&](const auto& value) {
                                channel.values.emplace_back(value[0], value[1], value[2], value[3]);
                            });
                    } else {
                        fastgltf::iterateAccessor<fastgltf::math::fvec3>(
                            asset, values, [&](const auto& value) {
                                channel.values.emplace_back(value[0], value[1], value[2], 0);
                            });
                    }
                    clip.channels.push_back(std::move(channel));
                }
                data->clips.push_back(std::move(clip));
            }
            mesh.skin.resize(mesh.vertices.size());
            std::size_t first = 0;
            for(const auto& primitive : selected_primitives(asset, selection)) {
                const auto joints = primitive.findAttribute("JOINTS_0"),
                           weights = primitive.findAttribute("WEIGHTS_0");
                if(joints == primitive.attributes.end() || weights == primitive.attributes.end())
                    return Imported::failure(
                        "Every skinned primitive requires JOINTS_0 and WEIGHTS_0");
                const auto& joint_accessor = asset.accessors[joints->accessorIndex];
                const auto& weight_accessor = asset.accessors[weights->accessorIndex];
                const auto vertices =
                    asset.accessors[primitive.findAttribute("POSITION")->accessorIndex].count;
                if(joint_accessor.type != fastgltf::AccessorType::Vec4
                    || weight_accessor.type != fastgltf::AccessorType::Vec4
                    || joint_accessor.count != vertices || weight_accessor.count != vertices)
                    return Imported::failure("Invalid joint or weight accessor");
                if(primitive.findAttribute("JOINTS_1") != primitive.attributes.end())
                    return Imported::failure("Only four bone influences per vertex are supported");
                fastgltf::iterateAccessorWithIndex<fastgltf::math::uvec4>(
                    asset, joint_accessor, [&](const auto& value, std::size_t index) {
                        for(int slot = 0; slot < 4; ++slot)
                            mesh.skin[first + index].joints[slot] = value[slot];
                    });
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                    asset, weight_accessor, [&](const auto& value, std::size_t index) {
                        mesh.skin[first + index].weights = {value[0], value[1], value[2], value[3]};
                    });
                first += vertices;
            }
            for(std::size_t index = 0; index < mesh.skin.size(); ++index) {
                auto& influence = mesh.skin[index];
                const float sum = glm::dot(influence.weights, Math::Vec4(1));
                if(!Math::is_finite(influence.weights) || sum < 0.0001f
                    || glm::any(glm::lessThan(influence.weights, Math::Vec4(0))))
                    return Imported::failure("Invalid skin weights");
                influence.weights /= sum;
                for(int slot = 0; slot < 4; ++slot) {
                    if(influence.joints[slot] >= data->joints.size())
                        return Imported::failure("Vertex references an invalid joint");
                    if(influence.weights[slot] == 0)
                        continue;
                    auto& bounds = data->joints[influence.joints[slot]].bounds;
                    if(bounds)
                        bounds->include(mesh.vertices[index].position);
                    else
                        bounds = BoundingBox::from_point(mesh.vertices[index].position);
                }
            }
            if(auto valid = validate_animation(*data); !valid)
                return Imported::failure(valid.error());
            return Imported::success(std::move(data));
        }

    }

    Result<GltfMeshSelection> standalone_mesh_selection(const fastgltf::Asset& asset) {
        using Selected = Result<GltfMeshSelection>;
        if(asset.meshes.size() != 1)
            return Selected::failure("exactly one glTF mesh is required per Comet Mesh asset");
        if(asset.skins.size() > 1)
            return Selected::failure("One skin is supported per Mesh asset");
        GltfMeshSelection selection;
        if(asset.skins.empty()) {
            for(const auto& primitive : asset.meshes.front().primitives)
                if(primitive.findAttribute("JOINTS_0") != primitive.attributes.end())
                    return Selected::failure("Joint attributes require a skin");
            return Selected::success(selection);
        }
        for(std::size_t index = 0; index < asset.nodes.size(); ++index) {
            const auto& node = asset.nodes[index];
            if(!node.meshIndex)
                continue;
            if(*node.meshIndex != 0 || !node.skinIndex || *node.skinIndex != 0
                || selection.skin_node)
                return Selected::failure("A skinned Mesh asset requires one mesh node");
            selection.skin_node = index;
        }
        if(!selection.skin_node)
            return Selected::failure("A skinned Mesh asset requires one mesh node");
        return Selected::success(selection);
    }

    Result<std::size_t> estimate_gltf_mesh(const fastgltf::Asset& asset,
        const GltfMeshSelection& selection, const std::size_t source_bytes,
        const AssetImportLimits& limits) {
        if(limits.mesh_working_bytes <= GLTF_IMPORT_OVERHEAD_BYTES)
            return Result<std::size_t>::failure("Mesh working budget is too small");
        if(auto valid = validate_selection(asset, selection); !valid)
            return Result<std::size_t>::failure(valid.error());
        const std::size_t available = limits.mesh_working_bytes - GLTF_IMPORT_OVERHEAD_BYTES;
        if(source_bytes > available / 3)
            return Result<std::size_t>::failure(
                "Mesh exceeds its configured CPU working-set budget");
        const std::size_t output_budget = (available - source_bytes * 3) / 4;
        std::size_t output_bytes = 0;
        const auto add_output = [&](std::size_t count, std::size_t size) {
            if(count > (output_budget - output_bytes) / size)
                return false;
            output_bytes += count * size;
            return true;
        };
        if(selection.skin_node) {
            if(!add_output(asset.nodes.size(), sizeof(AnimationNode) + sizeof(uint32_t))
                || !add_output(
                    asset.skins[*asset.nodes[*selection.skin_node].skinIndex].joints.size(),
                    sizeof(SkinJoint))
                || !add_output(asset.animations.size(), sizeof(AnimationClip)))
                return Result<std::size_t>::failure("Skeleton exceeds its CPU working-set budget");
            for(const auto& node : asset.nodes)
                if(!add_output(node.name.size(), 1))
                    return Result<std::size_t>::failure(
                        "Skeleton names exceed the working-set budget");
            for(const auto& clip : asset.animations) {
                if(!add_output(clip.channels.size(), sizeof(AnimationChannel))
                    || !add_output(clip.name.size(), 1))
                    return Result<std::size_t>::failure(
                        "Animation exceeds its CPU working-set budget");
                for(const auto& channel : clip.channels) {
                    if(channel.samplerIndex >= clip.samplers.size())
                        return Result<std::size_t>::failure("Invalid animation sampler");
                    const auto& sampler = clip.samplers[channel.samplerIndex];
                    if(sampler.inputAccessor >= asset.accessors.size()
                        || sampler.outputAccessor >= asset.accessors.size())
                        return Result<std::size_t>::failure("Invalid animation accessor");
                    if(!add_output(asset.accessors[sampler.inputAccessor].count, sizeof(float))
                        || !add_output(
                            asset.accessors[sampler.outputAccessor].count, sizeof(Math::Vec4)))
                        return Result<std::size_t>::failure(
                            "Animation keys exceed the CPU working-set budget");
                }
            }
        }
        const std::size_t vertex_bytes =
            sizeof(MeshVertex) + (selection.skin_node ? sizeof(SkinInfluences) : 0);
        for(const fastgltf::Primitive& primitive : selected_primitives(asset, selection)) {
            const auto* position = primitive.findAttribute("POSITION");
            if(position == primitive.attributes.end()
                || position->accessorIndex >= asset.accessors.size())
                return Result<std::size_t>::failure("Mesh has no valid POSITION accessor");
            const std::size_t vertices = asset.accessors[position->accessorIndex].count;
            std::size_t indices = vertices;
            if(primitive.indicesAccessor) {
                if(*primitive.indicesAccessor >= asset.accessors.size())
                    return Result<std::size_t>::failure("Mesh has an invalid index accessor");
                indices = asset.accessors[*primitive.indicesAccessor].count;
            }
            if(vertices == 0 || indices == 0
                || vertices > (output_budget - output_bytes) / vertex_bytes)
                return Result<std::size_t>::failure(
                    "Mesh exceeds its configured CPU working-set budget");
            output_bytes += vertices * vertex_bytes;
            if(indices > (output_budget - output_bytes) / sizeof(std::uint32_t))
                return Result<std::size_t>::failure(
                    "Mesh exceeds its configured CPU working-set budget");
            output_bytes += indices * sizeof(std::uint32_t);
            const bool generated_normals =
                primitive.findAttribute("NORMAL") == primitive.attributes.end();
            const bool generated_tangents =
                primitive.findAttribute("TEXCOORD_0") != primitive.attributes.end()
                && (generated_normals
                    || primitive.findAttribute("TANGENT") == primitive.attributes.end());
            // 接缝最坏每个面角产生一个顶点；另为 MikkTSpace 和排序临时数组预留空间。
            if((generated_normals || generated_tangents) && !add_output(indices, vertex_bytes + 64))
                return Result<std::size_t>::failure(
                    "Generated mesh geometry exceeds its CPU working-set budget");
        }
        return Result<std::size_t>::success(
            source_bytes * 3 + output_bytes * 4 + GLTF_IMPORT_OVERHEAD_BYTES);
    }

    Result<MeshData> import_gltf_mesh(const fastgltf::Asset& asset,
        const GltfMeshSelection& selection, const std::filesystem::path& source_path) {
        if(auto valid = validate_selection(asset, selection); !valid)
            return Result<MeshData>::failure(valid.error());
        MeshData data;
        std::vector<MeshPrimitiveRange> primitives;
        for(const fastgltf::Primitive& primitive : selected_primitives(asset, selection)) {
            if(primitive.type != fastgltf::PrimitiveType::Triangles) {
                return Result<MeshData>::failure(
                    import_error(source_path, "only triangle-list primitives are supported"));
            }

            const auto* position_attribute = primitive.findAttribute("POSITION");
            if(position_attribute == primitive.attributes.end()) {
                return Result<MeshData>::failure(
                    import_error(source_path, "each primitive must provide a POSITION attribute"));
            }
            if(position_attribute->accessorIndex >= asset.accessors.size())
                return Result<MeshData>::failure(
                    import_error(source_path, "POSITION references an invalid accessor"));
            const fastgltf::Accessor& position_accessor =
                asset.accessors[position_attribute->accessorIndex];
            if(position_accessor.type != fastgltf::AccessorType::Vec3
                || position_accessor.count == 0) {
                return Result<MeshData>::failure(
                    import_error(source_path, "POSITION must be a non-empty VEC3 accessor"));
            }

            const std::size_t first_vertex = data.vertices.size();
            if(position_accessor.count > std::numeric_limits<std::uint32_t>::max() - first_vertex) {
                return Result<MeshData>::failure(
                    import_error(source_path, "vertex count exceeds 32-bit indices"));
            }
            data.vertices.resize(first_vertex + position_accessor.count);
            bool invalid_attribute = false;
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, position_accessor,
                [&](const fastgltf::math::fvec3 position, const std::size_t index) {
                    const Math::Vec3 value(position.x(), position.y(), position.z());
                    if(!Math::is_finite(value)) {
                        invalid_attribute = true;
                        return;
                    }
                    data.vertices[first_vertex + index].position = value;
                });
            if(invalid_attribute)
                return Result<MeshData>::failure(
                    import_error(source_path, "POSITION contains a non-finite value"));

            bool has_normals = false;
            if(const auto* normal_attribute = primitive.findAttribute("NORMAL");
                normal_attribute != primitive.attributes.end()) {
                if(normal_attribute->accessorIndex >= asset.accessors.size())
                    return Result<MeshData>::failure(
                        import_error(source_path, "NORMAL references an invalid accessor"));
                const fastgltf::Accessor& normal_accessor =
                    asset.accessors[normal_attribute->accessorIndex];
                if(normal_accessor.type != fastgltf::AccessorType::Vec3
                    || normal_accessor.count != position_accessor.count) {
                    return Result<MeshData>::failure(import_error(
                        source_path, "NORMAL must be a VEC3 accessor matching POSITION count"));
                }
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, normal_accessor,
                    [&](const fastgltf::math::fvec3 normal, const std::size_t index) {
                        const Math::Vec3 value(normal.x(), normal.y(), normal.z());
                        if(!Math::is_finite(value)) {
                            invalid_attribute = true;
                            return;
                        }
                        data.vertices[first_vertex + index].normal = value;
                    });
                if(invalid_attribute)
                    return Result<MeshData>::failure(
                        import_error(source_path, "NORMAL contains a non-finite value"));
                has_normals = true;
            }

            if(const auto* texcoord_attribute = primitive.findAttribute("TEXCOORD_0");
                texcoord_attribute != primitive.attributes.end()) {
                if(texcoord_attribute->accessorIndex >= asset.accessors.size())
                    return Result<MeshData>::failure(
                        import_error(source_path, "TEXCOORD_0 references an invalid accessor"));
                const fastgltf::Accessor& texcoord_accessor =
                    asset.accessors[texcoord_attribute->accessorIndex];
                if(texcoord_accessor.type != fastgltf::AccessorType::Vec2
                    || texcoord_accessor.count != position_accessor.count) {
                    return Result<MeshData>::failure(import_error(
                        source_path, "TEXCOORD_0 must be a VEC2 accessor matching POSITION count"));
                }
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, texcoord_accessor,
                    [&](const fastgltf::math::fvec2 texcoord, const std::size_t index) {
                        const Math::Vec2 value(texcoord.x(), texcoord.y());
                        if(!Math::is_finite(value)) {
                            invalid_attribute = true;
                            return;
                        }
                        data.vertices[first_vertex + index].texcoord = value;
                    });
                if(invalid_attribute)
                    return Result<MeshData>::failure(
                        import_error(source_path, "TEXCOORD_0 contains a non-finite value"));
            }

            // glTF 在缺少法线时要求忽略源切线。
            if(const auto* attribute = primitive.findAttribute("TANGENT");
                has_normals && attribute != primitive.attributes.end()) {
                if(attribute->accessorIndex >= asset.accessors.size())
                    return Result<MeshData>::failure(
                        import_error(source_path, "TANGENT references an invalid accessor"));
                const auto& accessor = asset.accessors[attribute->accessorIndex];
                if(accessor.type != fastgltf::AccessorType::Vec4
                    || accessor.componentType != fastgltf::ComponentType::Float
                    || accessor.count != position_accessor.count)
                    return Result<MeshData>::failure(import_error(source_path,
                        "TANGENT must be a float VEC4 accessor matching POSITION count"));
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                    asset, accessor, [&](const auto& value, std::size_t index) {
                        const Math::Vec4 tangent(value[0], value[1], value[2], value[3]);
                        const auto normal = data.vertices[first_vertex + index].normal;
                        const auto direction = Math::Vec3(tangent);
                        if(!Math::is_finite(tangent) || std::abs(tangent.w) != 1.f
                            || glm::dot(direction, direction) < 1e-12f
                            || glm::dot(
                                   glm::cross(normal, direction), glm::cross(normal, direction))
                                   < 1e-12f) {
                            invalid_attribute = true;
                            return;
                        }
                        data.vertices[first_vertex + index].tangent = tangent;
                    });
                if(invalid_attribute)
                    return Result<MeshData>::failure(import_error(source_path,
                        "TANGENT requires a finite direction independent of NORMAL and a sign of -1 or 1"));
            }

            const std::size_t first_index = data.indices.size();
            if(primitive.indicesAccessor.has_value()) {
                if(primitive.indicesAccessor.value() >= asset.accessors.size())
                    return Result<MeshData>::failure(
                        import_error(source_path, "indices references an invalid accessor"));
                const fastgltf::Accessor& index_accessor =
                    asset.accessors[primitive.indicesAccessor.value()];
                if(index_accessor.type != fastgltf::AccessorType::Scalar
                    || (index_accessor.componentType != fastgltf::ComponentType::UnsignedByte
                        && index_accessor.componentType != fastgltf::ComponentType::UnsignedShort
                        && index_accessor.componentType != fastgltf::ComponentType::UnsignedInt)) {
                    return Result<MeshData>::failure(
                        import_error(source_path, "indices must use an unsigned scalar accessor"));
                }
                if(index_accessor.count
                    > std::numeric_limits<std::uint32_t>::max() - data.indices.size()) {
                    return Result<MeshData>::failure(
                        import_error(source_path, "index count exceeds 32-bit draw limits"));
                }
                data.indices.reserve(data.indices.size() + index_accessor.count);
                bool invalid_index = false;
                fastgltf::iterateAccessor<std::uint32_t>(
                    asset, index_accessor, [&](const std::uint32_t local_index) {
                        if(local_index >= position_accessor.count) {
                            invalid_index = true;
                            return;
                        }
                        data.indices.push_back(
                            static_cast<std::uint32_t>(first_vertex) + local_index);
                    });
                if(invalid_index)
                    return Result<MeshData>::failure(import_error(
                        source_path, "primitive contains an out-of-range vertex index"));
            } else {
                if(position_accessor.count
                    > std::numeric_limits<std::uint32_t>::max() - data.indices.size()) {
                    return Result<MeshData>::failure(
                        import_error(source_path, "index count exceeds 32-bit draw limits"));
                }
                data.indices.reserve(data.indices.size() + position_accessor.count);
                for(std::size_t index = 0; index < position_accessor.count; ++index) {
                    data.indices.push_back(static_cast<std::uint32_t>(first_vertex + index));
                }
            }

            if((data.indices.size() - first_index) % 3 != 0) {
                return Result<MeshData>::failure(import_error(
                    source_path, "triangle primitive index count must be divisible by three"));
            }
            const bool has_tangents =
                has_normals && primitive.findAttribute("TANGENT") != primitive.attributes.end();
            primitives.push_back({first_index, data.indices.size() - first_index, !has_normals,
                !has_tangents
                    && primitive.findAttribute("TEXCOORD_0") != primitive.attributes.end()});
        }

        if(data.vertices.empty() || data.indices.empty()) {
            return Result<MeshData>::failure(
                import_error(source_path, "mesh contains no drawable triangles"));
        }
        auto animation = import_animation(asset, selection, data);
        if(!animation)
            return Result<MeshData>::failure(import_error(source_path, animation.error()));
        data.animation = std::move(animation).value();
        if(auto completed = complete_mesh_geometry(data, primitives); !completed)
            return Result<MeshData>::failure(import_error(source_path, completed.error()));
        return Result<MeshData>::success(std::move(data));
    }
}

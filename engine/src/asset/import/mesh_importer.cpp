#include "asset/import/mesh_importer.h"
#include "asset/data/mesh_data.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace Comet {
    namespace {
        constexpr std::size_t IMPORT_OVERHEAD_BYTES = 16ull * 1024 * 1024;

        std::string import_error(
            const std::filesystem::path& source_path, const std::string_view message) {
            return "Failed to import mesh '" + source_path.string() + "': " + std::string(message);
        }

        Result<std::shared_ptr<const AnimationData>> import_animation(
            const fastgltf::Asset& asset, MeshData& mesh) {
            using Imported = Result<std::shared_ptr<const AnimationData>>;
            if(asset.skins.empty()) {
                for(const auto& primitive : asset.meshes.front().primitives)
                    if(primitive.findAttribute("JOINTS_0") != primitive.attributes.end())
                        return Imported::failure("Joint attributes require a skin");
                return Imported::success(nullptr);
            }
            if(asset.skins.size() != 1)
                return Imported::failure("One skin is supported per Mesh asset");
            auto data = std::make_shared<AnimationData>();
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
                if(source.meshIndex) {
                    if(*source.meshIndex != 0 || !source.skinIndex || *source.skinIndex != 0
                        || data->mesh_node != NO_ANIMATION_NODE)
                        return Imported::failure("A skinned Mesh asset requires one mesh node");
                    data->mesh_node = uint32_t(index);
                }
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
            const auto& skin = asset.skins.front();
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
            for(const auto& primitive : asset.meshes.front().primitives) {
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

        Result<std::size_t> estimate_mesh_bytes(const fastgltf::Asset& asset,
            const std::size_t source_bytes, const AssetImportLimits& limits) {
            if(limits.mesh_working_bytes <= IMPORT_OVERHEAD_BYTES)
                return Result<std::size_t>::failure("Mesh working budget is too small");
            if(asset.meshes.size() != 1)
                return Result<std::size_t>::failure(
                    "exactly one glTF mesh is required per Comet Mesh asset");
            const std::size_t available = limits.mesh_working_bytes - IMPORT_OVERHEAD_BYTES;
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
            if(!asset.skins.empty()) {
                if(!add_output(asset.nodes.size(), sizeof(AnimationNode) + sizeof(uint32_t))
                    || !add_output(asset.skins.front().joints.size(), sizeof(SkinJoint))
                    || !add_output(asset.animations.size(), sizeof(AnimationClip)))
                    return Result<std::size_t>::failure(
                        "Skeleton exceeds its CPU working-set budget");
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
                sizeof(MeshVertex) + (asset.skins.empty() ? 0 : sizeof(SkinInfluences));
            for(const fastgltf::Primitive& primitive : asset.meshes.front().primitives) {
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
            }
            return Result<std::size_t>::success(
                source_bytes * 3 + output_bytes * 4 + IMPORT_OVERHEAD_BYTES);
        }

        Result<std::size_t> sum_source_bytes(
            const std::vector<std::filesystem::path>& files, const std::size_t limit) {
            std::size_t total = 0;
            for(const auto& path : files) {
                std::error_code error;
                const auto size = std::filesystem::file_size(path, error);
                if(error || size > limit - total)
                    return Result<std::size_t>::failure(
                        "Cannot read mesh input or inputs exceed the configured source budget: "
                        + path.string());
                total += static_cast<std::size_t>(size);
            }
            return Result<std::size_t>::success(total);
        }

        std::vector<std::filesystem::path> source_paths_for(
            const std::filesystem::path& source_path, const fastgltf::Asset& asset) {
            std::vector<std::filesystem::path> files{source_path.lexically_normal()};
            for(const fastgltf::Buffer& buffer : asset.buffers) {
                const auto* uri = std::get_if<fastgltf::sources::URI>(&buffer.data);
                if(!uri || !uri->uri.isLocalPath())
                    continue;
                const auto path =
                    (source_path.parent_path() / uri->uri.fspath()).lexically_normal();
                if(std::ranges::find(files, path) == files.end())
                    files.push_back(path);
            }
            return files;
        }

        struct MeshInspection {
            std::size_t working_bytes;
            std::vector<std::filesystem::path> source_paths;
        };

        Result<MeshInspection> inspect_mesh(
            const std::filesystem::path& source_path, const AssetImportLimits& limits) {
            if(limits.mesh_working_bytes <= IMPORT_OVERHEAD_BYTES)
                return Result<MeshInspection>::failure("Mesh working budget is too small");
            std::error_code error;
            const auto source_size = std::filesystem::file_size(source_path, error);
            if(error || source_size > limits.source_bytes)
                return Result<MeshInspection>::failure(
                    "Cannot read mesh input or input exceeds the configured source budget: "
                    + source_path.string());
            if(source_size > (limits.mesh_working_bytes - IMPORT_OVERHEAD_BYTES) / 3)
                return Result<MeshInspection>::failure(
                    "Mesh exceeds its configured CPU working-set budget");
            auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
            if(!source)
                return Result<MeshInspection>::failure(
                    import_error(source_path, fastgltf::getErrorMessage(source.error())));
            fastgltf::Parser parser;
            auto parsed = parser.loadGltf(source.get(), source_path.parent_path(),
                fastgltf::Options::DecomposeNodeMatrices, fastgltf::Category::All);
            if(!parsed)
                return Result<MeshInspection>::failure(
                    import_error(source_path, fastgltf::getErrorMessage(parsed.error())));
            auto paths = source_paths_for(source_path, parsed.get());
            auto source_bytes = sum_source_bytes(paths, limits.source_bytes);
            if(!source_bytes)
                return Result<MeshInspection>::failure(source_bytes.error());
            auto estimate = estimate_mesh_bytes(parsed.get(), source_bytes.value(), limits);
            if(!estimate)
                return Result<MeshInspection>::failure(estimate.error());
            return Result<MeshInspection>::success({estimate.value(), std::move(paths)});
        }

        Result<void> generate_normals(MeshData& data, const std::size_t first_vertex,
            const std::size_t vertex_count, const std::size_t first_index,
            const std::filesystem::path& source_path) {
            const std::size_t index_count = data.indices.size() - first_index;
            if(index_count % 3 != 0) {
                return Result<void>::failure(import_error(
                    source_path, "triangle primitive index count must be divisible by three"));
            }

            for(std::size_t index = first_index; index < data.indices.size(); index += 3) {
                const std::size_t i0 = data.indices[index];
                const std::size_t i1 = data.indices[index + 1];
                const std::size_t i2 = data.indices[index + 2];
                const std::size_t vertex_end = first_vertex + vertex_count;
                if(i0 < first_vertex || i0 >= vertex_end || i1 < first_vertex || i1 >= vertex_end
                    || i2 < first_vertex || i2 >= vertex_end) {
                    return Result<void>::failure(import_error(
                        source_path, "primitive contains an out-of-range vertex index"));
                }

                const Math::Vec3 edge_a = data.vertices[i1].position - data.vertices[i0].position;
                const Math::Vec3 edge_b = data.vertices[i2].position - data.vertices[i0].position;
                const Math::Vec3 face_normal = Math::cross(edge_a, edge_b);
                data.vertices[i0].normal += face_normal;
                data.vertices[i1].normal += face_normal;
                data.vertices[i2].normal += face_normal;
            }

            constexpr float NORMAL_EPSILON = 1.0e-12f;
            for(std::size_t index = first_vertex; index < first_vertex + vertex_count; ++index) {
                Math::Vec3& normal = data.vertices[index].normal;
                const float length = Math::length(normal);
                if(std::isfinite(length) && length > NORMAL_EPSILON) {
                    normal /= length;
                } else {
                    normal = Math::Vec3(0.0f, 1.0f, 0.0f);
                }
            }
            return Result<void>::success();
        }

        [[nodiscard]] Result<std::vector<std::filesystem::path>> collect_source_dependencies(
            const std::filesystem::path& source_path) {
            auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
            if(!source)
                return Result<std::vector<std::filesystem::path>>::failure(
                    import_error(source_path, fastgltf::getErrorMessage(source.error())));
            fastgltf::Parser parser;
            auto parsed = parser.loadGltf(source.get(), source_path.parent_path(),
                fastgltf::Options::None, fastgltf::Category::Buffers);
            if(!parsed)
                return Result<std::vector<std::filesystem::path>>::failure(
                    import_error(source_path, fastgltf::getErrorMessage(parsed.error())));
            auto paths = source_paths_for(source_path, parsed.get());
            return Result<std::vector<std::filesystem::path>>::success(
                {std::next(paths.begin()), paths.end()});
        }

    }

    Result<std::size_t> MeshImporter::working_bytes(
        const std::filesystem::path& source_path, const AssetImportLimits& limits) {
        auto inspection = inspect_mesh(source_path, limits);
        if(!inspection)
            return Result<std::size_t>::failure(inspection.error());
        return Result<std::size_t>::success(inspection.value().working_bytes);
    }

    Result<MeshData> MeshImporter::import(const std::filesystem::path& source_path,
        const std::size_t memory_budget, const AssetImportLimits& limits) const {
        auto inspection = inspect_mesh(source_path, limits);
        if(!inspection)
            return Result<MeshData>::failure(inspection.error());
        if(inspection.value().working_bytes > memory_budget)
            return Result<MeshData>::failure("Mesh exceeds its reserved CPU memory budget");
        auto source = fastgltf::GltfDataBuffer::FromPath(source_path);
        if(!source) {
            return Result<MeshData>::failure(
                import_error(source_path, fastgltf::getErrorMessage(source.error())));
        }

        constexpr auto options =
            fastgltf::Options::LoadExternalBuffers | fastgltf::Options::DecomposeNodeMatrices;
        fastgltf::Parser parser;
        auto parsed = parser.loadGltf(
            source.get(), source_path.parent_path(), options, fastgltf::Category::All);
        if(!parsed) {
            return Result<MeshData>::failure(
                import_error(source_path, fastgltf::getErrorMessage(parsed.error())));
        }

        fastgltf::Asset asset = std::move(parsed.get());
        auto source_bytes = sum_source_bytes(inspection.value().source_paths, limits.source_bytes);
        if(!source_bytes)
            return Result<MeshData>::failure(source_bytes.error());
        auto current_estimate = estimate_mesh_bytes(asset, source_bytes.value(), limits);
        if(!current_estimate || current_estimate.value() > memory_budget)
            return Result<MeshData>::failure("Mesh exceeds its reserved CPU memory budget");
        const fastgltf::Error validation_error = fastgltf::validate(asset);
        if(validation_error != fastgltf::Error::None) {
            return Result<MeshData>::failure(
                import_error(source_path, fastgltf::getErrorMessage(validation_error)));
        }
        if(asset.meshes.size() != 1) {
            return Result<MeshData>::failure(import_error(
                source_path, "exactly one glTF mesh is required per Comet Mesh asset"));
        }

        MeshData data;
        const fastgltf::Mesh& mesh = asset.meshes.front();
        for(const fastgltf::Primitive& primitive : mesh.primitives) {
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
            if(!has_normals) {
                auto normals = generate_normals(
                    data, first_vertex, position_accessor.count, first_index, source_path);
                if(!normals)
                    return Result<MeshData>::failure(normals.error());
            }
        }

        if(data.vertices.empty() || data.indices.empty()) {
            return Result<MeshData>::failure(
                import_error(source_path, "mesh contains no drawable triangles"));
        }
        auto animation = import_animation(asset, data);
        if(!animation)
            return Result<MeshData>::failure(import_error(source_path, animation.error()));
        data.animation = std::move(animation).value();
        return Result<MeshData>::success(std::move(data));
    }

    Result<MeshImportData> MeshImporter::import_with_dependencies(
        const std::filesystem::path& source_path, const std::size_t memory_budget,
        const AssetImportLimits& limits) const {
        auto data = import(source_path, memory_budget, limits);
        if(!data)
            return Result<MeshImportData>::failure(data.error());
        auto dependencies = collect_source_dependencies(source_path);
        if(!dependencies)
            return Result<MeshImportData>::failure(dependencies.error());
        return Result<MeshImportData>::success({.data = std::move(data).value(),
            .source_dependencies = std::move(dependencies).value()});
    }
}

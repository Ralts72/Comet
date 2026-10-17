#include "asset/artifact/mesh_artifact.h"
#include "asset/data/mesh_data.h"

#include "common/binary.h"
#include "common/file_io.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Comet {
    namespace {
        constexpr std::array<std::byte, 8> MAGIC{std::byte{'C'}, std::byte{'O'}, std::byte{'M'},
            std::byte{'E'}, std::byte{'T'}, std::byte{'M'}, std::byte{'S'}, std::byte{'H'}};
        constexpr std::uint32_t FORMAT_VERSION = 4;
        constexpr std::uint32_t MAX_INPUT_COUNT = 1024;
        constexpr std::uint32_t MAX_PATH_LENGTH = 16 * 1024;
        bool write_animation(Binary::Writer& writer, const MeshData& mesh) {
            writer.write_u32(mesh.animation ? 1 : 0);
            if(!mesh.animation)
                return mesh.skin.empty();
            const auto& data = *mesh.animation;
            if(mesh.skin.size() != mesh.vertices.size() || !validate_animation(data))
                return false;
            writer.write_u32(data.mesh_node);
            writer.write_u32(uint32_t(data.nodes.size()));
            for(const auto& node : data.nodes) {
                if(!writer.write_string(node.name, MAX_PATH_LENGTH))
                    return false;
                writer.write_u32(node.parent);
                for(int i = 0; i < 3; ++i)
                    writer.write_float(node.rest.translation[i]);
                for(int i = 0; i < 4; ++i)
                    writer.write_float(node.rest.rotation[i]);
                for(int i = 0; i < 3; ++i)
                    writer.write_float(node.rest.scale[i]);
            }
            for(const auto node : data.evaluation_order)
                writer.write_u32(node);
            writer.write_u32(uint32_t(data.joints.size()));
            for(const auto& joint : data.joints) {
                writer.write_u32(joint.node);
                for(int c = 0; c < 4; ++c)
                    for(int r = 0; r < 4; ++r)
                        writer.write_float(joint.inverse_bind[c][r]);
            }
            for(const auto& influence : mesh.skin) {
                if(!Math::is_finite(influence.weights)
                    || Math::any_less(influence.weights, Math::Vec4(0))
                    || std::abs(Math::dot(influence.weights, Math::Vec4(1)) - 1) > 0.001f)
                    return false;
                for(const auto joint : influence.joints)
                    if(joint >= data.joints.size())
                        return false;
                for(const auto joint : influence.joints)
                    writer.write_u32(joint);
                for(int i = 0; i < 4; ++i)
                    writer.write_float(influence.weights[i]);
            }
            writer.write_u32(uint32_t(data.clips.size()));
            for(const auto& clip : data.clips) {
                if(!writer.write_string(clip.name, MAX_PATH_LENGTH))
                    return false;
                writer.write_float(clip.duration);
                writer.write_u32(uint32_t(clip.channels.size()));
                for(const auto& channel : clip.channels) {
                    writer.write_u32(channel.node);
                    writer.write_u32(uint32_t(channel.path));
                    writer.write_u32(uint32_t(channel.interpolation));
                    writer.write_u32(uint32_t(channel.times.size()));
                    for(const auto time : channel.times)
                        writer.write_float(time);
                    for(const auto& value : channel.values)
                        for(int i = 0; i < 4; ++i)
                            writer.write_float(value[i]);
                }
            }
            return true;
        }
        bool read_count(Binary::Reader& reader, uint32_t& count, std::size_t minimum_bytes) {
            return reader.read_u32(count) && count <= 1000000
                   && count <= reader.remaining() / minimum_bytes;
        }
        bool read_animation(Binary::Reader& reader, MeshData& mesh) {
            uint32_t present = 0;
            if(!reader.read_u32(present) || present > 1)
                return false;
            if(!present)
                return true;
            auto data = std::make_shared<AnimationData>();
            uint32_t count = 0;
            if(!reader.read_u32(data->mesh_node) || !read_count(reader, count, 48))
                return false;
            data->nodes.resize(count);
            data->evaluation_order.resize(count);
            for(auto& node : data->nodes) {
                if(!reader.read_string(node.name, MAX_PATH_LENGTH) || !reader.read_u32(node.parent))
                    return false;
                for(int i = 0; i < 3; ++i)
                    if(!reader.read_float(node.rest.translation[i]))
                        return false;
                for(int i = 0; i < 4; ++i)
                    if(!reader.read_float(node.rest.rotation[i]))
                        return false;
                for(int i = 0; i < 3; ++i)
                    if(!reader.read_float(node.rest.scale[i]))
                        return false;
            }
            for(auto& node : data->evaluation_order)
                if(!reader.read_u32(node))
                    return false;
            if(!read_count(reader, count, 68))
                return false;
            data->joints.resize(count);
            for(auto& joint : data->joints) {
                if(!reader.read_u32(joint.node))
                    return false;
                for(int c = 0; c < 4; ++c)
                    for(int r = 0; r < 4; ++r)
                        if(!reader.read_float(joint.inverse_bind[c][r]))
                            return false;
            }
            if(mesh.vertices.size() > reader.remaining() / 32)
                return false;
            mesh.skin.resize(mesh.vertices.size());
            for(std::size_t index = 0; index < mesh.skin.size(); ++index) {
                auto& influence = mesh.skin[index];
                for(auto& joint : influence.joints)
                    if(!reader.read_u32(joint) || joint >= data->joints.size())
                        return false;
                for(int i = 0; i < 4; ++i)
                    if(!reader.read_float(influence.weights[i])
                        || !std::isfinite(influence.weights[i]) || influence.weights[i] < 0)
                        return false;
                if(std::abs(Math::dot(influence.weights, Math::Vec4(1)) - 1) > 0.001f)
                    return false;
                for(int i = 0; i < 4; ++i) {
                    if(influence.weights[i] == 0)
                        continue;
                    auto& bounds = data->joints[influence.joints[i]].bounds;
                    if(bounds)
                        bounds->include(mesh.vertices[index].position);
                    else
                        bounds = BoundingBox::from_point(mesh.vertices[index].position);
                }
            }
            if(!read_count(reader, count, 12))
                return false;
            data->clips.resize(count);
            for(auto& clip : data->clips) {
                if(!reader.read_string(clip.name, MAX_PATH_LENGTH)
                    || !reader.read_float(clip.duration) || !read_count(reader, count, 16))
                    return false;
                clip.channels.resize(count);
                for(auto& channel : clip.channels) {
                    uint32_t path = 0, interpolation = 0;
                    if(!reader.read_u32(channel.node) || !reader.read_u32(path) || path > 2
                        || !reader.read_u32(interpolation) || interpolation > 2
                        || !read_count(reader, count, 20))
                        return false;
                    channel.path = AnimationChannel::Path(path);
                    channel.interpolation = AnimationChannel::Interpolation(interpolation);
                    const auto value_count = std::size_t(count) * (interpolation == 2 ? 3 : 1);
                    if(count * 4ull + value_count * 16ull > reader.remaining())
                        return false;
                    channel.times.resize(count);
                    channel.values.resize(value_count);
                    for(auto& time : channel.times)
                        if(!reader.read_float(time))
                            return false;
                    for(auto& value : channel.values)
                        for(int i = 0; i < 4; ++i)
                            if(!reader.read_float(value[i]))
                                return false;
                }
            }
            if(!validate_animation(*data))
                return false;
            mesh.animation = std::move(data);
            return true;
        }

        [[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
            const std::u8string value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }

        [[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
            const std::u8string decoded(
                reinterpret_cast<const char8_t*>(value.data()), value.size());
            return std::filesystem::path(decoded);
        }

        [[nodiscard]] bool read_vertex(Binary::Reader& reader, MeshVertex& vertex) {
            return reader.read_float(vertex.position.x) && reader.read_float(vertex.position.y)
                   && reader.read_float(vertex.position.z) && reader.read_float(vertex.texcoord.x)
                   && reader.read_float(vertex.texcoord.y) && reader.read_float(vertex.normal.x)
                   && reader.read_float(vertex.normal.y) && reader.read_float(vertex.normal.z)
                   && reader.read_float(vertex.tangent.x) && reader.read_float(vertex.tangent.y)
                   && reader.read_float(vertex.tangent.z) && reader.read_float(vertex.tangent.w)
                   && Math::is_finite(vertex.tangent) && Math::is_finite(vertex.position)
                   && Math::is_finite(vertex.texcoord) && Math::is_finite(vertex.normal);
        }

        bool write_vertex(Binary::Writer& writer, const MeshVertex& vertex) {
            const std::array values{vertex.position.x, vertex.position.y, vertex.position.z,
                vertex.texcoord.x, vertex.texcoord.y, vertex.normal.x, vertex.normal.y,
                vertex.normal.z, vertex.tangent.x, vertex.tangent.y, vertex.tangent.z,
                vertex.tangent.w};
            if(!std::ranges::all_of(
                   values, [](const float value) { return std::isfinite(value); })) {
                return false;
            }
            for(const float value : values) {
                writer.write_float(value);
            }
            return true;
        }

    }

    std::optional<MeshArtifact> MeshArtifact::load(const std::filesystem::path& artifact_path,
        const AssetHandle expected_handle, const std::size_t memory_budget) {
        const auto file = read_binary_file(artifact_path, memory_budget / 3);
        if(!file || file.value().size() < MAGIC.size() + sizeof(std::uint64_t)) {
            return std::nullopt;
        }

        std::uint64_t stored_checksum = 0;
        if(!Binary::Reader(std::span<const std::byte>(file.value()).last(sizeof(stored_checksum)))
                .read_u64(stored_checksum)) {
            return std::nullopt;
        }
        const std::span payload(file.value());
        const std::span<const std::byte> serialized =
            payload.first(payload.size() - sizeof(stored_checksum));
        if(Binary::hash_bytes(serialized) != stored_checksum) {
            return std::nullopt;
        }

        Binary::Reader reader(serialized);
        std::uint32_t format_version = 0;
        std::uint32_t stored_importer_version = 0;
        std::uint64_t stored_handle = 0;
        std::uint32_t input_count = 0;
        std::uint32_t vertex_count = 0;
        std::uint32_t index_count = 0;
        if(!reader.match_bytes(MAGIC) || !reader.read_u32(format_version)
            || !reader.read_u32(stored_importer_version) || !reader.read_u64(stored_handle)
            || !reader.read_u32(input_count) || !reader.read_u32(vertex_count)
            || !reader.read_u32(index_count) || format_version != FORMAT_VERSION
            || stored_importer_version == 0 || stored_handle == 0
            || AssetHandle(stored_handle) != expected_handle || input_count == 0
            || input_count > MAX_INPUT_COUNT || vertex_count == 0 || index_count == 0
            || index_count % 3 != 0) {
            return std::nullopt;
        }

        MeshArtifact artifact{
            .handle = AssetHandle(stored_handle), .importer_version = stored_importer_version};
        artifact.source_inputs.files.reserve(input_count);
        for(std::uint32_t index = 0; index < input_count; ++index) {
            std::string serialized_path;
            ImportInputFingerprint input;
            if(!reader.read_string(serialized_path, MAX_PATH_LENGTH) || !reader.read_u64(input.size)
                || !reader.read_u64(input.hash)) {
                return std::nullopt;
            }
            const std::filesystem::path relative =
                path_from_utf8(serialized_path).lexically_normal();
            if(path_to_utf8(relative) != serialized_path) {
                return std::nullopt;
            }
            input.relative_path = relative;
            artifact.source_inputs.files.push_back(std::move(input));
        }
        if(!artifact.source_inputs.valid()) {
            return std::nullopt;
        }

        constexpr std::uint64_t SERIALIZED_VERTEX_SIZE = 12 * sizeof(float);
        const std::uint64_t expected_mesh_size =
            static_cast<std::uint64_t>(vertex_count) * SERIALIZED_VERTEX_SIZE
            + static_cast<std::uint64_t>(index_count) * sizeof(std::uint32_t);
        if(expected_mesh_size + 4 > reader.remaining()) {
            return std::nullopt;
        }

        artifact.data.vertices.resize(vertex_count);
        for(MeshVertex& vertex : artifact.data.vertices) {
            if(!read_vertex(reader, vertex)) {
                return std::nullopt;
            }
        }
        artifact.data.indices.resize(index_count);
        for(std::uint32_t& index : artifact.data.indices) {
            if(!reader.read_u32(index) || index >= vertex_count) {
                return std::nullopt;
            }
        }

        if(!read_animation(reader, artifact.data) || reader.remaining() != 0)
            return std::nullopt;
        return artifact;
    }

    Result<void> MeshArtifact::publish_atomic(const std::filesystem::path& artifact_path) const {
        if(data.vertices.empty() || data.indices.empty() || data.indices.size() % 3 != 0
            || data.vertices.size() > std::numeric_limits<std::uint32_t>::max()
            || data.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
            return Result<void>::failure(
                "Cannot publish a mesh artifact with invalid vertex or index counts");
        }

        if(!handle || importer_version == 0 || source_inputs.files.size() > MAX_INPUT_COUNT
            || !source_inputs.valid()) {
            return Result<void>::failure(
                "Cannot publish a mesh artifact with invalid source inputs");
        }

        Binary::Writer writer;
        writer.write_bytes(MAGIC);
        writer.write_u32(FORMAT_VERSION);
        writer.write_u32(importer_version);
        writer.write_u64(handle.value());
        writer.write_u32(static_cast<std::uint32_t>(source_inputs.files.size()));
        writer.write_u32(static_cast<std::uint32_t>(data.vertices.size()));
        writer.write_u32(static_cast<std::uint32_t>(data.indices.size()));
        for(const ImportInputFingerprint& input : source_inputs.files) {
            if(!writer.write_string(path_to_utf8(input.relative_path), MAX_PATH_LENGTH))
                return Result<void>::failure("Mesh artifact input path is too long");
            writer.write_u64(input.size);
            writer.write_u64(input.hash);
        }
        for(const MeshVertex& vertex : data.vertices) {
            if(!write_vertex(writer, vertex))
                return Result<void>::failure(
                    "Cannot publish a mesh artifact containing non-finite vertex data");
        }
        for(const std::uint32_t index : data.indices) {
            if(index >= data.vertices.size()) {
                return Result<void>::failure(
                    "Cannot publish a mesh artifact with an out-of-range index");
            }
            writer.write_u32(index);
        }
        if(!write_animation(writer, data))
            return Result<void>::failure("Cannot publish invalid animation data");
        writer.write_u64(Binary::hash_bytes(writer.data()));
        return write_binary_file_atomic(artifact_path, writer.data());
    }

    std::vector<std::filesystem::path> MeshArtifact::source_dependencies() const {
        if(source_inputs.files.size() <= 1) {
            return {};
        }

        std::vector<std::filesystem::path> dependencies;
        dependencies.reserve(source_inputs.files.size() - 1);
        for(auto input = std::next(source_inputs.files.begin()); input != source_inputs.files.end();
            ++input) {
            dependencies.push_back(input->relative_path);
        }
        return dependencies;
    }
}

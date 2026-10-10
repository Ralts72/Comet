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
        constexpr std::uint32_t FORMAT_VERSION = 2;
        constexpr std::uint32_t MAX_INPUT_COUNT = 1024;
        constexpr std::uint32_t MAX_PATH_LENGTH = 16 * 1024;
        [[nodiscard]] bool is_safe_relative_path(const std::filesystem::path& path) {
            if(path.empty() || path.is_absolute()) {
                return false;
            }
            for(const std::filesystem::path& component : path) {
                if(component == "..") {
                    return false;
                }
            }
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
                   && Math::is_finite(vertex.position) && Math::is_finite(vertex.texcoord)
                   && Math::is_finite(vertex.normal);
        }

        bool write_vertex(Binary::Writer& writer, const MeshVertex& vertex) {
            const std::array values{vertex.position.x, vertex.position.y, vertex.position.z,
                vertex.texcoord.x, vertex.texcoord.y, vertex.normal.x, vertex.normal.y,
                vertex.normal.z};
            if(!std::ranges::all_of(
                   values, [](const float value) { return std::isfinite(value); })) {
                return false;
            }
            for(const float value : values) {
                writer.write_float(value);
            }
            return true;
        }

        [[nodiscard]] bool valid_source_inputs(const ImportInputSnapshot& snapshot) {
            if(snapshot.files.empty() || snapshot.files.size() > MAX_INPUT_COUNT) {
                return false;
            }

            std::filesystem::path previous_dependency;
            for(std::size_t index = 0; index < snapshot.files.size(); ++index) {
                const std::filesystem::path relative =
                    snapshot.files[index].relative_path.lexically_normal();
                if(!is_safe_relative_path(relative)
                    || relative != snapshot.files[index].relative_path
                    || (index > 0 && relative == snapshot.files.front().relative_path)
                    || (index > 1
                        && relative.generic_string() <= previous_dependency.generic_string())) {
                    return false;
                }
                if(index > 0) {
                    previous_dependency = relative;
                }
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
            if(!is_safe_relative_path(relative) || path_to_utf8(relative) != serialized_path) {
                return std::nullopt;
            }
            input.relative_path = relative;
            artifact.source_inputs.files.push_back(std::move(input));
        }
        if(!valid_source_inputs(artifact.source_inputs)) {
            return std::nullopt;
        }

        constexpr std::uint64_t SERIALIZED_VERTEX_SIZE = 8 * sizeof(float);
        const std::uint64_t expected_mesh_size =
            static_cast<std::uint64_t>(vertex_count) * SERIALIZED_VERTEX_SIZE
            + static_cast<std::uint64_t>(index_count) * sizeof(std::uint32_t);
        if(expected_mesh_size != reader.remaining()) {
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

        return artifact;
    }

    Result<void> MeshArtifact::publish_atomic(const std::filesystem::path& artifact_path) const {
        if(data.vertices.empty() || data.indices.empty() || data.indices.size() % 3 != 0
            || data.vertices.size() > std::numeric_limits<std::uint32_t>::max()
            || data.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
            return Result<void>::failure(
                "Cannot publish a mesh artifact with invalid vertex or index counts");
        }

        if(!handle || importer_version == 0 || !valid_source_inputs(source_inputs)) {
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

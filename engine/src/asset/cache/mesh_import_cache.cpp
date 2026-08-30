#include "asset/cache/mesh_import_cache.h"

#include "common/file_io.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Comet::MeshImportCache {
    namespace {
        constexpr std::array<std::byte, 8> MAGIC{
            std::byte{'C'}, std::byte{'O'}, std::byte{'M'}, std::byte{'E'},
            std::byte{'T'}, std::byte{'M'}, std::byte{'S'}, std::byte{'H'}};
        constexpr std::uint32_t FORMAT_VERSION = 1;
        constexpr std::uint32_t MAX_INPUT_COUNT = 1024;
        constexpr std::uint32_t MAX_PATH_LENGTH = 16 * 1024;
        constexpr std::uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
        constexpr std::uint64_t FNV_PRIME = 1099511628211ull;
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        static_assert(std::numeric_limits<float>::is_iec559);

        struct FileFingerprint {
            std::uint64_t size = 0;
            std::uint64_t hash = FNV_OFFSET_BASIS;
        };

        struct InputRecord {
            std::string relative_path;
            FileFingerprint fingerprint;
        };

        [[nodiscard]] std::uint64_t hash_bytes(
            const std::span<const std::byte> bytes) {
            std::uint64_t hash = FNV_OFFSET_BASIS;
            for(const std::byte byte: bytes) {
                hash ^= std::to_integer<std::uint8_t>(byte);
                hash *= FNV_PRIME;
            }
            return hash;
        }

        [[nodiscard]] std::optional<FileFingerprint> fingerprint_file(
            const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            if(!input) {
                return std::nullopt;
            }

            FileFingerprint fingerprint;
            std::array<char, 64 * 1024> buffer{};
            while(input) {
                input.read(
                    buffer.data(),
                    static_cast<std::streamsize>(buffer.size()));
                const std::streamsize count = input.gcount();
                if(count <= 0) {
                    continue;
                }
                if(static_cast<std::uint64_t>(count)
                   > std::numeric_limits<std::uint64_t>::max()
                       - fingerprint.size) {
                    return std::nullopt;
                }
                fingerprint.size += static_cast<std::uint64_t>(count);
                for(std::streamsize index = 0; index < count; ++index) {
                    fingerprint.hash ^=
                        static_cast<unsigned char>(buffer[index]);
                    fingerprint.hash *= FNV_PRIME;
                }
            }
            if(!input.eof()) {
                return std::nullopt;
            }
            return fingerprint;
        }

        [[nodiscard]] bool is_safe_relative_path(
            const std::filesystem::path& path) {
            if(path.empty() || path.is_absolute()) {
                return false;
            }
            for(const std::filesystem::path& component: path) {
                if(component == "..") {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::filesystem::path canonical_path(
            const std::filesystem::path& path) {
            std::error_code error;
            std::filesystem::path result =
                std::filesystem::weakly_canonical(path, error);
            if(error) {
                throw std::runtime_error(
                    "Failed to resolve path '" + path.string()
                    + "': " + error.message());
            }
            return result;
        }

        [[nodiscard]] std::filesystem::path relative_to_root(
            const std::filesystem::path& canonical_root,
            const std::filesystem::path& path) {
            const std::filesystem::path canonical = canonical_path(path);
            const std::filesystem::path relative =
                canonical.lexically_relative(canonical_root).lexically_normal();
            if(!is_safe_relative_path(relative)) {
                throw std::runtime_error(
                    "Mesh import cache input is outside the asset root: "
                    + path.string());
            }
            return relative;
        }

        [[nodiscard]] std::string path_to_utf8(
            const std::filesystem::path& path) {
            const std::u8string value = path.generic_u8string();
            return {
                reinterpret_cast<const char*>(value.data()),
                value.size()
            };
        }

        [[nodiscard]] std::filesystem::path path_from_utf8(
            const std::string_view value) {
            const std::u8string decoded(
                reinterpret_cast<const char8_t*>(value.data()),
                value.size());
            return std::filesystem::path(decoded);
        }

        class BinaryWriter final {
        public:
            void write_bytes(const std::span<const std::byte> bytes) {
                m_data.insert(m_data.end(), bytes.begin(), bytes.end());
            }

            void write_u32(const std::uint32_t value) {
                for(std::uint32_t shift = 0; shift < 32; shift += 8) {
                    m_data.push_back(
                        static_cast<std::byte>((value >> shift) & 0xFF));
                }
            }

            void write_u64(const std::uint64_t value) {
                for(std::uint32_t shift = 0; shift < 64; shift += 8) {
                    m_data.push_back(
                        static_cast<std::byte>((value >> shift) & 0xFF));
                }
            }

            void write_float(const float value) {
                write_u32(std::bit_cast<std::uint32_t>(value));
            }

            void write_string(const std::string_view value) {
                if(value.size() > MAX_PATH_LENGTH) {
                    throw std::runtime_error(
                        "Mesh import cache input path is too long");
                }
                write_u32(static_cast<std::uint32_t>(value.size()));
                write_bytes(std::as_bytes(std::span(value)));
            }

            [[nodiscard]] const std::vector<std::byte>& data() const {
                return m_data;
            }

        private:
            std::vector<std::byte> m_data;
        };

        class BinaryReader final {
        public:
            explicit BinaryReader(const std::span<const std::byte> data)
                : m_data(data) {}

            [[nodiscard]] bool read_bytes(
                const std::span<const std::byte> expected) {
                if(expected.size() > remaining()) {
                    return false;
                }
                const bool equal = std::equal(
                    expected.begin(),
                    expected.end(),
                    m_data.begin() + static_cast<std::ptrdiff_t>(m_offset));
                m_offset += expected.size();
                return equal;
            }

            [[nodiscard]] bool read_u32(std::uint32_t& value) {
                if(sizeof(value) > remaining()) {
                    return false;
                }
                value = 0;
                for(std::uint32_t shift = 0; shift < 32; shift += 8) {
                    value |= static_cast<std::uint32_t>(
                                 std::to_integer<std::uint8_t>(
                                     m_data[m_offset++]))
                        << shift;
                }
                return true;
            }

            [[nodiscard]] bool read_u64(std::uint64_t& value) {
                if(sizeof(value) > remaining()) {
                    return false;
                }
                value = 0;
                for(std::uint32_t shift = 0; shift < 64; shift += 8) {
                    value |= static_cast<std::uint64_t>(
                                 std::to_integer<std::uint8_t>(
                                     m_data[m_offset++]))
                        << shift;
                }
                return true;
            }

            [[nodiscard]] bool read_float(float& value) {
                std::uint32_t bits = 0;
                if(!read_u32(bits)) {
                    return false;
                }
                value = std::bit_cast<float>(bits);
                return true;
            }

            [[nodiscard]] bool read_string(std::string& value) {
                std::uint32_t size = 0;
                if(!read_u32(size) || size > MAX_PATH_LENGTH
                   || size > remaining()) {
                    return false;
                }
                value.assign(
                    reinterpret_cast<const char*>(m_data.data() + m_offset),
                    size);
                m_offset += size;
                return true;
            }

            [[nodiscard]] std::size_t remaining() const {
                return m_data.size() - m_offset;
            }

        private:
            std::span<const std::byte> m_data;
            std::size_t m_offset = 0;
        };

        [[nodiscard]] std::optional<std::vector<std::byte>> read_file(
            const std::filesystem::path& path) {
            std::error_code error;
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if(error
               || size > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::size_t>::max())
               || size > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::streamsize>::max())) {
                return std::nullopt;
            }

            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            std::ifstream input(path, std::ios::binary);
            if(!input) {
                return std::nullopt;
            }
            if(!bytes.empty()) {
                input.read(
                    reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
            }
            if(!input || input.peek() != std::ifstream::traits_type::eof()) {
                return std::nullopt;
            }
            return bytes;
        }

        [[nodiscard]] bool read_checksum(
            const std::span<const std::byte> bytes,
            std::uint64_t& checksum) {
            if(bytes.size() < sizeof(checksum)) {
                return false;
            }
            checksum = 0;
            const std::size_t offset = bytes.size() - sizeof(checksum);
            for(std::uint32_t shift = 0; shift < 64; shift += 8) {
                checksum |= static_cast<std::uint64_t>(
                                std::to_integer<std::uint8_t>(
                                    bytes[offset + shift / 8]))
                    << shift;
            }
            return true;
        }

        [[nodiscard]] bool read_vertex(
            BinaryReader& reader,
            MeshVertex& vertex) {
            return reader.read_float(vertex.position.x)
                && reader.read_float(vertex.position.y)
                && reader.read_float(vertex.position.z)
                && reader.read_float(vertex.texcoord.x)
                && reader.read_float(vertex.texcoord.y)
                && reader.read_float(vertex.normal.x)
                && reader.read_float(vertex.normal.y)
                && reader.read_float(vertex.normal.z)
                && std::isfinite(vertex.position.x)
                && std::isfinite(vertex.position.y)
                && std::isfinite(vertex.position.z)
                && std::isfinite(vertex.texcoord.x)
                && std::isfinite(vertex.texcoord.y)
                && std::isfinite(vertex.normal.x)
                && std::isfinite(vertex.normal.y)
                && std::isfinite(vertex.normal.z);
        }

        void write_vertex(BinaryWriter& writer, const MeshVertex& vertex) {
            const std::array values{
                vertex.position.x,
                vertex.position.y,
                vertex.position.z,
                vertex.texcoord.x,
                vertex.texcoord.y,
                vertex.normal.x,
                vertex.normal.y,
                vertex.normal.z
            };
            if(!std::ranges::all_of(values, [](const float value) {
                   return std::isfinite(value);
               })) {
                throw std::runtime_error(
                    "Cannot cache a mesh containing non-finite vertex data");
            }
            for(const float value: values) {
                writer.write_float(value);
            }
        }
    }

    std::optional<MeshData> load_if_current(
        const std::filesystem::path& cache_path,
        const std::filesystem::path& asset_root,
        const std::filesystem::path& source_path,
        const std::uint32_t importer_version) {
        try {
            const auto file = read_file(cache_path);
            if(!file || file->size() < MAGIC.size() + sizeof(std::uint64_t)) {
                return std::nullopt;
            }

            std::uint64_t stored_checksum = 0;
            if(!read_checksum(*file, stored_checksum)) {
                return std::nullopt;
            }
            const std::span payload(*file);
            const std::span<const std::byte> serialized =
                payload.first(payload.size() - sizeof(stored_checksum));
            if(hash_bytes(serialized) != stored_checksum) {
                return std::nullopt;
            }

            BinaryReader reader(serialized);
            std::uint32_t format_version = 0;
            std::uint32_t stored_importer_version = 0;
            std::uint32_t input_count = 0;
            std::uint32_t vertex_count = 0;
            std::uint32_t index_count = 0;
            if(!reader.read_bytes(MAGIC)
               || !reader.read_u32(format_version)
               || !reader.read_u32(stored_importer_version)
               || !reader.read_u32(input_count)
               || !reader.read_u32(vertex_count)
               || !reader.read_u32(index_count)
               || format_version != FORMAT_VERSION
               || stored_importer_version != importer_version
               || input_count == 0 || input_count > MAX_INPUT_COUNT
               || vertex_count == 0 || index_count == 0
               || index_count % 3 != 0) {
                return std::nullopt;
            }

            std::vector<InputRecord> inputs;
            inputs.reserve(input_count);
            for(std::uint32_t index = 0; index < input_count; ++index) {
                InputRecord input;
                if(!reader.read_string(input.relative_path)
                   || !reader.read_u64(input.fingerprint.size)
                   || !reader.read_u64(input.fingerprint.hash)) {
                    return std::nullopt;
                }
                const std::filesystem::path relative =
                    path_from_utf8(input.relative_path).lexically_normal();
                if(!is_safe_relative_path(relative)
                   || path_to_utf8(relative) != input.relative_path) {
                    return std::nullopt;
                }
                inputs.push_back(std::move(input));
            }

            const std::filesystem::path canonical_root =
                canonical_path(asset_root);
            const std::string expected_source = path_to_utf8(
                relative_to_root(canonical_root, source_path));
            if(inputs.front().relative_path != expected_source) {
                return std::nullopt;
            }

            for(const InputRecord& input: inputs) {
                const std::filesystem::path relative =
                    path_from_utf8(input.relative_path);
                const std::filesystem::path input_path =
                    canonical_root / relative;
                if(relative_to_root(canonical_root, input_path) != relative) {
                    return std::nullopt;
                }
                const auto fingerprint = fingerprint_file(input_path);
                if(!fingerprint
                   || fingerprint->size != input.fingerprint.size
                   || fingerprint->hash != input.fingerprint.hash) {
                    return std::nullopt;
                }
            }

            constexpr std::uint64_t SERIALIZED_VERTEX_SIZE = 8 * sizeof(float);
            const std::uint64_t expected_mesh_size =
                static_cast<std::uint64_t>(vertex_count)
                    * SERIALIZED_VERTEX_SIZE
                + static_cast<std::uint64_t>(index_count)
                    * sizeof(std::uint32_t);
            if(expected_mesh_size != reader.remaining()) {
                return std::nullopt;
            }

            MeshData data;
            data.vertices.resize(vertex_count);
            for(MeshVertex& vertex: data.vertices) {
                if(!read_vertex(reader, vertex)) {
                    return std::nullopt;
                }
            }
            data.indices.resize(index_count);
            for(std::uint32_t& index: data.indices) {
                if(!reader.read_u32(index) || index >= vertex_count) {
                    return std::nullopt;
                }
            }

            return data;
        } catch(...) {
            return std::nullopt;
        }
    }

    void store(
        const std::filesystem::path& cache_path,
        const std::filesystem::path& asset_root,
        const std::filesystem::path& source_path,
        const std::span<const std::filesystem::path> source_dependencies,
        const std::uint32_t importer_version,
        const MeshData& data) {
        if(data.vertices.empty() || data.indices.empty()
           || data.indices.size() % 3 != 0
           || data.vertices.size() > std::numeric_limits<std::uint32_t>::max()
           || data.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error(
                "Cannot cache a mesh with invalid vertex or index counts");
        }

        const std::filesystem::path canonical_root = canonical_path(asset_root);
        const std::filesystem::path source_relative =
            relative_to_root(canonical_root, source_path);
        std::map<std::string, std::filesystem::path> dependency_paths;
        for(const std::filesystem::path& dependency: source_dependencies) {
            const std::filesystem::path relative =
                relative_to_root(canonical_root, dependency);
            if(relative == source_relative) {
                continue;
            }
            dependency_paths.emplace(path_to_utf8(relative), relative);
        }
        if(dependency_paths.size() + 1 > MAX_INPUT_COUNT) {
            throw std::runtime_error(
                "Mesh import cache has too many source dependencies");
        }

        std::vector<InputRecord> inputs;
        inputs.reserve(dependency_paths.size() + 1);
        const auto add_input = [&](const std::filesystem::path& relative) {
            const auto fingerprint =
                fingerprint_file(canonical_root / relative);
            if(!fingerprint) {
                throw std::runtime_error(
                    "Failed to fingerprint mesh import cache input '"
                    + (canonical_root / relative).string() + "'");
            }
            inputs.push_back({
                .relative_path = path_to_utf8(relative),
                .fingerprint = *fingerprint
            });
        };
        add_input(source_relative);
        for(const auto& [path, relative]: dependency_paths) {
            static_cast<void>(path);
            add_input(relative);
        }

        BinaryWriter writer;
        writer.write_bytes(MAGIC);
        writer.write_u32(FORMAT_VERSION);
        writer.write_u32(importer_version);
        writer.write_u32(static_cast<std::uint32_t>(inputs.size()));
        writer.write_u32(static_cast<std::uint32_t>(data.vertices.size()));
        writer.write_u32(static_cast<std::uint32_t>(data.indices.size()));
        for(const InputRecord& input: inputs) {
            writer.write_string(input.relative_path);
            writer.write_u64(input.fingerprint.size);
            writer.write_u64(input.fingerprint.hash);
        }
        for(const MeshVertex& vertex: data.vertices) {
            write_vertex(writer, vertex);
        }
        for(const std::uint32_t index: data.indices) {
            if(index >= data.vertices.size()) {
                throw std::runtime_error(
                    "Cannot cache a mesh with an out-of-range index");
            }
            writer.write_u32(index);
        }
        writer.write_u64(hash_bytes(writer.data()));
        write_binary_file_atomic(cache_path, writer.data());
    }
}

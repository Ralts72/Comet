#include "asset/artifact/shader_program_artifact.h"
#include "asset/serialization/shader_program_serializer.h"
#include "common/file_io.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Comet {
    namespace {
        constexpr std::string_view MAGIC = "COMETSPG";
        constexpr std::uint32_t SPIRV_MAGIC = 0x07230203;
        constexpr std::size_t MAX_BYTES = 64 * 1024 * 1024;
        constexpr std::size_t MAX_INPUTS = 512;
        constexpr std::size_t MAX_PATH_BYTES = 16 * 1024;
        constexpr std::size_t MAX_ENTRY_BYTES = 256;
        constexpr std::size_t MAX_MATERIAL_BYTES = 64 * 1024;
        constexpr std::uint64_t HASH_SEED = 14695981039346656037ull;

        void append_u64(std::vector<std::byte>& output, const std::uint64_t value) {
            for(unsigned shift = 0; shift < 64; shift += 8)
                output.push_back(std::byte((value >> shift) & 0xff));
        }

        std::uint64_t hash_bytes(const std::span<const std::byte> input) {
            std::uint64_t hash = HASH_SEED;
            for(const auto byte : input) {
                hash ^= std::to_integer<unsigned char>(byte);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        bool valid_relative(const std::filesystem::path& path) {
            if(path.empty() || path.is_absolute() || path != path.lexically_normal())
                return false;
            for(const auto& component : path)
                if(component == "..")
                    return false;
            return true;
        }

        bool valid_words(const std::vector<std::uint32_t>& words) {
            return words.size() >= 5 && words.front() == SPIRV_MAGIC
                   && words.size() <= MAX_BYTES / sizeof(std::uint32_t);
        }

        class Reader final {
        public:
            explicit Reader(const std::span<const std::byte> input) : m_input(input) {}

            bool read_u64(std::uint64_t& value) {
                if(m_input.size() - m_offset < 8)
                    return false;
                value = 0;
                for(unsigned shift = 0; shift < 64; shift += 8)
                    value |= std::uint64_t(std::to_integer<unsigned char>(m_input[m_offset++]))
                             << shift;
                return true;
            }

            bool read_path(std::filesystem::path& path) {
                std::uint64_t length = 0;
                if(!read_u64(length) || length == 0 || length > MAX_PATH_BYTES
                    || length > m_input.size() - m_offset)
                    return false;
                std::string bytes(length, '\0');
                for(std::size_t i = 0; i < length; ++i)
                    bytes[i] = char(std::to_integer<unsigned char>(m_input[m_offset++]));
                if(bytes.find('\0') != std::string::npos)
                    return false;
                try {
                    path = std::filesystem::path(std::u8string(
                        reinterpret_cast<const char8_t*>(bytes.data()), bytes.size()));
                } catch(const std::filesystem::filesystem_error&) {
                    return false;
                }
                return valid_relative(path) && path.generic_u8string().size() == bytes.size();
            }

            bool read_words(std::vector<std::uint32_t>& words, const std::uint64_t count) {
                if(count < 5 || count > MAX_BYTES / 4 || count > (m_input.size() - m_offset) / 4)
                    return false;
                words.reserve(count);
                for(std::uint64_t i = 0; i < count; ++i) {
                    std::uint64_t word = 0;
                    for(unsigned shift = 0; shift < 32; shift += 8)
                        word |= std::uint64_t(std::to_integer<unsigned char>(m_input[m_offset++]))
                                << shift;
                    words.push_back(static_cast<std::uint32_t>(word));
                }
                return valid_words(words);
            }

            bool read_text(std::string& entry, const std::size_t maximum) {
                std::uint64_t length = 0;
                if(!read_u64(length) || length > maximum || length > m_input.size() - m_offset)
                    return false;
                entry.resize(length);
                for(std::size_t i = 0; i < length; ++i)
                    entry[i] = char(std::to_integer<unsigned char>(m_input[m_offset++]));
                return entry.find('\0') == std::string::npos;
            }

            [[nodiscard]] bool at_end() const { return m_offset == m_input.size(); }

        private:
            std::span<const std::byte> m_input;
            std::size_t m_offset = 0;
        };
    }

    std::optional<ShaderProgramArtifact> ShaderProgramArtifact::load(
        const std::filesystem::path& path, const AssetHandle handle) {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if(!input || input.tellg() < 0 || input.tellg() > std::streamoff(MAX_BYTES))
            return std::nullopt;
        const auto size = static_cast<std::size_t>(input.tellg());
        if(size < MAGIC.size() + 6 * 8)
            return std::nullopt;
        std::vector<std::byte> bytes(size);
        input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
            return std::nullopt;
        if(std::string_view(reinterpret_cast<const char*>(bytes.data()), MAGIC.size()) != MAGIC)
            return std::nullopt;
        Reader reader(std::span<const std::byte>(bytes).subspan(MAGIC.size()));
        std::uint64_t version = 0, identity = 0, count = 0, vertex_count = 0, fragment_count = 0;
        std::uint64_t payload_hash = 0;
        if(!reader.read_u64(version) || !reader.read_u64(identity) || !reader.read_u64(count)
            || !reader.read_u64(vertex_count) || !reader.read_u64(fragment_count)
            || !reader.read_u64(payload_hash) || version != FORMAT_VERSION
            || identity != handle.value() || !handle || count == 0 || count > MAX_INPUTS
            || hash_bytes(std::span<const std::byte>(bytes).subspan(MAGIC.size() + 6 * 8))
                   != payload_hash)
            return std::nullopt;
        ShaderProgramArtifact artifact;
        artifact.handle = handle;
        for(std::uint64_t i = 0; i < count; ++i) {
            ImportInputFingerprint file;
            if(!reader.read_path(file.relative_path) || !reader.read_u64(file.size)
                || !reader.read_u64(file.hash))
                return std::nullopt;
            artifact.inputs.files.push_back(std::move(file));
        }
        std::string material_text;
        if(!reader.read_text(artifact.vertex_entry, MAX_ENTRY_BYTES)
            || !reader.read_text(artifact.fragment_entry, MAX_ENTRY_BYTES)
            || artifact.vertex_entry.empty() || artifact.fragment_entry.empty()
            || !reader.read_text(material_text, MAX_MATERIAL_BYTES)
            || !reader.read_words(artifact.vertex_words, vertex_count)
            || !reader.read_words(artifact.fragment_words, fragment_count) || !reader.at_end())
            return std::nullopt;
        if(!material_text.empty()) {
            auto parsed = ShaderProgramSerializer{}.deserialize_material(material_text);
            if(!parsed)
                return std::nullopt;
            artifact.material = std::move(parsed).value();
        }
        return artifact;
    }

    Result<void> ShaderProgramArtifact::publish_atomic(const std::filesystem::path& path) const {
        if(!handle || inputs.files.empty() || inputs.files.size() > MAX_INPUTS
            || !valid_words(vertex_words) || !valid_words(fragment_words) || vertex_entry.empty()
            || vertex_entry.size() > MAX_ENTRY_BYTES || vertex_entry.find('\0') != std::string::npos
            || fragment_entry.empty() || fragment_entry.size() > MAX_ENTRY_BYTES
            || fragment_entry.find('\0') != std::string::npos)
            return Result<void>::failure("Invalid shader program artifact");
        if(vertex_words.size() + fragment_words.size() > MAX_BYTES / sizeof(std::uint32_t))
            return Result<void>::failure("Shader program artifact exceeds size limit");
        std::string material_text;
        if(material) {
            auto serialized = ShaderProgramSerializer{}.serialize_material(*material);
            if(!serialized)
                return Result<void>::failure(serialized.error());
            material_text = std::move(serialized).value();
            if(material_text.size() > MAX_MATERIAL_BYTES)
                return Result<void>::failure("Shader material metadata exceeds size limit");
        }
        std::vector<std::byte> payload;
        for(const auto& file : inputs.files) {
            const auto bytes = file.relative_path.generic_u8string();
            if(!valid_relative(file.relative_path) || bytes.empty()
                || bytes.size() > MAX_PATH_BYTES)
                return Result<void>::failure("Invalid shader program input path");
            append_u64(payload, bytes.size());
            for(const auto byte : bytes)
                payload.push_back(std::byte(byte));
            append_u64(payload, file.size);
            append_u64(payload, file.hash);
        }
        const auto append_entry = [&](const std::string& entry) {
            append_u64(payload, entry.size());
            for(const unsigned char character : entry)
                payload.push_back(std::byte(character));
        };
        append_entry(vertex_entry);
        append_entry(fragment_entry);
        append_entry(material_text);
        const auto append_words = [&](const std::vector<std::uint32_t>& words) {
            for(const auto word : words)
                for(unsigned shift = 0; shift < 32; shift += 8)
                    payload.push_back(std::byte((word >> shift) & 0xff));
        };
        append_words(vertex_words);
        append_words(fragment_words);
        if(payload.size() > MAX_BYTES - MAGIC.size() - 6 * 8)
            return Result<void>::failure("Shader program artifact exceeds size limit");
        std::vector<std::byte> output;
        output.reserve(MAGIC.size() + 6 * 8 + payload.size());
        for(const auto character : MAGIC)
            output.push_back(std::byte(character));
        append_u64(output, FORMAT_VERSION);
        append_u64(output, handle.value());
        append_u64(output, inputs.files.size());
        append_u64(output, vertex_words.size());
        append_u64(output, fragment_words.size());
        append_u64(output, hash_bytes(payload));
        output.insert(output.end(), payload.begin(), payload.end());
        return write_binary_file_atomic(path, output);
    }
}

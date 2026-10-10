#include "asset/artifact/shader_program_artifact.h"
#include "asset/serialization/shader_program_serializer.h"
#include "common/binary.h"
#include "common/file_io.h"

#include <cstddef>
#include <cstdint>
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

        bool read_text(Binary::Reader& reader, std::string& text, const std::size_t maximum) {
            return reader.read_string<std::uint64_t>(text, maximum)
                   && text.find('\0') == std::string::npos;
        }

        bool read_path(Binary::Reader& reader, std::filesystem::path& path) {
            std::string bytes;
            if(!read_text(reader, bytes, MAX_PATH_BYTES) || bytes.empty())
                return false;
            try {
                path = std::filesystem::path(
                    std::u8string(reinterpret_cast<const char8_t*>(bytes.data()), bytes.size()));
            } catch(const std::filesystem::filesystem_error&) {
                return false;
            }
            return valid_relative(path) && path.generic_u8string().size() == bytes.size();
        }

        bool read_words(
            Binary::Reader& reader, std::vector<std::uint32_t>& words, const std::uint64_t count) {
            if(count < 5 || count > MAX_BYTES / sizeof(std::uint32_t)
                || count > reader.remaining() / sizeof(std::uint32_t))
                return false;
            words.reserve(static_cast<std::size_t>(count));
            for(std::uint64_t index = 0; index < count; ++index) {
                std::uint32_t word = 0;
                if(!reader.read_u32(word))
                    return false;
                words.push_back(word);
            }
            return valid_words(words);
        }
    }

    std::optional<ShaderProgramArtifact> ShaderProgramArtifact::load(
        const std::filesystem::path& path, const AssetHandle handle) {
        auto file = read_binary_file(path, MAX_BYTES);
        if(!file || file.value().size() < MAGIC.size() + 6 * sizeof(std::uint64_t))
            return std::nullopt;
        const auto& bytes = file.value();
        Binary::Reader reader(bytes);
        if(!reader.match_bytes(std::as_bytes(std::span(MAGIC))))
            return std::nullopt;
        std::uint64_t version = 0, identity = 0, count = 0, vertex_count = 0, fragment_count = 0;
        std::uint64_t payload_hash = 0;
        if(!reader.read_u64(version) || !reader.read_u64(identity) || !reader.read_u64(count)
            || !reader.read_u64(vertex_count) || !reader.read_u64(fragment_count)
            || !reader.read_u64(payload_hash) || version != FORMAT_VERSION
            || identity != handle.value() || !handle || count == 0 || count > MAX_INPUTS
            || Binary::hash_bytes(std::span<const std::byte>(bytes).subspan(MAGIC.size() + 6 * 8))
                   != payload_hash)
            return std::nullopt;
        ShaderProgramArtifact artifact;
        artifact.handle = handle;
        for(std::uint64_t i = 0; i < count; ++i) {
            ImportInputFingerprint file;
            if(!read_path(reader, file.relative_path) || !reader.read_u64(file.size)
                || !reader.read_u64(file.hash))
                return std::nullopt;
            artifact.inputs.files.push_back(std::move(file));
        }
        std::string material_text;
        if(!read_text(reader, artifact.vertex_entry, MAX_ENTRY_BYTES)
            || !read_text(reader, artifact.fragment_entry, MAX_ENTRY_BYTES)
            || artifact.vertex_entry.empty() || artifact.fragment_entry.empty()
            || !read_text(reader, material_text, MAX_MATERIAL_BYTES)
            || !read_words(reader, artifact.vertex_words, vertex_count)
            || !read_words(reader, artifact.fragment_words, fragment_count)
            || reader.remaining() != 0)
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
        Binary::Writer payload;
        for(const auto& file : inputs.files) {
            const auto bytes = file.relative_path.generic_u8string();
            if(!valid_relative(file.relative_path) || bytes.empty()
                || bytes.size() > MAX_PATH_BYTES)
                return Result<void>::failure("Invalid shader program input path");
            payload.write_u64(bytes.size());
            payload.write_bytes(std::as_bytes(std::span(bytes)));
            payload.write_u64(file.size);
            payload.write_u64(file.hash);
        }
        const auto append_entry = [&](const std::string& entry) {
            payload.write_u64(entry.size());
            payload.write_bytes(std::as_bytes(std::span(entry)));
        };
        append_entry(vertex_entry);
        append_entry(fragment_entry);
        append_entry(material_text);
        const auto append_words = [&](const std::vector<std::uint32_t>& words) {
            for(const auto word : words)
                payload.write_u32(word);
        };
        append_words(vertex_words);
        append_words(fragment_words);
        if(payload.data().size() > MAX_BYTES - MAGIC.size() - 6 * 8)
            return Result<void>::failure("Shader program artifact exceeds size limit");
        Binary::Writer output(MAGIC.size() + 6 * sizeof(std::uint64_t) + payload.data().size());
        output.write_bytes(std::as_bytes(std::span(MAGIC)));
        output.write_u64(FORMAT_VERSION);
        output.write_u64(handle.value());
        output.write_u64(inputs.files.size());
        output.write_u64(vertex_words.size());
        output.write_u64(fragment_words.size());
        output.write_u64(Binary::hash_bytes(payload.data()));
        output.write_bytes(payload.data());
        return write_binary_file_atomic(path, output.data());
    }
}

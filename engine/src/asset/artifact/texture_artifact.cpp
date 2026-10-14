#include "asset/artifact/texture_artifact.h"
#include "common/binary.h"
#include "common/file_io.h"

#include <array>
#include <bit>
#include <fstream>
#include <limits>

namespace Comet {
    namespace {
        constexpr std::string_view MAGIC = "COMETTEX";
        constexpr uint64_t FORMAT_VERSION = 2;
        constexpr std::size_t FIELD_COUNT = 15;

        bool valid_source_path(const std::filesystem::path& path) {
            if(path.empty() || path.is_absolute())
                return false;
            for(const auto& part : path)
                if(part == "..")
                    return false;
            return true;
        }

        std::optional<uint64_t> pixel_bytes(const uint64_t width, const uint64_t height,
            const uint64_t levels, const bool mipmaps) {
            if(width == 0 || height == 0 || width > INT32_MAX || height > INT32_MAX)
                return std::nullopt;
            const auto expected_levels = mipmaps ? std::bit_width(std::max(width, height)) : 1;
            if(levels != expected_levels)
                return std::nullopt;
            uint64_t total = 0;
            for(unsigned mip = 0; mip < levels; ++mip) {
                const auto bytes =
                    std::max(width >> mip, uint64_t{1}) * std::max(height >> mip, uint64_t{1}) * 4;
                if(bytes > UINT64_MAX - total)
                    return std::nullopt;
                total += bytes;
            }
            return total;
        }
    }

    std::optional<TextureArtifact> TextureArtifact::load(const std::filesystem::path& path,
        const AssetHandle handle, const std::size_t memory_budget) {
        std::ifstream input(path, std::ios::binary);
        std::array<char, 8> magic{};
        if(!input.read(magic.data(), magic.size()) || std::string_view(magic.data(), 8) != MAGIC)
            return std::nullopt;
        std::array<std::byte, FIELD_COUNT * sizeof(uint64_t)> header{};
        if(!input.read(reinterpret_cast<char*>(header.data()), header.size()))
            return std::nullopt;
        Binary::Reader reader(header);
        std::array<uint64_t, FIELD_COUNT> fields{};
        for(auto& field : fields)
            if(!reader.read_u64(field))
                return std::nullopt;
        const auto [version, identity, importer, width, height, levels, color_space, flip_y,
            mipmaps, normal_map, source_size, source_hash, path_size, payload_size, payload_hash] =
            fields;
        if(version != FORMAT_VERSION || !handle || identity != handle.value()
            || importer > UINT32_MAX || color_space > 1 || flip_y > 1 || mipmaps > 1
            || normal_map > 1 || path_size == 0 || path_size > 16 * 1024
            || payload_size > memory_budget)
            return std::nullopt;
        const auto expected_bytes = pixel_bytes(width, height, levels, mipmaps != 0);
        if(!expected_bytes || payload_size != *expected_bytes)
            return std::nullopt;
        std::error_code error;
        const auto file_size = std::filesystem::file_size(path, error);
        if(error || file_size < MAGIC.size() + header.size() + path_size
            || file_size - MAGIC.size() - header.size() - path_size != payload_size)
            return std::nullopt;
        std::string source_path(path_size, '\0');
        if(!input.read(source_path.data(), source_path.size()))
            return std::nullopt;
        const std::filesystem::path relative{std::u8string(
            reinterpret_cast<const char8_t*>(source_path.data()), source_path.size())};
        if(!valid_source_path(relative))
            return std::nullopt;
        TextureArtifact artifact{.handle = handle,
            .importer_version = static_cast<uint32_t>(importer),
            .source = {relative, source_size, source_hash},
            .settings = {.color_space = static_cast<TextureColorSpace>(color_space),
                .flip_y = flip_y != 0,
                .generate_mipmaps = mipmaps != 0,
                .normal_map = normal_map != 0},
            .data = {.width = static_cast<int>(width),
                .height = static_cast<int>(height),
                .format = normal_map == 0 && color_space == uint64_t(TextureColorSpace::Srgb)
                              ? Format::R8G8B8A8_SRGB
                              : Format::R8G8B8A8_UNORM,
                .mip_levels = static_cast<uint32_t>(levels)}};
        artifact.data.pixels.resize(payload_size);
        auto bytes = std::as_bytes(std::span(artifact.data.pixels));
        if(!input.read(reinterpret_cast<char*>(artifact.data.pixels.data()), bytes.size())
            || input.peek() != std::ifstream::traits_type::eof()
            || Binary::hash_bytes(bytes) != payload_hash)
            return std::nullopt;
        return artifact;
    }

    Result<void> TextureArtifact::publish_atomic(const std::filesystem::path& path) const {
        const auto size =
            pixel_bytes(data.width, data.height, data.mip_levels, settings.generate_mipmaps);
        const auto color_space = uint64_t(settings.color_space);
        const auto expected_format =
            !settings.normal_map && settings.color_space == TextureColorSpace::Srgb
                ? Format::R8G8B8A8_SRGB
                : Format::R8G8B8A8_UNORM;
        const auto source_path = source.relative_path.generic_u8string();
        if(!handle || !size || *size != data.pixels.size() || color_space > 1 || data.cubemap
            || data.format != expected_format || !valid_source_path(source.relative_path)
            || source_path.size() > 16 * 1024)
            return Result<void>::failure("Invalid texture artifact");
        const auto pixels = std::as_bytes(std::span(data.pixels));
        const std::array<uint64_t, FIELD_COUNT> fields{FORMAT_VERSION, handle.value(),
            importer_version, static_cast<uint64_t>(data.width), static_cast<uint64_t>(data.height),
            data.mip_levels, color_space, settings.flip_y, settings.generate_mipmaps,
            settings.normal_map, source.size, source.hash, source_path.size(), pixels.size(),
            Binary::hash_bytes(pixels)};
        Binary::Writer writer(MAGIC.size() + sizeof(fields) + source_path.size());
        writer.write_bytes(std::as_bytes(std::span(MAGIC)));
        for(const auto field : fields)
            writer.write_u64(field);
        writer.write_bytes(std::as_bytes(std::span(source_path)));
        const std::array chunks{std::span<const std::byte>(writer.data()), pixels};
        return write_binary_file_atomic(path, chunks);
    }
}

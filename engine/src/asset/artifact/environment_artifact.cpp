#include "asset/artifact/environment_artifact.h"
#include "common/file_io.h"

#include <array>
#include <bit>
#include <fstream>

namespace Comet {
    static constexpr std::string_view MAGIC = "COMETENV";
    static constexpr uint64_t FORMAT_VERSION = 1;
    static constexpr uint64_t MAX_PIXELS = 256ull * 1024 * 1024;

    static uint64_t hash_pixels(const std::span<const uint8_t> pixels) {
        uint64_t hash = 14695981039346656037ull;
        for(const auto byte : pixels) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    static uint64_t pixel_bytes(const uint64_t size) {
        uint64_t bytes = 0;
        for(auto extent = size; extent > 0; extent /= 2)
            bytes += extent * extent * 6 * 8;
        return bytes;
    }

    std::optional<EnvironmentArtifact> EnvironmentArtifact::load(const std::filesystem::path& path,
        const AssetHandle handle, const std::size_t memory_budget) {
        std::ifstream input(path, std::ios::binary);
        std::array<char, 8> magic{};
        if(!input.read(magic.data(), magic.size()) || std::string_view(magic.data(), 8) != MAGIC)
            return std::nullopt;
        // Fixed little-endian header; bounded lengths are checked before allocating payloads.
        std::array<uint64_t, 10> fields{};
        for(auto& field : fields) {
            std::array<unsigned char, 8> bytes{};
            if(!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
                return std::nullopt;
            for(unsigned i = 0; i < 8; ++i)
                field |= uint64_t(bytes[i]) << (i * 8);
        }
        const auto [version, identity, importer, size, mips, source_size, source_hash, path_size,
            payload_size, payload_hash] = fields;
        if(version != FORMAT_VERSION || identity != handle.value() || importer > UINT32_MAX
            || size == 0 || size > 2048 || !std::has_single_bit(size)
            || mips != std::bit_width(size) || path_size == 0 || path_size > 16 * 1024
            || payload_size != pixel_bytes(size) || payload_size > MAX_PIXELS
            || payload_size > memory_budget)
            return std::nullopt;
        std::string source_path(path_size, '\0');
        if(!input.read(source_path.data(), source_path.size()))
            return std::nullopt;
        const std::filesystem::path relative{std::u8string(
            reinterpret_cast<const char8_t*>(source_path.data()), source_path.size())};
        if(relative.is_absolute() || relative.empty())
            return std::nullopt;
        for(const auto& part : relative)
            if(part == "..")
                return std::nullopt;
        EnvironmentArtifact artifact{.handle = handle,
            .importer_version = static_cast<uint32_t>(importer),
            .source = {relative, source_size, source_hash},
            .data = {.width = static_cast<int>(size),
                .height = static_cast<int>(size),
                .format = Format::R16G16B16A16_SFLOAT,
                .mip_levels = static_cast<uint32_t>(mips),
                .cubemap = true}};
        artifact.data.pixels.resize(payload_size);
        if(!input.read(reinterpret_cast<char*>(artifact.data.pixels.data()), payload_size)
            || input.peek() != std::ifstream::traits_type::eof()
            || hash_pixels(artifact.data.pixels) != payload_hash)
            return std::nullopt;
        return artifact;
    }

    Result<void> EnvironmentArtifact::publish_atomic(const std::filesystem::path& path) const {
        if(!handle || data.width <= 0 || data.width > 2048 || data.height != data.width
            || !std::has_single_bit(static_cast<unsigned>(data.width))
            || data.mip_levels != std::bit_width(static_cast<unsigned>(data.width)) || !data.cubemap
            || data.format != Format::R16G16B16A16_SFLOAT
            || data.pixels.size() != pixel_bytes(data.width))
            return Result<void>::failure("Invalid environment artifact");
        const auto source_path = source.relative_path.generic_u8string();
        if(source_path.empty() || source_path.size() > 16 * 1024)
            return Result<void>::failure("Invalid environment source path");
        const std::array<uint64_t, 10> fields{FORMAT_VERSION, handle.value(), importer_version,
            static_cast<uint64_t>(data.width), data.mip_levels, source.size, source.hash,
            source_path.size(), data.pixels.size(), hash_pixels(data.pixels)};
        std::vector<std::byte> bytes;
        bytes.reserve(MAGIC.size() + sizeof(fields) + source_path.size() + data.pixels.size());
        const auto append = [&](const auto values) {
            const auto span = std::as_bytes(values);
            bytes.insert(bytes.end(), span.begin(), span.end());
        };
        append(std::span(MAGIC));
        for(const auto field : fields)
            for(unsigned i = 0; i < 8; ++i)
                bytes.push_back(static_cast<std::byte>((field >> (i * 8)) & 0xff));
        append(std::span(source_path));
        append(std::span(data.pixels));
        return write_binary_file_atomic(path, bytes);
    }
}

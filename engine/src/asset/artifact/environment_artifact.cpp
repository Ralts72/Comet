#include "asset/artifact/environment_artifact.h"
#include "common/binary.h"
#include "common/file_io.h"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>

namespace Comet {
    static constexpr std::string_view MAGIC = "COMETENV";
    static constexpr uint64_t FORMAT_VERSION = 2;

    static auto textures(const EnvironmentData& data) {
        return std::array{&data.background, &data.irradiance, &data.specular, &data.brdf};
    }

    static uint64_t hash_pixels(const EnvironmentData& data) {
        uint64_t hash = Binary::HASH_SEED;
        for(const auto* texture : textures(data))
            hash = Binary::hash_bytes(std::as_bytes(std::span(texture->pixels)), hash);
        return hash;
    }

    static uint64_t pixel_bytes(const TextureData& texture) {
        uint64_t bytes = 0;
        auto extent = uint64_t(texture.width);
        for(uint32_t mip = 0; mip < texture.mip_levels; ++mip, extent /= 2)
            bytes += extent * extent * 8;
        if(texture.cubemap)
            bytes *= 6;
        return bytes;
    }

    static EnvironmentData shape(const uint32_t size) {
        const auto cube = [](uint32_t size, bool mips) {
            return TextureData{.width = static_cast<int>(size),
                .height = static_cast<int>(size),
                .format = Format::R16G16B16A16_SFLOAT,
                .mip_levels = mips ? static_cast<uint32_t>(std::bit_width(size)) : 1u,
                .cubemap = true};
        };
        return {cube(size, true),
            cube(std::min(size, uint32_t(EnvironmentData::IRRADIANCE_SIZE)), false),
            cube(std::min(size, uint32_t(EnvironmentData::SPECULAR_SIZE)), true),
            {.width = EnvironmentData::BRDF_SIZE,
                .height = EnvironmentData::BRDF_SIZE,
                .format = Format::R16G16B16A16_SFLOAT}};
    }

    static uint64_t payload_bytes(const EnvironmentData& data) {
        uint64_t bytes = 0;
        for(const auto* texture : textures(data))
            bytes += pixel_bytes(*texture);
        return bytes;
    }

    static std::optional<EnvironmentArtifact> load_artifact(const std::filesystem::path& path,
        const AssetHandle handle, const std::size_t memory_budget,
        const ImportInputFingerprint* expected_source, const uint32_t expected_importer) {
        std::ifstream input(path, std::ios::binary);
        std::array<char, 8> magic{};
        if(!input.read(magic.data(), magic.size()) || std::string_view(magic.data(), 8) != MAGIC)
            return std::nullopt;
        // 头部固定为小端编码；分配纹理存储前先拒绝旧版本或不完整产物。
        std::array<std::byte, 10 * sizeof(uint64_t)> header{};
        if(!input.read(reinterpret_cast<char*>(header.data()), header.size()))
            return std::nullopt;
        Binary::Reader reader(header);
        std::array<uint64_t, 10> fields{};
        for(auto& field : fields) {
            if(!reader.read_u64(field))
                return std::nullopt;
        }
        const auto [version, identity, importer, size, mips, source_size, source_hash, path_size,
            payload_size, payload_hash] = fields;
        if(version != FORMAT_VERSION || identity != handle.value() || importer > UINT32_MAX
            || size == 0 || size > 2048 || !std::has_single_bit(size)
            || mips != static_cast<uint64_t>(std::bit_width(size)) || path_size == 0
            || path_size > 16 * 1024 || payload_size > memory_budget)
            return std::nullopt;
        if(expected_source
            && (importer != expected_importer || source_size != expected_source->size
                || source_hash != expected_source->hash))
            return std::nullopt;
        auto data = shape(static_cast<uint32_t>(size));
        if(payload_size != payload_bytes(data))
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
        if(expected_source && relative != expected_source->relative_path)
            return std::nullopt;
        for(auto* texture : {&data.background, &data.irradiance, &data.specular, &data.brdf}) {
            texture->pixels.resize(pixel_bytes(*texture));
            if(!input.read(reinterpret_cast<char*>(texture->pixels.data()), texture->pixels.size()))
                return std::nullopt;
        }
        if(input.peek() != std::ifstream::traits_type::eof() || hash_pixels(data) != payload_hash)
            return std::nullopt;
        return EnvironmentArtifact{.handle = handle,
            .importer_version = static_cast<uint32_t>(importer),
            .source = {relative, source_size, source_hash},
            .data = std::move(data)};
    }

    std::optional<EnvironmentArtifact> EnvironmentArtifact::load(const std::filesystem::path& path,
        const AssetHandle handle, const std::size_t memory_budget) {
        return load_artifact(path, handle, memory_budget, nullptr, 0);
    }

    std::optional<EnvironmentArtifact> EnvironmentArtifact::load(const std::filesystem::path& path,
        const AssetHandle handle, const std::size_t memory_budget,
        const ImportInputFingerprint& source, const uint32_t importer_version) {
        return load_artifact(path, handle, memory_budget, &source, importer_version);
    }

    Result<void> EnvironmentArtifact::publish_atomic(const std::filesystem::path& path) const {
        const auto size = data.background.width;
        if(!handle || size <= 0 || size > 2048 || !std::has_single_bit(static_cast<uint32_t>(size)))
            return Result<void>::failure("Invalid environment artifact");
        const auto expected = shape(size);
        const auto actual_textures = textures(data);
        const auto expected_textures = textures(expected);
        for(size_t i = 0; i < actual_textures.size(); ++i) {
            const auto& actual = *actual_textures[i];
            const auto& target = *expected_textures[i];
            if(actual.width != target.width || actual.height != target.height
                || actual.format != target.format || actual.mip_levels != target.mip_levels
                || actual.cubemap != target.cubemap || actual.pixels.size() != pixel_bytes(target))
                return Result<void>::failure("Invalid environment texture layout");
        }
        const auto source_path = source.relative_path.generic_u8string();
        if(source_path.empty() || source_path.size() > 16 * 1024)
            return Result<void>::failure("Invalid environment source path");
        const std::array<uint64_t, 10> fields{FORMAT_VERSION, handle.value(), importer_version,
            static_cast<uint64_t>(size), data.background.mip_levels, source.size, source.hash,
            source_path.size(), payload_bytes(data), hash_pixels(data)};
        Binary::Writer writer(MAGIC.size() + sizeof(fields) + source_path.size());
        writer.write_bytes(std::as_bytes(std::span(MAGIC)));
        for(const auto field : fields)
            writer.write_u64(field);
        writer.write_bytes(std::as_bytes(std::span(source_path)));
        std::array<std::span<const std::byte>, 5> chunks{std::span<const std::byte>(writer.data())};
        for(std::size_t index = 0; index < actual_textures.size(); ++index)
            chunks[index + 1] = std::as_bytes(std::span(actual_textures[index]->pixels));
        return write_binary_file_atomic(path, chunks);
    }
}

#include "asset/data/navigation_data.h"
#include "common/binary.h"
#include "common/file_io.h"

#include <array>

namespace Comet {
    namespace {
        constexpr std::array MAGIC{'C', 'O', 'M', 'E', 'T', 'N', 'A', 'V'};
        constexpr uint32_t VERSION = 1;
        constexpr size_t HEADER_SIZE = 48;
        constexpr size_t MAX_BYTES = HEADER_SIZE + NavigationData::MAX_TILE_BYTES + 8;
    }

    Result<void> NavigationData::validate() const {
        if(!library_version || (reference_bytes != 4 && reference_bytes != 8)
            || (byte_order != 1 && byte_order != 2) || tile.empty() || tile.size() > MAX_TILE_BYTES)
            return Result<void>::failure("Invalid navigation product header or tile size");
        if(!Math::is_finite(query_extents) || Math::any_less_equal(query_extents, Math::Vec3(0))
            || Math::any_greater(query_extents, Math::Vec3(100000)))
            return Result<void>::failure("Invalid navigation query extents");
        return Result<void>::success();
    }

    Result<std::vector<std::byte>> NavigationData::encode() const {
        using Encoded = Result<std::vector<std::byte>>;
        if(auto valid = validate(); !valid)
            return Encoded::failure(valid.error());
        Binary::Writer writer(HEADER_SIZE + tile.size() + 8);
        writer.write_bytes(std::as_bytes(std::span(MAGIC)));
        for(const auto value : {VERSION, library_version, reference_bytes, byte_order})
            writer.write_u32(value);
        writer.write_u64(input_fingerprint);
        for(int axis = 0; axis < 3; ++axis)
            writer.write_float(query_extents[axis]);
        writer.write_u32(static_cast<uint32_t>(tile.size()));
        writer.write_bytes(tile);
        writer.write_u64(Binary::hash_bytes(writer.data()));
        return Encoded::success(writer.data());
    }

    Result<NavigationData> NavigationData::decode(const std::span<const std::byte> bytes) {
        using Decoded = Result<NavigationData>;
        if(bytes.size() < HEADER_SIZE + 8 || bytes.size() > MAX_BYTES)
            return Decoded::failure("Invalid navigation product size");
        Binary::Reader reader(bytes);
        NavigationData data;
        uint32_t version = 0, size = 0;
        if(!reader.match_bytes(std::as_bytes(std::span(MAGIC))) || !reader.read_u32(version)
            || version != VERSION || !reader.read_u32(data.library_version)
            || !reader.read_u32(data.reference_bytes) || !reader.read_u32(data.byte_order)
            || !reader.read_u64(data.input_fingerprint))
            return Decoded::failure("Unsupported navigation product header");
        for(int axis = 0; axis < 3; ++axis)
            if(!reader.read_float(data.query_extents[axis]))
                return Decoded::failure("Truncated navigation query extents");
        if(!reader.read_u32(size) || !size || size > MAX_TILE_BYTES
            || reader.remaining() != size_t(size) + 8)
            return Decoded::failure("Navigation tile and payload sizes differ");
        Binary::Reader checksum_reader(bytes.last(8));
        uint64_t checksum = 0;
        if(!checksum_reader.read_u64(checksum)
            || checksum != Binary::hash_bytes(bytes.first(bytes.size() - 8)))
            return Decoded::failure("Navigation product checksum mismatch");
        const auto tile = bytes.subspan(HEADER_SIZE, size);
        data.tile.assign(tile.begin(), tile.end());
        if(auto valid = data.validate(); !valid)
            return Decoded::failure(valid.error());
        return Decoded::success(std::move(data));
    }

    Result<void> NavigationData::save(const std::filesystem::path& path) const {
        auto bytes = encode();
        if(!bytes)
            return Result<void>::failure(bytes.error());
        return write_binary_file_atomic(path, bytes.value());
    }
    Result<NavigationData> NavigationData::load(const std::filesystem::path& path) {
        auto bytes = read_binary_file(path, MAX_BYTES);
        if(!bytes)
            return Result<NavigationData>::failure(bytes.error());
        return decode(bytes.value());
    }
}

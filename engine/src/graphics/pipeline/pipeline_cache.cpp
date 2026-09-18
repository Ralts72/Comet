#include "graphics/pipeline/pipeline_cache.h"

#include "common/file_io.h"
#include "diagnostics/logger.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace Comet {
    namespace {
        constexpr std::size_t HEADER_SIZE = 32;
        constexpr std::array MAGIC{std::byte{'C'}, std::byte{'M'}, std::byte{'V'}, std::byte{'K'},
            std::byte{'P'}, std::byte{'C'}, std::byte{'0'}, std::byte{'1'}};

        uint64_t read_le(std::span<const std::byte> bytes, std::size_t offset, std::size_t size) {
            uint64_t value = 0;
            for(std::size_t i = 0; i < size; ++i)
                value |= uint64_t(std::to_integer<uint8_t>(bytes[offset + i])) << (i * 8);
            return value;
        }

        void write_le(std::span<std::byte> bytes, std::size_t offset, uint64_t value,
            std::size_t size) {
            for(std::size_t i = 0; i < size; ++i)
                bytes[offset + i] = std::byte((value >> (i * 8)) & 0xff);
        }

        uint64_t checksum(std::span<const std::byte> bytes) {
            uint64_t value = 14695981039346656037ull;
            for(const auto byte : bytes) {
                value ^= std::to_integer<uint8_t>(byte);
                value *= 1099511628211ull;
            }
            return value;
        }

        bool valid_header(std::span<const std::byte> data) {
            return data.size() >= HEADER_SIZE && data.size() <= PipelineCache::MAX_DATA_SIZE
                && read_le(data, 0, 4) == HEADER_SIZE
                && read_le(data, 4, 4) == VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
        }

        Result<void> validate_device(std::span<const std::byte> data,
            const vk::PhysicalDeviceProperties& properties) {
            if(!valid_header(data))
                return Result<void>::failure(
                    "Invalid Vulkan pipeline cache size or header version");
            if(read_le(data, 8, 4) != properties.vendorID
                || read_le(data, 12, 4) != properties.deviceID)
                return Result<void>::failure("Pipeline cache device does not match");
            for(std::size_t i = 0; i < VK_UUID_SIZE; ++i)
                if(std::to_integer<uint8_t>(data[16 + i]) != properties.pipelineCacheUUID[i])
                    return Result<void>::failure("Pipeline cache UUID does not match");
            return Result<void>::success();
        }

        std::filesystem::path cache_path(const std::filesystem::path& directory,
            const vk::PhysicalDeviceProperties& properties) {
            std::ostringstream name;
            name << std::hex << properties.vendorID << '-' << properties.deviceID << '-';
            for(const auto byte : properties.pipelineCacheUUID)
                name << std::setw(2) << std::setfill('0') << unsigned(byte);
            return directory / (name.str() + ".bin");
        }

        Result<std::vector<std::byte>> read_file(const std::filesystem::path& path) {
            using ReadResult = Result<std::vector<std::byte>>;
            std::error_code error;
            if(!std::filesystem::is_regular_file(path, error) || error)
                return ReadResult::failure("Pipeline cache is not a readable regular file");
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if(!input)
                return ReadResult::failure("Cannot open pipeline cache");
            const auto size = input.tellg();
            if(size < std::streamoff(HEADER_SIZE)
                || size > std::streamoff(PipelineCache::MAX_DATA_SIZE + HEADER_SIZE))
                return ReadResult::failure("Pipeline cache file size is outside its limit");
            std::vector<std::byte> contents(static_cast<std::size_t>(size));
            input.seekg(0);
            input.read(reinterpret_cast<char*>(contents.data()), size);
            if(!input || input.peek() != std::char_traits<char>::eof())
                return ReadResult::failure(
                    "Pipeline cache changed or could not be read completely");
            return ReadResult::success(std::move(contents));
        }
    }

    Result<std::vector<std::byte>> PipelineCache::encode(std::span<const std::byte> data) {
        if(!valid_header(data))
            return Result<std::vector<std::byte>>::failure("Invalid Vulkan pipeline cache header");
        std::vector<std::byte> file(HEADER_SIZE + data.size());
        std::ranges::copy(MAGIC, file.begin());
        write_le(file, 8, 1, 4);
        write_le(file, 12, HEADER_SIZE, 4);
        write_le(file, 16, data.size(), 8);
        write_le(file, 24, checksum(data), 8);
        std::ranges::copy(data, file.begin() + HEADER_SIZE);
        return Result<std::vector<std::byte>>::success(std::move(file));
    }

    Result<std::span<const std::byte>> PipelineCache::decode(
        std::span<const std::byte> file, const vk::PhysicalDeviceProperties& properties) {
        using DecodeResult = Result<std::span<const std::byte>>;
        if(file.size() < HEADER_SIZE || file.size() > MAX_DATA_SIZE + HEADER_SIZE
            || !std::ranges::equal(file.first(MAGIC.size()), MAGIC)
            || read_le(file, 8, 4) != 1 || read_le(file, 12, 4) != HEADER_SIZE)
            return DecodeResult::failure("Invalid Comet pipeline cache envelope");
        const auto data = file.subspan(HEADER_SIZE);
        if(read_le(file, 16, 8) != data.size() || read_le(file, 24, 8) != checksum(data))
            return DecodeResult::failure("Pipeline cache length or checksum mismatch");
        if(auto valid = validate_device(data, properties); !valid)
            return DecodeResult::failure(valid.error());
        return DecodeResult::success(data);
    }

    PipelineCache::PipelineCache(vk::Device device, vk::UniquePipelineCache cache,
        vk::PhysicalDeviceProperties properties)
        : m_device(device), m_cache(std::move(cache)), m_properties(properties) {}

    Result<void, GraphicsError> PipelineCache::restore(const std::filesystem::path& directory) {
        using RestoreResult = Result<void, GraphicsError>;
        m_path = directory.empty() ? std::filesystem::path{} : cache_path(directory, m_properties);
        m_load_status = directory.empty() ? LoadStatus::Disabled : LoadStatus::Missing;
        if(directory.empty())
            return RestoreResult::success();
        std::error_code error;
        const bool exists = std::filesystem::exists(m_path, error);
        if(!exists && !error)
            return RestoreResult::success();
        m_load_status = LoadStatus::Rejected;
        auto file = read_file(m_path);
        auto data = file ? decode(file.value(), m_properties)
                         : Result<std::span<const std::byte>>::failure(file.error());
        if(!data) {
            LOG_WARN("Pipeline cache '{}' rejected: {}", m_path.string(), data.error());
            return RestoreResult::success();
        }
        vk::PipelineCacheCreateInfo info;
        info.initialDataSize = data.value().size();
        info.pInitialData = data.value().data();
        vk::PipelineCache candidate;
        auto result = m_device.createPipelineCache(&info, nullptr, &candidate);
        if(result != vk::Result::eSuccess) {
            const GraphicsError failure{"Cannot restore driver pipeline cache", result};
            if(failure.is_device_lost() || failure.is_out_of_memory())
                return RestoreResult::failure(failure);
            LOG_WARN("Driver rejected pipeline cache '{}': {}", m_path.string(),
                vk::to_string(result));
            return RestoreResult::success();
        }
        result = m_device.mergePipelineCaches(get(), 1, &candidate);
        m_device.destroyPipelineCache(candidate);
        if(result != vk::Result::eSuccess)
            return RestoreResult::failure({"Cannot merge driver pipeline cache", result});
        m_load_status = LoadStatus::Restored;
        return RestoreResult::success();
    }

    PipelineCache::~PipelineCache() {
        if(auto saved = save(); !saved)
            std::fprintf(
                stderr, "Pipeline cache save skipped: %s\n", saved.error().message.c_str());
    }

    Result<void, Error> PipelineCache::save() const {
        using SaveResult = Result<void, Error>;
        if(m_path.empty())
            return SaveResult::success();
        // owner 串行，仍有界处理驱动返回 INCOMPLETE。
        for(int attempt = 0; attempt < 3; ++attempt) {
            std::size_t size = 0;
            const auto query = m_device.getPipelineCacheData(get(), &size, nullptr);
            if(query != vk::Result::eSuccess)
                return SaveResult::failure(
                    GraphicsError{"Cannot query pipeline cache", query}.as_error());
            if(size < HEADER_SIZE || size > MAX_DATA_SIZE)
                return SaveResult::failure({"Invalid pipeline cache data size"});
            std::vector<std::byte> data(size);
            const auto result = m_device.getPipelineCacheData(get(), &size, data.data());
            if(result == vk::Result::eIncomplete)
                continue;
            if(result != vk::Result::eSuccess)
                return SaveResult::failure(
                    GraphicsError{"Cannot read pipeline cache", result}.as_error());
            if(size > data.size())
                return SaveResult::failure({"Pipeline cache size exceeds allocated storage"});
            data.resize(size);
            if(auto valid = validate_device(data, m_properties); !valid)
                return SaveResult::failure({valid.error()});
            auto file = encode(data);
            if(!file)
                return SaveResult::failure({file.error()});
            auto written = write_binary_file_atomic(m_path, file.value());
            return written ? SaveResult::success() : SaveResult::failure({written.error()});
        }
        return SaveResult::failure({"Pipeline cache data remained incomplete"});
    }
}

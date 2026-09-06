#include "graphics/pipeline/pipeline_cache.h"

#include "graphics/device.h"
#include "common/file_io.h"
#include "diagnostics/logger.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace Comet {
    namespace {
        constexpr size_t HEADER_SIZE = 32;
        constexpr std::array MAGIC{std::byte{'C'}, std::byte{'M'}, std::byte{'V'},
            std::byte{'K'}, std::byte{'P'}, std::byte{'C'}, std::byte{'0'},
            std::byte{'1'}};

        uint64_t read_le(std::span<const std::byte> bytes, size_t offset, size_t size) {
            if(offset > bytes.size() || size > bytes.size() - offset)
                throw std::invalid_argument("Truncated pipeline cache header");
            uint64_t value = 0;
            for(size_t index = 0; index < size; ++index)
                value |= uint64_t(std::to_integer<uint8_t>(bytes[offset + index]))
                         << (index * 8);
            return value;
        }

        void write_le(
            std::span<std::byte> bytes, size_t offset, uint64_t value, size_t size) {
            for(size_t index = 0; index < size; ++index)
                bytes[offset + index] = std::byte((value >> (index * 8)) & 0xff);
        }

        uint64_t checksum(std::span<const std::byte> bytes) {
            uint64_t value = 14695981039346656037ull;
            for(const auto byte : bytes) {
                value ^= std::to_integer<uint8_t>(byte);
                value *= 1099511628211ull;
            }
            return value;
        }

        void validate_header(std::span<const std::byte> data) {
            if(data.size() < HEADER_SIZE || data.size() > PipelineCache::MAX_DATA_SIZE
                || read_le(data, 0, 4) != HEADER_SIZE
                || read_le(data, 4, 4) != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
                throw std::invalid_argument(
                    "Invalid Vulkan pipeline cache size or header version");
        }

        void validate_device(std::span<const std::byte> data,
            const vk::PhysicalDeviceProperties& properties) {
            validate_header(data);
            if(read_le(data, 8, 4) != properties.vendorID
                || read_le(data, 12, 4) != properties.deviceID)
                throw std::invalid_argument("Pipeline cache device does not match");
            for(size_t index = 0; index < VK_UUID_SIZE; ++index) {
                if(std::to_integer<uint8_t>(data[16 + index])
                    != properties.pipelineCacheUUID[index])
                    throw std::invalid_argument("Pipeline cache UUID does not match");
            }
        }

        std::filesystem::path cache_path(const std::filesystem::path& directory,
            const vk::PhysicalDeviceProperties& properties) {
            std::ostringstream name;
            name << std::hex << properties.vendorID << '-' << properties.deviceID << '-';
            for(const auto byte : properties.pipelineCacheUUID)
                name << std::setw(2) << std::setfill('0') << unsigned(byte);
            name << ".bin";
            return directory / name.str();
        }

        std::vector<std::byte> read_file(const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if(!input)
                throw std::runtime_error("Cannot open pipeline cache");
            const auto size = input.tellg();
            if(size < std::streamoff(HEADER_SIZE)
                || size > std::streamoff(PipelineCache::MAX_DATA_SIZE + HEADER_SIZE))
                throw std::invalid_argument(
                    "Pipeline cache file size is outside its limit");
            std::vector<std::byte> contents(static_cast<size_t>(size));
            input.seekg(0);
            input.read(reinterpret_cast<char*>(contents.data()), size);
            if(!input || input.peek() != std::char_traits<char>::eof())
                throw std::runtime_error(
                    "Pipeline cache changed or could not be read completely");
            return contents;
        }
    }

    std::vector<std::byte> PipelineCache::encode(std::span<const std::byte> data) {
        validate_header(data);
        std::vector<std::byte> file(HEADER_SIZE + data.size());
        std::ranges::copy(MAGIC, file.begin());
        write_le(file, 8, 1, 4);
        write_le(file, 12, HEADER_SIZE, 4);
        write_le(file, 16, data.size(), 8);
        write_le(file, 24, checksum(data), 8);
        std::ranges::copy(data, file.begin() + HEADER_SIZE);
        return file;
    }

    std::span<const std::byte> PipelineCache::decode(
        std::span<const std::byte> file, const vk::PhysicalDeviceProperties& properties) {
        if(file.size() < HEADER_SIZE || file.size() > MAX_DATA_SIZE + HEADER_SIZE
            || !std::ranges::equal(file.first(MAGIC.size()), MAGIC)
            || read_le(file, 8, 4) != 1 || read_le(file, 12, 4) != HEADER_SIZE)
            throw std::invalid_argument("Invalid Comet pipeline cache envelope");
        const auto data = file.subspan(HEADER_SIZE);
        if(read_le(file, 16, 8) != data.size() || read_le(file, 24, 8) != checksum(data))
            throw std::invalid_argument("Pipeline cache length or checksum mismatch");
        validate_device(data, properties);
        return data;
    }

    PipelineCache::PipelineCache(Device& device,
        const vk::PhysicalDeviceProperties& properties,
        const std::filesystem::path& directory)
        : m_device(device), m_properties(properties) {
        std::vector<std::byte> file;
        std::span<const std::byte> data;
        if(!directory.empty()) {
            m_path = cache_path(directory, properties);
            m_load_status = LoadStatus::Missing;
            try {
                if(std::filesystem::exists(m_path)) {
                    file = read_file(m_path);
                    data = decode(file, properties);
                    m_load_status = LoadStatus::Restored;
                }
            } catch(const std::exception& error) {
                m_load_status = LoadStatus::Rejected;
                LOG_WARN("Pipeline cache '{}' rejected; using an empty cache: {}",
                    m_path.string(), error.what());
            }
        }
        vk::PipelineCacheCreateInfo info;
        info.initialDataSize = data.size();
        info.pInitialData = data.data();
        try {
            m_cache = device.get().createPipelineCacheUnique(info);
        } catch(const vk::SystemError& error) {
            if(data.empty())
                throw;
            m_load_status = LoadStatus::Rejected;
            LOG_WARN("Driver rejected pipeline cache '{}'; retrying empty: {}",
                m_path.string(), error.what());
            m_cache =
                device.get().createPipelineCacheUnique(vk::PipelineCacheCreateInfo{});
        }
        if(m_load_status == LoadStatus::Restored)
            LOG_INFO("Restored Vulkan pipeline cache '{}' ({} bytes)", m_path.string(),
                data.size());
    }

    PipelineCache::~PipelineCache() {
        try {
            save();
        } catch(...) {
            // 可丢弃缓存或日志后端异常不能中断 Device 的关闭。
        }
    }

    bool PipelineCache::save() const {
        if(m_path.empty())
            return false;
        try {
            // owner 串行，正常只需一次；仍有界处理驱动返回 INCOMPLETE。
            for(int attempt = 0; attempt < 3; ++attempt) {
                size_t size = 0;
                const auto query =
                    m_device.get().getPipelineCacheData(get(), &size, nullptr);
                if(query != vk::Result::eSuccess || size < HEADER_SIZE
                    || size > MAX_DATA_SIZE)
                    throw std::runtime_error(
                        "Invalid pipeline cache data size or query result");
                std::vector<std::byte> data(size);
                const auto result =
                    m_device.get().getPipelineCacheData(get(), &size, data.data());
                if(result == vk::Result::eIncomplete)
                    continue;
                if(result != vk::Result::eSuccess || size > data.size())
                    throw std::runtime_error("Cannot read Vulkan pipeline cache data");
                data.resize(size);
                validate_device(data, m_properties);
                write_binary_file_atomic(m_path, encode(data));
                LOG_DEBUG("Saved Vulkan pipeline cache '{}' ({} bytes)", m_path.string(),
                    data.size());
                return true;
            }
            throw std::runtime_error("Pipeline cache data remained incomplete");
        } catch(const std::exception& error) {
            LOG_WARN("Pipeline cache save skipped for '{}': {}", m_path.string(),
                error.what());
            return false;
        }
    }
}

#pragma once

#include "common/export.h"
#include "graphics/result.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace Comet {
    // Device owner 串行访问；磁盘数据不拥有 Pipeline 对象。
    class COMET_API PipelineCache {
    public:
        enum class LoadStatus { Disabled, Missing, Rejected, Restored };
        static constexpr std::size_t MAX_DATA_SIZE = 64 * 1024 * 1024;

        PipelineCache(vk::Device device, vk::UniquePipelineCache cache,
            vk::PhysicalDeviceProperties properties);
        ~PipelineCache();
        PipelineCache(const PipelineCache&) = delete;
        PipelineCache& operator=(const PipelineCache&) = delete;

        [[nodiscard]] vk::PipelineCache get() const { return *m_cache; }
        [[nodiscard]] LoadStatus get_load_status() const { return m_load_status; }
        [[nodiscard]] const std::filesystem::path& get_path() const { return m_path; }

        // 启动时合并磁盘缓存；坏文件保留当前内存缓存，设备/内存错误返回失败。
        [[nodiscard]] Result<void, GraphicsError> restore(const std::filesystem::path& directory);
        [[nodiscard]] Result<void, Error> save() const;

        [[nodiscard]] static Result<std::vector<std::byte>> encode(std::span<const std::byte> data);
        // 借用输入存储，调用者必须保持 file 存活。
        [[nodiscard]] static Result<std::span<const std::byte>> decode(
            std::span<const std::byte> file, const vk::PhysicalDeviceProperties& properties);

    private:
        vk::Device m_device;
        vk::UniquePipelineCache m_cache;
        vk::PhysicalDeviceProperties m_properties;
        std::filesystem::path m_path;
        LoadStatus m_load_status = LoadStatus::Disabled;
    };
}

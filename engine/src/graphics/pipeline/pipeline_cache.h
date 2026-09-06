#pragma once

#include "common/export.h"
#include <vulkan/vulkan.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace Comet {
    class Device;

    // owner 线程串行使用；磁盘 blob 只加速驱动，不保存或拥有 Pipeline 对象。
    class COMET_API PipelineCache {
    public:
        enum class LoadStatus { Disabled, Missing, Rejected, Restored };
        static constexpr size_t MAX_DATA_SIZE = 64 * 1024 * 1024;

        PipelineCache(Device& device, const vk::PhysicalDeviceProperties& properties,
            const std::filesystem::path& directory = {});
        ~PipelineCache();
        PipelineCache(const PipelineCache&) = delete;
        PipelineCache& operator=(const PipelineCache&) = delete;
        PipelineCache(PipelineCache&&) = delete;
        PipelineCache& operator=(PipelineCache&&) = delete;

        [[nodiscard]] vk::PipelineCache get() const { return *m_cache; }
        [[nodiscard]] LoadStatus get_load_status() const { return m_load_status; }
        [[nodiscard]] const std::filesystem::path& get_path() const { return m_path; }
        // 关闭时自动保存，也可由 owner 在编译批次后调用；不在逐帧路径写磁盘。
        bool save() const;

        [[nodiscard]] static std::vector<std::byte> encode(
            std::span<const std::byte> data);
        // 返回输入文件内的借用片段；调用者须保持 file 存活。
        [[nodiscard]] static std::span<const std::byte> decode(
            std::span<const std::byte> file,
            const vk::PhysicalDeviceProperties& properties);

    private:
        Device& m_device;
        vk::UniquePipelineCache m_cache;
        vk::PhysicalDeviceProperties m_properties;
        std::filesystem::path m_path;
        LoadStatus m_load_status = LoadStatus::Disabled;
    };
}

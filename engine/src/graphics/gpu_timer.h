#pragma once

#include "common/export.h"
#include "graphics/result.h"

#include <memory>
#include <optional>
#include <span>

namespace Comet {
    class Device;
    class CommandBuffer;

    class COMET_API GpuTimer {
    public:
        struct Capabilities {
            uint32_t valid_bits = 0;
            double period_nanoseconds = 0;
        };
        struct Sample {
            uint64_t ticks = 0;
            uint64_t available = 0;
        };
        enum class Boundary { Begin, End };

        [[nodiscard]] static Capabilities capabilities(const Device& device, uint32_t family);
        [[nodiscard]] static std::optional<double> elapsed_ms(
            uint64_t start, uint64_t end, Capabilities capabilities);
        [[nodiscard]] static Result<std::shared_ptr<GpuTimer>, GraphicsError> create(
            Device& device, uint32_t count);
        void reset(CommandBuffer& command) const;
        void write(CommandBuffer& command, uint32_t index, Boundary boundary) const;
        // 不等待；成功值 false 表示结果尚未全部可用，调用方另行确认提交完成。
        [[nodiscard]] Result<bool, GraphicsError> read(std::span<Sample> samples) const;

    private:
        GpuTimer(Device& device, vk::UniqueQueryPool pool, uint32_t count);
        Device& m_device;
        vk::UniqueQueryPool m_pool;
        uint32_t m_count;
    };
}

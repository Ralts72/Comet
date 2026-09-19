#include "graphics/gpu_timer.h"
#include "graphics/command/command_buffer.h"
#include "graphics/creation.h"
#include "graphics/device.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace Comet {
    GpuTimer::GpuTimer(Device& device, vk::UniqueQueryPool pool, uint32_t count)
        : m_device(device), m_pool(std::move(pool)), m_count(count) {}

    GpuTimer::Capabilities GpuTimer::capabilities(const Device& device, uint32_t family) {
        const auto physical = device.get_capability().physical_device;
        const auto families = physical.getQueueFamilyProperties();
        if(family >= families.size())
            return {};
        return {
            families[family].timestampValidBits, physical.getProperties().limits.timestampPeriod};
    }

    std::optional<double> GpuTimer::elapsed_ms(
        uint64_t start, uint64_t end, Capabilities capabilities) {
        const auto [bits, period] = capabilities;
        if(bits == 0 || bits > 64 || !std::isfinite(period) || period <= 0)
            return std::nullopt;
        const uint64_t mask = bits == 64 ? ~uint64_t(0) : (uint64_t(1) << bits) - 1;
        const double elapsed = double((end - start) & mask) * period / 1000000.0;
        return std::isfinite(elapsed) ? std::optional(elapsed) : std::nullopt;
    }

    Result<std::shared_ptr<GpuTimer>, GraphicsError> GpuTimer::create(
        Device& device, uint32_t count) {
        using Creation = Result<std::shared_ptr<GpuTimer>, GraphicsError>;
        if(count == 0)
            return Creation::failure({"Timestamp query pool requires a nonzero capacity"});
        const vk::QueryPoolCreateInfo info({}, vk::QueryType::eTimestamp, count);
        auto pool = Graphics::create_handle<vk::QueryPool>(
            device.get(), "Create timestamp pool", [&](vk::QueryPool* output) noexcept {
                return device.get().createQueryPool(&info, nullptr, output);
            });
        if(!pool)
            return Creation::failure(pool.error());
        return Creation::success(
            std::shared_ptr<GpuTimer>(new GpuTimer(device, std::move(pool).value(), count)));
    }

    void GpuTimer::reset(CommandBuffer& command) const {
        command.get().resetQueryPool(*m_pool, 0, m_count);
    }

    void GpuTimer::write(CommandBuffer& command, uint32_t index, Boundary boundary) const {
        assert(index < m_count);
        auto stage = vk::PipelineStageFlagBits2::eAllCommands;
        if(boundary == Boundary::Begin)
            stage = vk::PipelineStageFlagBits2::eTopOfPipe;
        command.get().writeTimestamp2(stage, *m_pool, index);
    }

    Result<bool, GraphicsError> GpuTimer::read(std::span<Sample> samples) const {
        static_assert(sizeof(Sample) == 2 * sizeof(uint64_t));
        if(samples.empty() || samples.size() > m_count)
            return Result<bool, GraphicsError>::failure({"Invalid timestamp result range"});
        const auto result =
            m_device.get().getQueryPoolResults(*m_pool, 0, static_cast<uint32_t>(samples.size()),
                samples.size_bytes(), samples.data(), sizeof(Sample),
                vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);
        if(result == vk::Result::eNotReady)
            return Result<bool, GraphicsError>::success(false);
        if(result != vk::Result::eSuccess)
            return Result<bool, GraphicsError>::failure({"Cannot read GPU timestamps", result});
        return Result<bool, GraphicsError>::success(std::ranges::all_of(
            samples, [](const Sample& sample) { return sample.available != 0; }));
    }
}

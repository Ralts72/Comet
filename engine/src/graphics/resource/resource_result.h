#pragma once

#include "graphics/vk_common.h"

#include <optional>
#include <utility>

namespace Comet {
    template<typename T>
    class GpuResourceResult {
    public:
        GpuResourceResult() = default;

        [[nodiscard]] static GpuResourceResult success(T value) {
            return GpuResourceResult(
                std::optional<T>(std::move(value)),
                vk::Result::eSuccess);
        }

        [[nodiscard]] static GpuResourceResult failure(
            const vk::Result result) {
            return GpuResourceResult(
                std::nullopt,
                result == vk::Result::eSuccess
                    ? vk::Result::eErrorUnknown
                    : result);
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return m_value.has_value();
        }

        [[nodiscard]] T& value() & { return m_value.value(); }
        [[nodiscard]] const T& value() const & { return m_value.value(); }
        [[nodiscard]] T&& value() && { return std::move(m_value).value(); }

        [[nodiscard]] vk::Result result() const noexcept { return m_result; }

    private:
        GpuResourceResult(
            std::optional<T> value,
            const vk::Result result)
            : m_value(std::move(value)), m_result(result) {}

        std::optional<T> m_value;
        vk::Result m_result = vk::Result::eErrorUnknown;
    };

    template<>
    class GpuResourceResult<void> {
    public:
        GpuResourceResult() = default;

        [[nodiscard]] static GpuResourceResult success() {
            return GpuResourceResult(vk::Result::eSuccess);
        }

        [[nodiscard]] static GpuResourceResult failure(
            const vk::Result result) {
            return GpuResourceResult(
                result == vk::Result::eSuccess
                    ? vk::Result::eErrorUnknown
                    : result);
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return m_result == vk::Result::eSuccess;
        }

        [[nodiscard]] vk::Result result() const noexcept { return m_result; }

    private:
        explicit GpuResourceResult(const vk::Result result)
            : m_result(result) {}

        vk::Result m_result = vk::Result::eErrorUnknown;
    };
}

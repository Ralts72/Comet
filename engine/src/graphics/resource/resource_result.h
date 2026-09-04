#pragma once

#include "common/export.h"
#include "graphics/vk_common.h"

#include <optional>
#include <utility>

namespace Comet {
    [[noreturn]] COMET_API void fail_gpu_resource_result_value_access(vk::Result result);

    template<typename T> class GpuResourceResult {
    public:
        [[nodiscard]] static GpuResourceResult success(T value) {
            return GpuResourceResult(
                std::optional<T>(std::move(value)), vk::Result::eSuccess);
        }

        [[nodiscard]] static GpuResourceResult failure(const vk::Result result) {
            return GpuResourceResult(std::nullopt,
                result == vk::Result::eSuccess ? vk::Result::eErrorUnknown : result);
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return m_value.has_value();
        }

        [[nodiscard]] T& value() & {
            require_value();
            return *m_value;
        }

        [[nodiscard]] const T& value() const& {
            require_value();
            return *m_value;
        }

        [[nodiscard]] T&& value() && {
            require_value();
            return std::move(*m_value);
        }

        [[nodiscard]] vk::Result result() const noexcept { return m_result; }

    private:
        void require_value() const {
            if(!m_value) {
                fail_gpu_resource_result_value_access(m_result);
            }
        }

        GpuResourceResult(std::optional<T> value, const vk::Result result)
            : m_value(std::move(value)), m_result(result) {}

        std::optional<T> m_value;
        vk::Result m_result = vk::Result::eErrorUnknown;
    };

    template<> class COMET_API GpuResourceResult<void> {
    public:
        [[nodiscard]] static GpuResourceResult success();

        [[nodiscard]] static GpuResourceResult failure(vk::Result result);

        [[nodiscard]] explicit operator bool() const noexcept {
            return m_result == vk::Result::eSuccess;
        }

        [[nodiscard]] vk::Result result() const noexcept { return m_result; }

    private:
        explicit GpuResourceResult(const vk::Result result) : m_result(result) {}

        vk::Result m_result = vk::Result::eErrorUnknown;
    };
}

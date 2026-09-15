#include "graphics/result.h"

#include "diagnostics/logger.h"

namespace Comet {
    bool is_device_lost(const Error& error) {
        return error.code == vk::make_error_code(vk::Result::eErrorDeviceLost);
    }

    Error GraphicsError::as_error() const {
        return {message, result ? vk::make_error_code(*result) : std::error_code{}};
    }

    [[noreturn]] void fail_gpu_resource_result_value_access(const vk::Result result) {
        LOG_FATAL("Attempted to access failed GPU resource result: {}", vk::to_string(result));
    }

    GpuResourceResult<void> GpuResourceResult<void>::success() {
        return GpuResourceResult(vk::Result::eSuccess);
    }

    GpuResourceResult<void> GpuResourceResult<void>::failure(const vk::Result result) {
        return GpuResourceResult(
            result == vk::Result::eSuccess ? vk::Result::eErrorUnknown : result);
    }

}

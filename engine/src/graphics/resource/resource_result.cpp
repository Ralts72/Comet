#include "graphics/resource/resource_result.h"

#include "diagnostics/logger.h"

namespace Comet {
    [[noreturn]] void fail_gpu_resource_result_value_access(
        const vk::Result result) {
        LOG_FATAL(
            "Attempted to access failed GPU resource result: {}",
            vk::to_string(result));
    }

    GpuResourceResult<void> GpuResourceResult<void>::success() {
        return GpuResourceResult(vk::Result::eSuccess);
    }

    GpuResourceResult<void> GpuResourceResult<void>::failure(
        const vk::Result result) {
        return GpuResourceResult(
            result == vk::Result::eSuccess
                ? vk::Result::eErrorUnknown
                : result);
    }

}

#pragma once

#include "common/error.h"
#include "common/export.h"

namespace Comet {
    // Backend-free classification; the original error_code is retained across asset boundaries.
    [[nodiscard]] COMET_API bool is_device_lost(const Error& error);
}

#pragma once

#include "common/export.h"
#include "common/result.h"
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace Comet {
    // UTF-8 参数逐项交付，合并 stdout/stderr；同步等待，须在宿主后台任务中调用。
    COMET_API Result<int> run_process(std::span<const std::string> arguments,
        const std::function<void(std::string_view)>& on_output = {});
    COMET_API std::wstring quote_windows_argument(std::wstring_view argument);
}

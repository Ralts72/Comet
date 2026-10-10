#pragma once

#include "config/config.h"
#include "common/export.h"
#include "common/result.h"

#include <string>

namespace Comet {
    class COMET_API ConfigLoader final {
    public:
        // Profile 只覆盖开发者参数；项目默认值和玩家选择由各设置模块保存。
        [[nodiscard]] Result<Config> load(const std::string& config_path) const;
    };
}

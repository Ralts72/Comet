#pragma once

#include "asset/script.h"

namespace Comet {
    // 资产准备复用 Lua 校验；此契约不引入实例、实体或运行服务。
    Result<void, Error> prepare_script_definition(Script& script,
        ScriptSources* collecting = nullptr,
        std::vector<std::filesystem::path>* dependencies = nullptr);
}

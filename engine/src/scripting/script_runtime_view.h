#pragma once

#include "common/export.h"

#include <memory>

namespace Comet {
    class Entity;
    class Script;

    // 查询实际运行的定义，不以资产缓存中的候选版本代替活动实例。
    class COMET_API ScriptRuntimeView {
    public:
        virtual ~ScriptRuntimeView() = default;
        [[nodiscard]] virtual std::shared_ptr<const Script> running_script(Entity entity) const = 0;
    };
}

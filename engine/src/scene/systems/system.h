#pragma once

#include "common/error.h"
#include "common/export.h"
#include "common/result.h"
#include "input/input_state.h"

namespace Comet {
    class Scene;

    class COMET_API System {
    public:
        struct Context {
            double delta_time;
            double total_time;
            uint64_t index;
            const InputState& input;
        };

        virtual ~System() = default;
        virtual Result<void, Error> on_start(Scene&) { return Result<void, Error>::success(); }
        virtual Result<void, Error> fixed_update(Scene&, const Context&) {
            return Result<void, Error>::success();
        }
        virtual Result<void, Error> update(Scene&, const Context&) {
            return Result<void, Error>::success();
        }
        // 每次先于 on_start 通知初始状态，之后仅通知状态切换；单步不恢复异步子系统。
        virtual void on_pause_changed(bool) noexcept {}
        // 包含部分启动失败的清理；不得重入 Runtime 或替换 Scene。
        virtual void on_stop(Scene&) noexcept {}
    };
}

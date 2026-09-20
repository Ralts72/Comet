#pragma once

#include "common/error.h"
#include "common/export.h"
#include "common/result.h"
#include "core/input.h"

namespace Comet {
    class Scene;

    class COMET_API System {
    public:
        struct Context {
            double delta_time;
            double total_time;
            uint64_t index;
            const Input::Frame& input;
        };

        virtual ~System() = default;
        virtual Result<void, Error> on_start(Scene&) { return Result<void, Error>::success(); }
        virtual Result<void, Error> fixed_update(Scene&, const Context&) {
            return Result<void, Error>::success();
        }
        virtual Result<void, Error> update(Scene&, const Context&) {
            return Result<void, Error>::success();
        }
        // 包含部分启动失败的清理；不得重入 Runtime 或替换 Scene。
        virtual void on_stop(Scene&) noexcept {}
    };
}

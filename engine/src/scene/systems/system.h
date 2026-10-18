#pragma once

#include "common/error.h"
#include "common/export.h"
#include "common/result.h"
#include "input/input_state.h"
#include "scene/runtime_services.h"

namespace Comet {
    class Scene;
    class RuntimeSession;

    class COMET_API System {
    public:
        enum class Order {
            Input,
            Gameplay,
            Animation,
            Physics,
            Constraints,
            Camera,
            Environment,
            Particles,
            Audio
        };
        struct Context {
            double delta_time;
            double total_time;
            uint64_t index;
            const InputState& input;
            RuntimeSession& session;
            RuntimeServices services;
        };

        virtual ~System() = default;
        virtual Result<void, Error> on_start(Scene&, RuntimeSession&, const RuntimeServices&) {
            return Result<void, Error>::success();
        }
        // 全部 on_start 及启动结构请求提交后执行一次；此时物理碰撞已同步。
        virtual Result<void, Error> on_runtime_ready(
            Scene&, RuntimeSession&, const RuntimeServices&) {
            return Result<void, Error>::success();
        }
        // 在 Main 更新场景，与只读快照绘制并发；不得修改 Renderer 或 UI 后端。
        virtual Result<void, Error> fixed_update(Scene&, const Context&) {
            return Result<void, Error>::success();
        }
        // 所有固定更新及结构提交完成后执行；借用本步接触，同一 Context 不再次消费输入。
        virtual Result<void, Error> post_fixed_update(Scene&, const Context&) {
            return Result<void, Error>::success();
        }
        virtual Result<void, Error> update(Scene&, const Context&) {
            return Result<void, Error>::success();
        }
        // 所有普通更新及结构提交完成后执行，消费本帧产生的事件。
        virtual Result<void, Error> post_update(Scene&, const Context&) {
            return Result<void, Error>::success();
        }
        // 只表达当前场景与授权动作的意图；窗口捕获仍由宿主决定。
        [[nodiscard]] virtual bool wants_cursor_capture(Scene&, const InputState&) const {
            return false;
        }
        // 每次先于 on_start 通知初始状态，之后仅通知状态切换；单步不恢复异步子系统。
        virtual void on_pause_changed(bool) noexcept {}
        // 包含部分启动失败的清理；不得重入 Runtime 或替换 Scene。
        virtual void on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept {}
    };
}

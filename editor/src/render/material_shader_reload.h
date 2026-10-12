#pragma once

#include "render/shader_reload.h"
#include "common/error.h"
#include "common/result.h"

namespace Comet {
    class Renderer;
}

namespace CometEditor {
    // Main 接纳编译结果，Renderer 调度 GPU 发布；ShaderReload 负责监控和 CPU 编译。
    class MaterialShaderReload final {
    public:
        MaterialShaderReload(Comet::TaskScheduler& scheduler,
            const std::filesystem::path& shader_root,
            std::chrono::milliseconds quiet_period = DEFAULT_FILE_CHANGE_QUIET_PERIOD);

        // 成功值表示已发布新的 pipeline，调用方据此刷新材质布局 UI。
        [[nodiscard]] Comet::Result<bool, Comet::Error> update(Comet::Renderer& renderer);

    private:
        ShaderReload m_reload;
        std::uint64_t m_reported_retry = 0;
    };
}

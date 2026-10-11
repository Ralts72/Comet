#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include "render/render_frame.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace Comet {
    class Renderer;

    // 交付后 Renderer 和 UI 后端归 Worker 使用；Owner 可继续处理窗口事件与 CPU 前端。
    // complete 取回结果后，Owner 才能再次访问可变 GPU 状态。
    class COMET_API RenderThread final {
    public:
        explicit RenderThread(Renderer& renderer);
        ~RenderThread();
        RenderThread(const RenderThread&) = delete;
        RenderThread& operator=(const RenderThread&) = delete;

        // 包含录制中的帧在内，最多接收一帧；空 world 表示只绘制背景和 UI。
        [[nodiscard]] Result<void, GraphicsError> submit(RenderFrame frame);
        [[nodiscard]] Result<void, GraphicsError> complete();

    private:
        void run();

        Renderer& m_renderer;
        std::mutex m_mutex;
        std::condition_variable m_available;
        std::condition_variable m_completed;
        std::optional<RenderFrame> m_frame;
        std::optional<Result<void, GraphicsError>> m_result;
        bool m_pending = false;
        bool m_stopping = false;
        std::thread m_worker;
    };
}

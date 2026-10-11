#include "render_thread.h"
#include "render/renderer.h"

#include <exception>
#include <string_view>
#include <system_error>
#include <utility>

namespace Comet {
    RenderThread::RenderThread(Renderer& renderer)
        : m_renderer(renderer), m_worker([this] { run(); }) {}

    RenderThread::~RenderThread() {
        {
            const std::scoped_lock lock(m_mutex);
            m_stopping = true;
        }
        m_available.notify_one();
        m_worker.join();
    }

    Result<void, GraphicsError> RenderThread::submit(std::shared_ptr<const RenderWorld> world) {
        {
            const std::scoped_lock lock(m_mutex);
            if(m_stopping)
                return Result<void, GraphicsError>::failure({"Render thread is stopped"});
            if(m_pending)
                return Result<void, GraphicsError>::failure(
                    {"Previous render frame is not completed"});
            m_frame.emplace(std::move(world));
            m_pending = true;
        }
        m_available.notify_one();
        return Result<void, GraphicsError>::success();
    }

    Result<void, GraphicsError> RenderThread::complete() {
        std::unique_lock lock(m_mutex);
        if(!m_pending)
            return Result<void, GraphicsError>::success();
        m_completed.wait(lock, [this] { return m_result.has_value(); });
        auto result = std::move(*m_result);
        m_result.reset();
        m_pending = false;
        return result;
    }

    void RenderThread::run() {
        for(;;) {
            auto frame = [&]() {
                std::unique_lock lock(m_mutex);
                m_available.wait(lock, [this] { return m_frame || m_stopping; });
                return std::exchange(m_frame, std::nullopt);
            }();
            if(!frame)
                return;
            auto result = [&] {
                try {
                    return m_renderer.record_world(std::move(*frame));
                } catch(const std::system_error& error) {
                    // 跨动态库使用标准基类；Vulkan-Hpp 的具体异常类型可能不共享 RTTI。
                    GraphicsError failure{"Render thread failed: " + std::string(error.what())};
                    if(std::string_view(error.code().category().name())
                        == vk::make_error_code(vk::Result::eErrorUnknown).category().name())
                        failure.result = static_cast<vk::Result>(error.code().value());
                    return Result<void, GraphicsError>::failure(std::move(failure));
                } catch(const std::exception& error) {
                    return Result<void, GraphicsError>::failure(
                        {"Render thread failed: " + std::string(error.what())});
                } catch(...) {
                    return Result<void, GraphicsError>::failure({"Render thread failed"});
                }
            }();
            // 先释放包的引用，再通知 Owner；通知之后不能再访问本帧资源。
            frame.reset();
            {
                const std::scoped_lock lock(m_mutex);
                if(!result)
                    m_stopping = true;
                m_result.emplace(std::move(result));
            }
            m_completed.notify_one();
        }
    }
}

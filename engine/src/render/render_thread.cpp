#include "render_thread.h"
#include "render/renderer.h"
#include "render/resource/render_resources.h"
#include "asset/data/mesh_data.h"
#include "asset/data/texture_data.h"

#include <utility>

namespace Comet {
    RenderThread::RenderThread(Renderer& renderer)
        : m_renderer(renderer), m_worker([this] { run(); }) {}

    RenderThread::~RenderThread() {
        stop();
    }

    void RenderThread::stop() {
        {
            const std::scoped_lock lock(m_mutex);
            m_stopping = true;
        }
        m_available.notify_one();
        if(m_worker.joinable())
            m_worker.join();
    }

    Result<void, GraphicsError> RenderThread::submit(RenderFrame frame) {
        return enqueue(std::move(frame));
    }

    RenderThread::FrameResult RenderThread::enqueue(Request request) {
        {
            const std::scoped_lock lock(m_mutex);
            if(m_stopping)
                return FrameResult::failure({"Render thread is stopped"});
            if(m_pending != Pending::None)
                return FrameResult::failure(
                    {"Previous render work is not completed", vk::Result::eNotReady});
            m_pending =
                std::holds_alternative<RenderFrame>(request) ? Pending::Frame : Pending::Operation;
            m_request.emplace(std::move(request));
        }
        m_available.notify_one();
        return FrameResult::success();
    }

    Result<void, GraphicsError> RenderThread::complete() {
        std::unique_lock lock(m_mutex);
        if(m_pending == Pending::None)
            return FrameResult::success();
        if(m_pending != Pending::Frame)
            return FrameResult::failure({"A synchronous GPU operation is still active"});
        return take_result(lock);
    }

    RenderThread::FrameResult RenderThread::take_result(std::unique_lock<std::mutex>& lock) {
        m_completed.wait(lock, [this] { return m_result.has_value(); });
        auto result = std::move(*m_result);
        m_result.reset();
        m_pending = Pending::None;
        return result;
    }

    GpuResourceResult<std::shared_ptr<Mesh>> RenderThread::try_create_mesh(MeshData data) {
        return invoke([this, data = std::move(data)]() mutable {
            return m_renderer.get_render_resources().try_create_mesh(std::move(data));
        });
    }

    GpuResourceResult<std::shared_ptr<Texture>> RenderThread::try_create_texture(TextureData data) {
        return invoke([this, data = std::move(data)]() mutable {
            return m_renderer.get_render_resources().try_create_texture(std::move(data));
        });
    }

    void RenderThread::run() {
        for(;;) {
            auto request = [&]() {
                std::unique_lock lock(m_mutex);
                m_available.wait(lock, [this] { return m_request || m_stopping; });
                return std::exchange(m_request, std::nullopt);
            }();
            if(!request)
                return;
            const auto is_frame = std::holds_alternative<RenderFrame>(*request);
            auto result = FrameResult::success();
            if(is_frame) {
                result = guarded_work([&] {
                    return m_renderer.record_frame(std::move(std::get<RenderFrame>(*request)));
                });
            } else {
                std::get<Operation> (*request)();
            }
            // 先释放包的引用，再通知 Owner；通知之后不能再访问本帧资源。
            request.reset();
            {
                const std::scoped_lock lock(m_mutex);
                if(is_frame && !result)
                    m_stopping = true;
                m_result.emplace(std::move(result));
            }
            m_completed.notify_one();
        }
    }
}

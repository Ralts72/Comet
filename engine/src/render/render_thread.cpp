#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "render_thread.h"
#include "render/renderer.h"
#include "render/resource/render_resources.h"
#include "asset/data/mesh_data.h"
#include "asset/data/texture_data.h"

#include <utility>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif

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
            if(std::holds_alternative<RenderFrame>(request))
                m_pending = Pending::Frame;
            else if(std::holds_alternative<Preparation>(request))
                m_pending = Pending::Preparation;
            else
                m_pending = Pending::Operation;
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

    Result<bool, GraphicsError> RenderThread::prepare_frame(
        const Math::Vec2u framebuffer_size, AssetRegistry::Removals removals) {
        if(auto queued = enqueue(Preparation{framebuffer_size, std::move(removals)}); !queued)
            return Result<bool, GraphicsError>::failure(queued.error());
        std::unique_lock lock(m_mutex);
        if(auto completed = take_result(lock); !completed)
            return Result<bool, GraphicsError>::failure(completed.error());
        return Result<bool, GraphicsError>::success(m_prepared);
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
#ifdef __APPLE__
        // 实时绘制使用交互 QoS，与窗口帧循环匹配。
        static_cast<void>(pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0));
#endif
        for(;;) {
            auto request = [&]() {
                std::unique_lock lock(m_mutex);
                m_available.wait(lock, [this] { return m_request || m_stopping; });
                return std::exchange(m_request, std::nullopt);
            }();
            if(!request) {
                if(m_renderer.m_render_thread == this)
                    m_renderer.prepare_shutdown();
                return;
            }
            const auto is_frame = std::holds_alternative<RenderFrame>(*request);
            auto result = FrameResult::success();
            bool prepared = false;
            if(is_frame) {
                result = guarded_work([&] {
                    return m_renderer.record_frame(std::move(std::get<RenderFrame>(*request)));
                });
            } else if(auto* preparation = std::get_if<Preparation>(&*request)) {
                auto preparation_result = guarded_work([&] {
                    return m_renderer.prepare_gpu_frame(
                        preparation->framebuffer_size, std::move(preparation->removals));
                });
                if(preparation_result)
                    prepared = preparation_result.value() == Renderer::FramePreparation::Ready;
                else
                    result = FrameResult::failure(preparation_result.error());
            } else {
                std::get<Operation> (*request)();
            }
            // 先释放包的引用，再通知 Owner；通知之后不能再访问本帧资源。
            request.reset();
            if(is_frame && !result && m_renderer.m_render_thread == this)
                m_renderer.prepare_shutdown();
            {
                const std::scoped_lock lock(m_mutex);
                if(is_frame && !result)
                    m_stopping = true;
                m_result.emplace(std::move(result));
                m_prepared = prepared;
            }
            m_completed.notify_one();
        }
    }
    uint64_t RenderThread::gpu_bytes(const std::shared_ptr<Mesh>& value) const {
        return value ? value->gpu_resident_bytes() : 0;
    }
    uint64_t RenderThread::gpu_bytes(const std::shared_ptr<Texture>& value) const {
        return value ? value->gpu_resident_bytes() : 0;
    }

}

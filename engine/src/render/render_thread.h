#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include "render/render_frame.h"
#include "render/resource/resource_factory.h"
#include "asset/registry.h"

#include <condition_variable>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace Comet {
    class Renderer;

    // Owner 单线程提交；交付后 Renderer 和 UI 后端归 Worker 使用。
    // Owner 可继续处理窗口事件与 CPU 前端。
    // complete 取回结果后，Owner 才能再次访问可变 GPU 状态。
    class COMET_API RenderThread final: public RenderResourceFactory {
    public:
        explicit RenderThread(Renderer& renderer);
        ~RenderThread() override;
        RenderThread(const RenderThread&) = delete;
        RenderThread& operator=(const RenderThread&) = delete;

        // 包含录制中的帧在内，交接槽最多接收一个请求；空 world 只绘制背景和 UI。
        [[nodiscard]] Result<void, GraphicsError> submit(RenderFrame frame);
        [[nodiscard]] Result<void, GraphicsError> complete();
        void stop();

        [[nodiscard]] uint64_t gpu_bytes(const std::shared_ptr<Mesh>&) const override;
        [[nodiscard]] uint64_t gpu_bytes(const std::shared_ptr<Texture>&) const override;

        // Owner 在帧交接边界同步创建资产；独立 CPU 产品在 Worker 消费后释放。
        [[nodiscard]] GpuResourceResult<std::shared_ptr<Mesh>> try_create_mesh(
            MeshData data) override;
        [[nodiscard]] GpuResourceResult<std::shared_ptr<Texture>> try_create_texture(
            TextureData data) override;

    private:
        friend class Renderer;
        using FrameResult = Result<void, GraphicsError>;
        using Operation = std::packaged_task<void()>;
        struct Preparation {
            Math::Vec2u framebuffer_size;
            AssetRegistry::Removals removals;
        };
        using Request = std::variant<RenderFrame, Preparation, Operation>;
        enum class Pending { None, Frame, Preparation, Operation };

        [[nodiscard]] Result<bool, GraphicsError> prepare_frame(
            Math::Vec2u framebuffer_size, AssetRegistry::Removals removals);
        [[nodiscard]] FrameResult enqueue(Request request);
        [[nodiscard]] FrameResult take_result(std::unique_lock<std::mutex>& lock);
        [[nodiscard]] bool is_current() const {
            return std::this_thread::get_id() == m_worker.get_id();
        }
        // 仅供资源工厂和 Renderer 的同步 GPU 操作复用，不开放业务任务入口。
        template<typename Work> auto invoke(Work work) {
            using Outcome = decltype(work());
            if(is_current())
                return guarded_work(std::move(work));
            std::packaged_task<Outcome()> operation(std::move(work));
            auto outcome = operation.get_future();
            Operation request([operation = std::move(operation)]() mutable { operation(); });
            if(auto queued = enqueue(std::move(request)); !queued)
                return failed_work<Outcome>(queued.error());
            std::unique_lock lock(m_mutex);
            static_cast<void>(take_result(lock));
            lock.unlock();
            return guarded_work([&] { return outcome.get(); });
        }

        template<typename Outcome> static Outcome failed_work(GraphicsError error) {
            if constexpr(requires { Outcome::failure(error); })
                return Outcome::failure(std::move(error));
            else
                return Outcome::failure(error.result.value_or(vk::Result::eErrorUnknown));
        }

        static auto guarded_work(auto work) {
            using Outcome = decltype(work());
            try {
                return work();
            } catch(const std::system_error& error) {
                // 跨动态库使用标准基类；Vulkan-Hpp 的具体异常类型可能不共享 RTTI。
                GraphicsError failure{"Render thread failed: " + std::string(error.what())};
                if(std::string_view(error.code().category().name())
                    == vk::make_error_code(vk::Result::eErrorUnknown).category().name())
                    failure.result = static_cast<vk::Result>(error.code().value());
                return failed_work<Outcome>(std::move(failure));
            } catch(const std::exception& error) {
                return failed_work<Outcome>({"Render thread failed: " + std::string(error.what())});
            } catch(...) {
                return failed_work<Outcome>({"Render thread failed"});
            }
        }
        void run();

        Renderer& m_renderer;
        std::mutex m_mutex;
        std::condition_variable m_available;
        std::condition_variable m_completed;
        std::optional<Request> m_request;
        std::optional<FrameResult> m_result;
        Pending m_pending = Pending::None;
        bool m_prepared = false;
        bool m_stopping = false;
        std::thread m_worker;
    };
}

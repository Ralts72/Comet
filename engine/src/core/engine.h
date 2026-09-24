#pragma once
#include "common/export.h"
#include "common/error.h"
#include "common/result.h"
#include "input/input.h"
#include "diagnostics/frame_diagnostics.h"
#include "scene/scene_runtime.h"
#include "timer.h"

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace Comet {
    class AssetRegistry;
    class Scene;
    class TaskScheduler;
    class Window;
    class Renderer;
    class RenderResources;
    class Config;

    class COMET_API Engine {
    public:
        struct FrameContext {
            const UpdateContext update;
            const Input::Frame& physical_input;
            std::optional<Input::Frame> runtime_input;
        };
        [[nodiscard]] const FrameDiagnostics& frame_diagnostics() const {
            return m_frame_diagnostics;
        }
        static Result<std::unique_ptr<Engine>, Error> create(const Config& config);

        ~Engine();

        // 终止生命周期时调用；先完成后台工作和 GPU 使用，再释放应用持有的资源。
        void prepare_shutdown();

        // 同步运行；帧上下文仅在当前 tick 存活，不跨帧保存授权输入。
        [[nodiscard]] Result<void, Error> run(
            const std::function<Result<void, Error>(FrameContext&)>& update = {},
            const std::function<Result<void, Error>(FrameContext&)>& frame_ready = {},
            const std::function<Result<void, Error>(const Error&)>& runtime_failed = {});

        void set_scene(std::unique_ptr<Scene> scene);

        [[nodiscard]] std::unique_ptr<Scene> replace_scene(std::unique_ptr<Scene> scene) noexcept;

        [[nodiscard]] Scene* get_scene() { return m_scene.get(); }
        [[nodiscard]] const Scene* get_scene() const { return m_scene.get(); }

        [[nodiscard]] Result<void, Error> add_system(std::unique_ptr<System> system);
        [[nodiscard]] Result<void, Error> add_default_scene_systems();
        [[nodiscard]] Result<void, Error> set_runtime_settings(SceneRuntime::Settings settings);
        [[nodiscard]] Result<void, Error> set_input_actions(InputActions actions);
        [[nodiscard]] Result<void, Error> start_scene_runtime();
        [[nodiscard]] Result<void, Error> stop_scene_runtime();
        [[nodiscard]] Result<void, Error> set_runtime_state(SceneRuntime::State state);
        [[nodiscard]] Result<void, Error> request_runtime_step();
        [[nodiscard]] const SceneRuntime& get_scene_runtime() const { return m_scene_runtime; }
        [[nodiscard]] AssetRegistry& get_asset_registry() { return *m_asset_registry; }
        [[nodiscard]] const AssetRegistry& get_asset_registry() const { return *m_asset_registry; }

        [[nodiscard]] RenderResources& get_render_resources();
        [[nodiscard]] const RenderResources& get_render_resources() const;

        [[nodiscard]] TaskScheduler& get_task_scheduler() { return *m_task_scheduler; }
        [[nodiscard]] const TaskScheduler& get_task_scheduler() const { return *m_task_scheduler; }

        [[nodiscard]] Window& get_window() { return *m_window; }
        [[nodiscard]] const Window& get_window() const { return *m_window; }
        [[nodiscard]] const Input::Frame& get_input_frame() const;
        [[nodiscard]] Renderer& get_renderer() { return *m_renderer; }
        [[nodiscard]] const Renderer& get_renderer() const { return *m_renderer; }

    private:
        Engine(std::unique_ptr<Window> window, std::unique_ptr<AssetRegistry> assets,
            std::unique_ptr<Renderer> renderer, std::unique_ptr<TaskScheduler> scheduler);
        [[nodiscard]] Result<void, Error> tick(
            const std::function<Result<void, Error>(FrameContext&)>& update,
            const std::function<Result<void, Error>(FrameContext&)>& frame_ready,
            const std::function<Result<void, Error>(const Error&)>& runtime_failed);
        std::unique_ptr<Timer> m_timer;
        std::unique_ptr<TaskScheduler> m_task_scheduler;
        std::unique_ptr<Window> m_window;
        std::unique_ptr<AssetRegistry> m_asset_registry;
        std::unique_ptr<Scene> m_scene;
        std::unique_ptr<Renderer> m_renderer;
        SceneRuntime m_scene_runtime;
        FrameDiagnostics m_frame_diagnostics;
        bool m_running = false;
        bool m_shutdown_prepared = false;
    };
}

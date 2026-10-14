#pragma once

#include "scene/runtime_session.h"
#include "scene/scene_serializer.h"
#include <functional>
#include <memory>

namespace Comet {
    class Scene;

    // App 与 Play 共用关卡初始内容；窗口、资产准备和活动 Scene 的所有权由宿主持有。
    class COMET_API GameSession final {
    public:
        using LoadScene = std::function<Result<std::unique_ptr<Scene>>(std::string_view)>;
        struct Transition {
            std::unique_ptr<Scene> scene;
            ParameterMap values;
            std::string path;
            std::string checkpoint;

        private:
            friend class GameSession;
            std::unique_ptr<Scene> source;
        };

        struct Persistence {
            std::function<Result<void>(std::string_view, const RuntimeSession::CheckpointRequest&)>
                save;
            std::function<Result<RuntimeSession::SceneRequest>()> load;
        };
        explicit GameSession(const SceneSerializer& serializer);
        void set_persistence(Persistence persistence) { m_persistence = std::move(persistence); }
        [[nodiscard]] Result<void> save_checkpoint(
            const RuntimeSession::CheckpointRequest& request) const;
        ~GameSession();
        // Editor 借用保留的 Edit 场景，App 接管加载的初始内容；借用源须活到 end。
        void begin(const Scene& source, std::string path = {});
        void begin(std::unique_ptr<Scene> source, std::string path);
        [[nodiscard]] Result<Transition> prepare(
            const RuntimeSession::SceneRequest& request, const LoadScene& load = {}) const;
        // 在候选安装且运行时启动成功后接纳；失败候选不改变重开基线。
        void accept(Transition transition);
        void end();
        [[nodiscard]] const std::string& scene_path() const { return m_path; }

    private:
        const SceneSerializer& m_serializer;
        const Scene* m_source = nullptr;
        std::unique_ptr<Scene> m_owned_source;
        ParameterMap m_entry_values;
        std::string m_path;
        std::string m_entry_checkpoint;
        Persistence m_persistence;
    };
}

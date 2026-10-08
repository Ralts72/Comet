#pragma once

#include "asset/script.h"
#include "scene/entity.h"

namespace Comet {
    class InputState;
    class MaterialParameterValidator;
    class Scene;
    class RuntimeSession;
    class AudioCommands;
    class PhysicsCommands;

    // 每个行为实例独占 VM；借用权限仅在 invoke 期间有效。
    class COMET_API ScriptInstance final {
    public:
        enum class Phase {
            Start,
            FixedUpdate,
            Update,
            Stop,
            CollisionEnter,
            CollisionExit,
            TriggerEnter,
            TriggerExit,
            Event
        };
        struct Invocation {
            double delta_time = 0;
            Scene* scene = nullptr;
            RuntimeSession* session = nullptr;
            AudioCommands* audio = nullptr;
            PhysicsCommands* physics = nullptr;
            const InputState* input = nullptr;
            Entity contact_other;
            const MaterialParameterValidator* materials = nullptr;
            std::string_view event_handler;
            const ParameterValue* event_value = nullptr;
            // Stop 只记录关闭请求；宿主在回调结束后决定是否交给运行中的场景。
            std::vector<std::string>* disabled_input_contexts = nullptr;
        };
        [[nodiscard]] static Result<std::unique_ptr<ScriptInstance>, Error> create(
            const Script& script);
        ~ScriptInstance();
        ScriptInstance(const ScriptInstance&) = delete;
        ScriptInstance& operator=(const ScriptInstance&) = delete;
        Result<void, Error> invoke(
            Phase phase, Entity entity, const ParameterMap& parameters, Invocation invocation);
        Result<void, Error> invoke(Phase phase, Entity entity, const ParameterMap& parameters);

    private:
        friend Result<void, Error> prepare_script_definition(
            Script&, ScriptSources*, std::vector<std::filesystem::path>*);
        struct Impl;
        explicit ScriptInstance(std::unique_ptr<Impl> impl);
        static Result<std::unique_ptr<ScriptInstance>, Error> create(const Script& script,
            ScriptSources* collecting, std::vector<std::filesystem::path>* dependencies);
        std::unique_ptr<Impl> m_impl;
    };
}

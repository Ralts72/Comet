#pragma once

#include "scene/entity.h"
#include "scene/property.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct lua_State;

namespace Comet {
    class InputState;
    class MaterialParameterValidator;
    class Scene;
    class RuntimeSession;
    namespace LuaBindings {
        struct Context {
            Entity entity;
            Scene* scene = nullptr;
            RuntimeSession* session = nullptr;
            const InputState* input = nullptr;
            std::uint64_t scene_generation = 0;
            // 由 lua_pcall 外的宿主持有，Lua 内存错误不能跳过 C++ 对象析构。
            std::optional<ParameterValue> return_value;
            const MaterialParameterValidator* materials = nullptr;
            bool can_request_restart = false;
            std::vector<std::string>* disabled_input_contexts = nullptr;
            bool can_log = false;
            unsigned log_messages = 0;
            bool log_overflow_reported = false;
        };

        void install(lua_State* state, Context& context);
        int push_entity_reference(lua_State* state, Entity entity, std::uint64_t scene_generation);
    }
}

#pragma once

#include "scene/entity.h"

#include <cstdint>

struct lua_State;

namespace Comet {
    class InputState;
    class Scene;
    namespace LuaBindings {
        struct Context {
            Entity entity;
            Scene* scene = nullptr;
            const InputState* input = nullptr;
            std::uint64_t scene_generation = 0;
        };

        void install(lua_State* state, Context& context);
        int push_entity_reference(lua_State* state, Entity entity,
            std::uint64_t scene_generation);
    }
}

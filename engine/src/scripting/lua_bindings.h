#pragma once

#include "scene/entity.h"

struct lua_State;

namespace Comet {
    class InputState;
    namespace LuaBindings {
        struct Context {
            Entity entity;
            const InputState* input = nullptr;
        };

        void install(lua_State* state, Context& context);
    }
}

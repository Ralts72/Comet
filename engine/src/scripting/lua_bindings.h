#pragma once

#include "scene/entity.h"
#include "core/input.h"

struct lua_State;

namespace Comet {
    namespace LuaBindings {
        struct Context {
            Entity entity;
            const Input::Frame* input = nullptr;
        };

        void install(lua_State* state, Context& context);
    }
}

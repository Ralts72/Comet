#include "scripting/lua_bindings.h"
#include "scene/scene.h"
#include "core/input.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <cmath>
#include <limits>

namespace Comet::LuaBindings {
    namespace {
        Context& current(lua_State* state) {
            return *static_cast<Context*>(lua_touserdata(state, lua_upvalueindex(1)));
        }
        TransformComponent& transform(lua_State* state) {
            auto& entity = current(state).entity;
            if(!entity || !entity.has_component<TransformComponent>())
                luaL_error(state, "Entity is unavailable in this script phase");
            return entity.get_component<TransformComponent>();
        }
        float number(lua_State* state, int index) {
            const auto value = luaL_checknumber(state, index);
            if(!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                luaL_error(state, "Expected a finite float");
            return static_cast<float>(value);
        }
        int rotate(lua_State* state) {
            const Math::Vec3 value{number(state, 1), number(state, 2), number(state, 3)};
            auto& target = transform(state);
            if(!Math::is_finite(target.rotation + value))
                return luaL_error(state, "Rotation overflow");
            target.rotate(value);
            return 0;
        }
        int translate(lua_State* state) {
            const Math::Vec3 value{number(state, 1), number(state, 2), number(state, 3)};
            auto& target = transform(state);
            if(!Math::is_finite(target.translation + value))
                return luaL_error(state, "Translation overflow");
            target.translation += value;
            return 0;
        }
        int position(lua_State* state) {
            const auto value = transform(state).translation;
            lua_pushnumber(state, value.x);
            lua_pushnumber(state, value.y);
            lua_pushnumber(state, value.z);
            return 3;
        }
        int key_down(lua_State* state) {
            const auto* input = current(state).input;
            const char* name = luaL_checkstring(state, 1);
            Input::Key key = Input::Key::Unknown;
            if(name[0] >= 'A' && name[0] <= 'Z' && name[1] == '\0')
                key = static_cast<Input::Key>(static_cast<int>(Input::Key::A) + name[0] - 'A');
            else
                return luaL_error(state, "key_down currently accepts A-Z");
            lua_pushboolean(state, input && input->focused && input->key(key).down);
            return 1;
        }
    }

    void install(lua_State* state, Context& context) {
        lua_newtable(state);
        lua_pushlightuserdata(state, &context);
        const luaL_Reg api[]{{"rotate", rotate}, {"translate", translate}, {"position", position},
            {"key_down", key_down}, {nullptr, nullptr}};
        luaL_setfuncs(state, api, 1);
        lua_setglobal(state, "comet");
    }
}

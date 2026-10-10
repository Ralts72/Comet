#include "ui/lua_controller.h"
#include "common/file_io.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cmath>
#include <cstdlib>
#include <utility>

namespace Comet::Ui::Detail {
    namespace {
        constexpr std::size_t max_model_fields = 128;
        constexpr std::size_t max_text_bytes = 256 * 1024;
        constexpr std::size_t max_script_bytes = 1024 * 1024;
        bool read_scalar(lua_State* state, int index, Rml::Variant& result) {
            switch(lua_type(state, index)) {
                case LUA_TBOOLEAN:
                    result = bool(lua_toboolean(state, index));
                    return true;
                case LUA_TNUMBER:
                    if(!std::isfinite(lua_tonumber(state, index)))
                        return false;
                    result = double(lua_tonumber(state, index));
                    return true;
                case LUA_TSTRING: {
                    std::size_t size = 0;
                    const auto* text = lua_tolstring(state, index, &size);
                    if(size > max_text_bytes)
                        return false;
                    result = std::string(text, size);
                    return true;
                }
                default:
                    return false;
            }
        }
        void push_scalar(lua_State* state, const Rml::Variant& value) {
            switch(value.GetType()) {
                case Rml::Variant::BOOL:
                    lua_pushboolean(state, value.Get<bool>());
                    break;
                case Rml::Variant::DOUBLE:
                    lua_pushnumber(state, value.Get<double>());
                    break;
                case Rml::Variant::FLOAT:
                    lua_pushnumber(state, value.Get<float>());
                    break;
                case Rml::Variant::INT:
                    lua_pushinteger(state, value.Get<int>());
                    break;
                case Rml::Variant::STRING: {
                    const auto& text = value.GetReference<Rml::String>();
                    lua_pushlstring(state, text.data(), text.size());
                    break;
                }
                default:
                    lua_pushnil(state);
                    break;
            }
        }
    }
    class LuaController::Impl final {
    public:
        Impl(Functions functions, Callback callback)
            : m_functions(std::move(functions)), m_callback(std::move(callback)) {}
        ~Impl() {
            if(m_state)
                lua_close(m_state);
        }

        Result<void> load(const std::filesystem::path& path) {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if(error || size > max_script_bytes)
                return Result<void>::failure(
                    "Cannot read UI controller (1 MiB limit): " + path.string());
            auto source = read_text_file(path);
            if(!source)
                return Result<void>::failure(source.error());
            m_source = std::move(source).value();
            m_name = "@" + path.generic_string();
            m_state = lua_newstate(allocate, this);
            if(!m_state)
                return Result<void>::failure("Cannot create UI controller VM");
            *static_cast<Impl**>(lua_getextraspace(m_state)) = this;
            m_budget = 1000;
            lua_sethook(m_state, limit, LUA_MASKCOUNT, 1000);
            lua_pushcfunction(m_state, initialize);
            return finish(lua_pcall(m_state, 0, 0, 0));
        }

        Result<void> call(const char* method, const Rml::VariantList& arguments = {}) {
            m_method = method;
            m_arguments = &arguments;
            m_budget = 200;
            lua_pushcfunction(m_state, invoke);
            auto result = finish(lua_pcall(m_state, 0, 0, 0));
            m_arguments = nullptr;
            return result;
        }

        Result<void> copy_state(const Impl& previous) {
            // 热重载只迁移显式 state 中的标量；函数、页面句柄及闭包始终属于新 VM。
            for(auto& [name, value] : m_model) {
                if(const auto old = previous.m_model.find(name);
                    old != previous.m_model.end() && old->second.GetType() == value.GetType())
                    value = old->second;
            }
            auto* old = previous.m_state;
            lua_rawgeti(old, LUA_REGISTRYINDEX, previous.m_definition);
            lua_getfield(old, -1, "state");
            if(lua_istable(old, -1)) {
                lua_pushnil(old);
                while(lua_next(old, -2)) {
                    Rml::Variant value;
                    if(lua_type(old, -2) == LUA_TSTRING && read_scalar(old, -1, value))
                        m_saved_state[lua_tostring(old, -2)] = std::move(value);
                    if(m_saved_state.size() > max_model_fields) {
                        lua_pop(old, 4);
                        return Result<void>::failure("UI reload state exceeds 128 scalar fields");
                    }
                    lua_pop(old, 1);
                }
            }
            lua_pop(old, 2);
            return Result<void>::success();
        }

        Functions m_functions;
        Callback m_callback;
        FrameInfo m_frame;
        lua_State* m_state = nullptr;
        std::map<std::string, Rml::Variant> m_model;
        std::map<std::string, Rml::Variant> m_saved_state;

    private:
        static Impl& current(lua_State* state) {
            return **static_cast<Impl**>(lua_getextraspace(state));
        }
        static void* allocate(void* user, void* pointer, std::size_t old_size, std::size_t size) {
            constexpr std::size_t limit = 16 * 1024 * 1024;
            auto& vm = *static_cast<Impl*>(user);
            if(!pointer)
                old_size = 0;
            if(!size) {
                vm.m_memory -= old_size;
                std::free(pointer);
                return nullptr;
            }
            if(size > limit || vm.m_memory - old_size > limit - size)
                return nullptr;
            auto* result = std::realloc(pointer, size);
            if(result)
                vm.m_memory = vm.m_memory - old_size + size;
            return result;
        }
        static void limit(lua_State* state, lua_Debug*) {
            if(--current(state).m_budget <= 0)
                luaL_error(state, "UI controller instruction budget exceeded");
        }
        Result<void> finish(int status) {
            if(status == LUA_OK)
                return Result<void>::success();
            const auto* message = lua_tostring(m_state, -1);
            std::string error = m_name + ": " + (message ? message : "Lua allocation failed");
            lua_pop(m_state, 1);
            return Result<void>::failure(std::move(error));
        }
        static int dispatch(lua_State* state) {
            auto& vm = current(state);
            return vm.m_callback(
                state, static_cast<int>(lua_tointeger(state, lua_upvalueindex(1))));
        }
        static int initialize(lua_State* state) {
            auto& vm = current(state);
            static constexpr luaL_Reg libraries[] = {{"_G", luaopen_base}, {"math", luaopen_math},
                {"string", luaopen_string}, {"table", luaopen_table}};
            for(const auto& [name, open] : libraries) {
                luaL_requiref(state, name, open, 1);
                lua_pop(state, 1);
            }
            for(const auto* name : {"dofile", "loadfile", "load", "collectgarbage", "pcall",
                    "xpcall", "setmetatable", "getmetatable", "rawset", "print"}) {
                lua_pushnil(state);
                lua_setglobal(state, name);
            }
            if(luaL_loadbufferx(
                   state, vm.m_source.data(), vm.m_source.size(), vm.m_name.c_str(), "t")
                != LUA_OK)
                return lua_error(state);
            lua_call(state, 0, 1);
            if(!lua_istable(state, -1))
                return luaL_error(state, "UI controller must return a table");
            for(const auto* method :
                {"on_mount", "on_frame", "on_present", "on_event", "on_input_result",
                    "on_display_result", "on_reload_error", "on_deactivate", "on_destroy"}) {
                lua_getfield(state, -1, method);
                const bool valid = lua_isnil(state, -1) || lua_isfunction(state, -1);
                lua_pop(state, 1);
                if(!valid)
                    return luaL_error(state, "UI lifecycle '%s' must be a function", method);
            }
            lua_getfield(state, -1, "model");
            if(!lua_istable(state, -1))
                return luaL_error(state, "UI controller needs a scalar model table");
            lua_pushnil(state);
            while(lua_next(state, -2)) {
                bool valid = false;
                {
                    Rml::Variant value;
                    valid = lua_type(state, -2) == LUA_TSTRING && read_scalar(state, -1, value);
                    if(valid)
                        vm.m_model[lua_tostring(state, -2)] = std::move(value);
                }
                if(!valid || vm.m_model.size() > max_model_fields)
                    return luaL_error(state, "UI model supports at most 128 named scalar fields");
                lua_pop(state, 1);
            }
            lua_pop(state, 1);
            vm.m_definition = luaL_ref(state, LUA_REGISTRYINDEX);
            lua_newtable(state);
            for(const auto& [name, api] : vm.m_functions) {
                lua_pushinteger(state, static_cast<int>(api));
                lua_pushcclosure(state, dispatch, 1);
                lua_setfield(state, -2, name);
            }
            vm.m_api = luaL_ref(state, LUA_REGISTRYINDEX);
            return 0;
        }
        static int invoke(lua_State* state) {
            auto& vm = current(state);
            lua_rawgeti(state, LUA_REGISTRYINDEX, vm.m_definition);
            if(!vm.m_saved_state.empty()) {
                lua_newtable(state);
                for(const auto& [name, value] : vm.m_saved_state) {
                    push_scalar(state, value);
                    lua_setfield(state, -2, name.c_str());
                }
                lua_setfield(state, -2, "state");
                vm.m_saved_state.clear();
            }
            lua_getfield(state, -1, vm.m_method);
            if(lua_isnil(state, -1))
                return 0;
            lua_pushvalue(state, -2);
            lua_rawgeti(state, LUA_REGISTRYINDEX, vm.m_api);
            if(std::string_view(vm.m_method) == "on_frame") {
                lua_newtable(state);
                lua_pushnumber(state, vm.m_frame.fps);
                lua_setfield(state, -2, "fps");
                lua_pushboolean(state, vm.m_frame.game_available);
                lua_setfield(state, -2, "game_available");
                lua_pushboolean(state, vm.m_frame.focused);
                lua_setfield(state, -2, "focused");
                lua_call(state, 3, 0);
            } else {
                for(const auto& value : *vm.m_arguments)
                    push_scalar(state, value);
                lua_call(state, static_cast<int>(vm.m_arguments->size()) + 2, 0);
            }
            return 0;
        }
        std::string m_source;
        std::string m_name;
        const char* m_method = nullptr;
        const Rml::VariantList* m_arguments = nullptr;
        std::size_t m_memory = 0;
        int m_budget = 0;
        int m_definition = LUA_NOREF;
        int m_api = LUA_NOREF;
    };

    LuaController::LuaController(Functions functions, Callback callback)
        : m_impl(std::make_unique<Impl>(std::move(functions), std::move(callback))) {}
    LuaController::~LuaController() = default;
    Result<void> LuaController::load(const std::filesystem::path& path) {
        return m_impl->load(path);
    }
    Result<void> LuaController::call(const char* method, const Rml::VariantList& arguments) {
        return m_impl->call(method, arguments);
    }
    Result<void> LuaController::frame(FrameInfo info) {
        m_impl->m_frame = info;
        return m_impl->call("on_frame");
    }
    Result<void> LuaController::copy_state(const LuaController& previous) {
        return m_impl->copy_state(*previous.m_impl);
    }
    LuaController::Model& LuaController::model() {
        return m_impl->m_model;
    }
    bool LuaController::scalar(lua_State* state, int index, Rml::Variant& result) {
        return read_scalar(state, index, result);
    }
}

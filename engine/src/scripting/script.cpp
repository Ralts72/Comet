#include "scripting/script.h"
#include "common/file_io.h"
#include "scene/scene.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cmath>
#include <cstdlib>
#include <limits>

namespace Comet {
    struct Script::Instance::Impl {
        static constexpr size_t MEMORY_LIMIT = 8 * 1024 * 1024;
        lua_State* state = nullptr;
        size_t memory = 0;
        int budget = 0;
        std::string source;
        std::string name;
        int definition = LUA_NOREF;
        int self = LUA_NOREF;
        Entity entity;
        const ParameterMap* parameters = nullptr;
        const System::Context* context = nullptr;
        Phase phase = Phase::Start;

        ~Impl() {
            if(state)
                lua_close(state);
        }

        static void* allocate(void* user, void* pointer, size_t old_size, size_t size) {
            auto& vm = *static_cast<Impl*>(user);
            if(!pointer)
                old_size = 0;
            if(size == 0) {
                vm.memory -= old_size;
                std::free(pointer);
                return nullptr;
            }
            if(size > MEMORY_LIMIT || vm.memory - old_size > MEMORY_LIMIT - size)
                return nullptr;
            void* result = std::realloc(pointer, size);
            if(result)
                vm.memory = vm.memory - old_size + size;
            return result;
        }

        static Impl& current(lua_State* state) {
            return **static_cast<Impl**>(lua_getextraspace(state));
        }
        static void limit(lua_State* state, lua_Debug*) {
            if(--current(state).budget <= 0)
                luaL_error(state, "Script instruction budget exceeded");
        }
        static TransformComponent& transform(lua_State* state) {
            auto& vm = current(state);
            if(!vm.entity || !vm.entity.has_component<TransformComponent>())
                luaL_error(state, "Entity is unavailable in this script phase");
            return vm.entity.get_component<TransformComponent>();
        }
        static float number(lua_State* state, int index) {
            const auto value = luaL_checknumber(state, index);
            if(!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                luaL_error(state, "Expected a finite float");
            return static_cast<float>(value);
        }
        static int rotate(lua_State* state) {
            const Math::Vec3 value{number(state, 1), number(state, 2), number(state, 3)};
            auto& target = transform(state);
            if(!Math::is_finite(target.rotation + value))
                return luaL_error(state, "Rotation overflow");
            target.rotate(value);
            return 0;
        }
        static int translate(lua_State* state) {
            const Math::Vec3 value{number(state, 1), number(state, 2), number(state, 3)};
            auto& target = transform(state);
            if(!Math::is_finite(target.translation + value))
                return luaL_error(state, "Translation overflow");
            target.translation += value;
            return 0;
        }
        static int position(lua_State* state) {
            const auto value = transform(state).translation;
            lua_pushnumber(state, value.x);
            lua_pushnumber(state, value.y);
            lua_pushnumber(state, value.z);
            return 3;
        }
        static int key_down(lua_State* state) {
            const auto* context = current(state).context;
            const char* name = luaL_checkstring(state, 1);
            Input::Key key = Input::Key::Unknown;
            if(name[0] >= 'A' && name[0] <= 'Z' && name[1] == '\0')
                key = static_cast<Input::Key>(static_cast<int>(Input::Key::A) + name[0] - 'A');
            else
                return luaL_error(state, "key_down currently accepts A-Z");
            lua_pushboolean(
                state, context && context->input.focused && context->input.key(key).down);
            return 1;
        }

        static int initialize(lua_State* state) {
            auto& vm = current(state);
            luaL_requiref(state, "_G", luaopen_base, 1);
            lua_pop(state, 1);
            luaL_requiref(state, "math", luaopen_math, 1);
            lua_pop(state, 1);
            luaL_requiref(state, "string", luaopen_string, 1);
            lua_pop(state, 1);
            luaL_requiref(state, "table", luaopen_table, 1);
            lua_pop(state, 1);
            // 不开放文件、原生库、动态代码、元表与嵌套保护调用，避免绕过执行边界。
            for(const char* name : {"dofile", "loadfile", "load", "collectgarbage", "pcall",
                    "xpcall", "setmetatable", "getmetatable", "print"}) {
                lua_pushnil(state);
                lua_setglobal(state, name);
            }
            lua_newtable(state);
            const luaL_Reg api[]{{"rotate", rotate}, {"translate", translate},
                {"position", position}, {"key_down", key_down}, {nullptr, nullptr}};
            luaL_setfuncs(state, api, 0);
            lua_setglobal(state, "comet");
            if(luaL_loadbufferx(state, vm.source.data(), vm.source.size(), vm.name.c_str(), "t")
                != LUA_OK)
                return lua_error(state);
            lua_call(state, 0, 1);
            if(!lua_istable(state, -1))
                return luaL_error(state, "Script must return a table");
            for(const char* name : {"on_start", "fixed_update", "update", "on_stop"}) {
                lua_getfield(state, -1, name);
                const bool valid = lua_isnil(state, -1) || lua_isfunction(state, -1);
                lua_pop(state, 1);
                if(!valid)
                    return luaL_error(state, "Lifecycle entry '%s' must be a function", name);
            }
            vm.definition = luaL_ref(state, LUA_REGISTRYINDEX);
            lua_newtable(state);
            vm.self = luaL_ref(state, LUA_REGISTRYINDEX);
            return 0;
        }

        static void push_parameter(lua_State* state, const ParameterValue& parameter) {
            std::visit(
                [state](const auto& value) {
                    using T = std::remove_cvref_t<decltype(value)>;
                    if constexpr(std::is_same_v<T, bool>)
                        lua_pushboolean(state, value);
                    else if constexpr(std::is_same_v<T, float>)
                        lua_pushnumber(state, value);
                    else if constexpr(std::is_same_v<T, std::string>)
                        lua_pushlstring(state, value.data(), value.size());
                    else {
                        lua_createtable(state, 3, 0);
                        for(int i = 0; i < 3; ++i) {
                            lua_pushnumber(state, value[i]);
                            lua_rawseti(state, -2, i + 1);
                        }
                    }
                },
                parameter);
        }

        static int dispatch(lua_State* state) {
            auto& vm = current(state);
            const char* names[]{"on_start", "fixed_update", "update", "on_stop"};
            lua_rawgeti(state, LUA_REGISTRYINDEX, vm.definition);
            lua_getfield(state, -1, names[static_cast<int>(vm.phase)]);
            if(lua_isnil(state, -1))
                return 0;
            lua_rawgeti(state, LUA_REGISTRYINDEX, vm.self);
            lua_newtable(state);
            for(const auto& [name, value] : *vm.parameters) {
                push_parameter(state, value);
                lua_setfield(state, -2, name.c_str());
            }
            lua_setfield(state, -2, "parameters");
            double delta = 0;
            if(vm.context)
                delta = vm.context->delta_time;
            lua_pushnumber(state, delta);
            lua_call(state, 2, 0);
            return 0;
        }

        static int default_table(lua_State* state) {
            lua_rawgeti(state, LUA_REGISTRYINDEX, current(state).definition);
            lua_getfield(state, -1, "properties");
            return 1;
        }
        Result<void, Error> call(lua_CFunction function, int results = 0) {
            budget = 200;
            lua_sethook(state, limit, LUA_MASKCOUNT, 1000);
            lua_pushcfunction(state, function);
            const int status = lua_pcall(state, 0, results, 0);
            lua_sethook(state, nullptr, 0, 0);
            if(status != LUA_OK) {
                std::string message = "Lua raised a non-string error";
                if(lua_type(state, -1) == LUA_TSTRING)
                    message = lua_tostring(state, -1);
                lua_settop(state, 0);
                return Result<void, Error>::failure({name + ": " + message});
            }
            return Result<void, Error>::success();
        }

        Result<ParameterMap, Error> read_defaults() {
            ParameterMap result;
            if(auto read = call(default_table, 1); !read)
                return Result<ParameterMap, Error>::failure(read.error());
            if(lua_isnil(state, -1)) {
                lua_settop(state, 0);
                return Result<ParameterMap, Error>::success({});
            }
            if(!lua_istable(state, -1))
                return Result<ParameterMap, Error>::failure({"properties must be a table"});
            lua_pushnil(state);
            while(lua_next(state, -2)) {
                if(lua_type(state, -2) != LUA_TSTRING || result.size() >= 128)
                    return Result<ParameterMap, Error>::failure(
                        {"Invalid script property name/count"});
                size_t length = 0;
                const char* name = lua_tolstring(state, -2, &length);
                ParameterValue value;
                switch(lua_type(state, -1)) {
                    case LUA_TBOOLEAN:
                        value = static_cast<bool>(lua_toboolean(state, -1));
                        break;
                    case LUA_TNUMBER: {
                        const auto number = lua_tonumber(state, -1);
                        if(!std::isfinite(number)
                            || std::abs(number) > std::numeric_limits<float>::max())
                            return Result<ParameterMap, Error>::failure(
                                {"Property needs a finite float"});
                        value = static_cast<float>(number);
                        break;
                    }
                    case LUA_TSTRING: {
                        size_t size = 0;
                        const char* text = lua_tolstring(state, -1, &size);
                        value = std::string(text, size);
                        break;
                    }
                    case LUA_TTABLE: {
                        if(lua_rawlen(state, -1) != 3)
                            return Result<ParameterMap, Error>::failure(
                                {"Vector property needs three numbers"});
                        Math::Vec3 vector;
                        for(int i = 0; i < 3; ++i) {
                            lua_rawgeti(state, -1, i + 1);
                            if(lua_type(state, -1) != LUA_TNUMBER)
                                return Result<ParameterMap, Error>::failure(
                                    {"Vector property needs numbers"});
                            const auto number = lua_tonumber(state, -1);
                            if(!std::isfinite(number)
                                || std::abs(number) > std::numeric_limits<float>::max())
                                return Result<ParameterMap, Error>::failure(
                                    {"Vector needs finite floats"});
                            vector[i] = static_cast<float>(number);
                            lua_pop(state, 1);
                        }
                        value = vector;
                        break;
                    }
                    default:
                        return Result<ParameterMap, Error>::failure(
                            {"Unsupported script property type"});
                }
                result.emplace(std::string(name, length), std::move(value));
                lua_pop(state, 1);
            }
            lua_settop(state, 0);
            if(!valid_parameters(result))
                return Result<ParameterMap, Error>::failure({"Invalid script property values"});
            return Result<ParameterMap, Error>::success(std::move(result));
        }
    };

    Script::Instance::Instance(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
    Script::Instance::~Instance() = default;

    Result<void, Error> Script::Instance::invoke(Phase phase, Entity entity,
        const ParameterMap& parameters, const System::Context* context) {
        if(!valid_parameters(parameters))
            return Result<void, Error>::failure({"Invalid script parameters"});
        m_impl->entity = entity;
        m_impl->parameters = &parameters;
        m_impl->context = context;
        m_impl->phase = phase;
        const auto result = m_impl->call(Impl::dispatch);
        m_impl->entity = {};
        m_impl->parameters = nullptr;
        m_impl->context = nullptr;
        return result;
    }

    Result<std::unique_ptr<Script::Instance>, Error> Script::instantiate() const {
        auto impl = std::make_unique<Instance::Impl>();
        impl->source = m_source;
        impl->name = "@" + m_name;
        impl->state = lua_newstate(Instance::Impl::allocate, impl.get());
        if(!impl->state)
            return Result<std::unique_ptr<Instance>, Error>::failure({"Cannot create Lua state"});
        *static_cast<Instance::Impl**>(lua_getextraspace(impl->state)) = impl.get();
        if(auto initialized = impl->call(Instance::Impl::initialize); !initialized)
            return Result<std::unique_ptr<Instance>, Error>::failure(initialized.error());
        return Result<std::unique_ptr<Instance>, Error>::success(
            std::unique_ptr<Instance>(new Instance(std::move(impl))));
    }

    Result<std::shared_ptr<Script>, Error> Script::create(std::string source, std::string name) {
        if(source.size() > 1024 * 1024)
            return Result<std::shared_ptr<Script>, Error>::failure({"Script exceeds 1 MiB limit"});
        auto script = std::make_shared<Script>();
        script->m_source = std::move(source);
        script->m_name = std::move(name);
        auto instance = script->instantiate();
        if(!instance)
            return Result<std::shared_ptr<Script>, Error>::failure(instance.error());
        auto defaults = instance.value()->m_impl->read_defaults();
        if(!defaults)
            return Result<std::shared_ptr<Script>, Error>::failure(
                {script->m_name + ": " + defaults.error().message});
        script->m_defaults = std::move(defaults).value();
        return Result<std::shared_ptr<Script>, Error>::success(std::move(script));
    }

    Result<std::shared_ptr<Script>, Error> Script::load(const std::filesystem::path& path) {
        std::error_code error;
        if(std::filesystem::file_size(path, error) > 1024 * 1024 || error)
            return Result<std::shared_ptr<Script>, Error>::failure(
                {"Cannot read script or size exceeds 1 MiB: " + path.string()});
        auto source = read_text_file(path);
        if(!source)
            return Result<std::shared_ptr<Script>, Error>::failure({source.error()});
        return create(std::move(source).value(), path.string());
    }

    Result<ParameterMap, Error> Script::parameters(const ParameterMap& overrides) const {
        if(!valid_parameters(overrides))
            return Result<ParameterMap, Error>::failure({"Invalid script parameter values"});
        auto values = m_defaults;
        for(const auto& [name, value] : overrides) {
            const auto found = values.find(name);
            if(found == values.end() || found->second.index() != value.index())
                return Result<ParameterMap, Error>::failure(
                    {"Script parameter no longer matches declaration: " + name});
            found->second = value;
        }
        return Result<ParameterMap, Error>::success(std::move(values));
    }
}

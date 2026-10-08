#include "scripting/script_instance.h"
#include "scripting/script_compiler.h"
#include "asset/data/script_sources.h"
#include "audio/audio_commands.h"
#include "physics/physics_commands.h"
#include "scripting/lua_bindings.h"
#include "scene/scene.h"
#include "scene/runtime_session.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cmath>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string_view>

namespace Comet {
    namespace {
        constexpr std::size_t MAX_MODULES = 64;
        constexpr std::size_t MAX_MODULE_DEPTH = 16;
    }

    struct ScriptInstance::Impl {
        using Property = Script::Property;
        using PropertyMap = Script::PropertyMap;
        using EventHandlers = Script::EventHandlers;
        static constexpr size_t MEMORY_LIMIT = 8 * 1024 * 1024;
        static constexpr std::array PHASE_NAMES{"on_start", "fixed_update", "update", "on_stop",
            "on_collision_enter", "on_collision_exit", "on_trigger_enter", "on_trigger_exit"};
        static_assert(PHASE_NAMES.size() == static_cast<size_t>(Phase::TriggerExit) + 1);
        lua_State* state = nullptr;
        size_t memory = 0;
        int budget = 0;
        std::string_view source;
        std::string name;
        struct Module {
            const ScriptSources::File* source;
            int reference = LUA_NOREF;
        };
        std::shared_ptr<const ScriptSources> sources;
        ScriptSources* collecting = nullptr;
        std::vector<std::filesystem::path>* collected_dependencies = nullptr;
        std::vector<std::filesystem::path> allowed_dependencies;
        std::map<std::filesystem::path, Module> modules;
        std::vector<std::filesystem::path> module_stack;
        std::string module_error;
        bool initializing = true;
        int definition = LUA_NOREF;
        int self = LUA_NOREF;
        LuaBindings::Context bindings;
        Scene* active_scene = nullptr;
        std::uint64_t scene_generation = 0;
        const ParameterMap* parameters = nullptr;
        std::optional<ParameterMap> previous_parameters;
        int parameter_table = LUA_NOREF;
        bool parameters_changed = true;
        double delta_time = 0;
        Phase phase = Phase::Start;
        Entity contact_other;
        std::string_view event_handler;
        const ParameterValue* event_value = nullptr;

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

        Module* prepare_module(const std::string_view name) {
            // 此 helper 不调用 Lua；所有 C++ 临时值均在回调抛 Lua 错误前析构。
            const auto relative = Script::module_path(name);
            if(!relative) {
                module_error = relative.error();
                return nullptr;
            }
            const auto& path = relative.value();
            const auto existing = modules.find(path);
            if(existing != modules.end()) {
                if(existing->second.reference != LUA_NOREF)
                    return &existing->second;
                module_error = "Module dependency cycle: ";
                for(const auto& parent : module_stack)
                    module_error += parent.generic_string() + " -> ";
                module_error += path.generic_string();
                return nullptr;
            }
            if(!initializing) {
                module_error =
                    "Module was not loaded during script initialization: " + path.generic_string();
                return nullptr;
            }
            if(module_stack.size() >= MAX_MODULE_DEPTH) {
                module_error = "Module dependency depth exceeds 16";
                return nullptr;
            }
            if(modules.size() >= MAX_MODULES) {
                module_error = "Script exceeds 64 modules";
                return nullptr;
            }
            const ScriptSources::File* file = nullptr;
            if(collecting) {
                auto captured = collecting->capture(path, collected_dependencies);
                if(!captured) {
                    module_error = captured.error();
                    return nullptr;
                }
                file = captured.value();
            } else {
                if(std::ranges::find(allowed_dependencies, path) == allowed_dependencies.end()) {
                    module_error = "Module is outside the prepared script dependencies: "
                                   + path.generic_string();
                    return nullptr;
                }
                const auto found = sources->files.find(path);
                if(found == sources->files.end()) {
                    module_error = "Module source is missing from the script snapshot: "
                                   + path.generic_string();
                    return nullptr;
                }
                file = &found->second;
            }
            const auto inserted = modules.emplace(path, Module{file});
            module_stack.push_back(path);
            return &inserted.first->second;
        }

        static int load_text(lua_State* state, std::string_view source, const char* name) {
            // 只调整解析视图，原始快照仍参与大小限制与重载一致性校验。
            if(source.starts_with("\xef\xbb\xbf"))
                source.remove_prefix(3);
            return luaL_loadbufferx(state, source.data(), source.size(), name, "t");
        }

        static int require_module(lua_State* state) {
            auto& vm = current(state);
            if(lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TSTRING)
                return luaL_error(state, "require expects exactly one module name");
            size_t size = 0;
            const char* name = lua_tolstring(state, 1, &size);
            Module* module = vm.prepare_module(std::string_view(name, size));
            if(!module)
                return luaL_error(state, "%s", vm.module_error.c_str());
            if(module->reference != LUA_NOREF) {
                lua_rawgeti(state, LUA_REGISTRYINDEX, module->reference);
                return 1;
            }
            if(load_text(state, module->source->source, module->source->name.c_str()) != LUA_OK)
                return lua_error(state);
            // 这里只有平凡局部；递归模块执行或 Lua 分配失败不会跨越 C++ 所有者。
            lua_call(state, 0, 1);
            if(!lua_istable(state, -1))
                return luaL_error(state, "Module '%s' must return a table", name);
            lua_pushvalue(state, -1);
            module->reference = luaL_ref(state, LUA_REGISTRYINDEX);
            vm.module_stack.pop_back();
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
                    "xpcall", "setmetatable", "getmetatable", "rawset", "print"}) {
                lua_pushnil(state);
                lua_setglobal(state, name);
            }
            LuaBindings::install(state, vm.bindings);
            if(vm.sources) {
                lua_pushcfunction(state, require_module);
                lua_setglobal(state, "require");
            }
            if(load_text(state, vm.source, vm.name.c_str()) != LUA_OK)
                return lua_error(state);
            lua_call(state, 0, 1);
            if(!lua_istable(state, -1))
                return luaL_error(state, "Script must return a table");
            for(const char* name : PHASE_NAMES) {
                lua_getfield(state, -1, name);
                const bool valid = lua_isnil(state, -1) || lua_isfunction(state, -1);
                lua_pop(state, 1);
                if(!valid)
                    return luaL_error(state, "Lifecycle entry '%s' must be a function", name);
            }
            vm.definition = luaL_ref(state, LUA_REGISTRYINDEX);
            lua_newtable(state);
            // 只继承本 VM 的定义字段，不把用户字段安装成实例元方法。
            lua_newtable(state);
            lua_rawgeti(state, LUA_REGISTRYINDEX, vm.definition);
            lua_setfield(state, -2, "__index");
            lua_setmetatable(state, -2);
            vm.self = luaL_ref(state, LUA_REGISTRYINDEX);
            return 0;
        }

        static int readonly_write(lua_State* state) {
            return luaL_error(
                state, "Script parameters are read-only; store runtime state on self");
        }
        static int readonly_next(lua_State* state) {
            lua_settop(state, 2);
            lua_pushvalue(state, lua_upvalueindex(1));
            lua_pushvalue(state, 2);
            if(!lua_next(state, -2))
                return 0;
            return 2;
        }
        static int readonly_pairs(lua_State* state) {
            lua_pushvalue(state, lua_upvalueindex(1));
            lua_pushcclosure(state, readonly_next, 1);
            lua_pushnil(state);
            lua_pushnil(state);
            return 3;
        }
        static int readonly_length(lua_State* state) {
            lua_pushinteger(
                state, static_cast<lua_Integer>(lua_rawlen(state, lua_upvalueindex(1))));
            return 1;
        }
        static void make_readonly(lua_State* state) {
            // 用代理隔离脚本写入，才能安全复用未变化的配置表。
            lua_newtable(state);
            lua_newtable(state);
            lua_pushvalue(state, -3);
            lua_setfield(state, -2, "__index");
            lua_pushcfunction(state, readonly_write);
            lua_setfield(state, -2, "__newindex");
            lua_pushvalue(state, -3);
            lua_pushcclosure(state, readonly_pairs, 1);
            lua_setfield(state, -2, "__pairs");
            lua_pushvalue(state, -3);
            lua_pushcclosure(state, readonly_length, 1);
            lua_setfield(state, -2, "__len");
            lua_setmetatable(state, -2);
            lua_remove(state, -2);
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
                    else if constexpr(std::is_same_v<T, EntityUuid>) {
                        const auto& context = current(state).bindings;
                        Entity entity;
                        if(context.scene && value)
                            entity = context.scene->find_entity(value);
                        LuaBindings::push_entity_reference(state, entity, context.scene_generation);
                    } else {
                        constexpr int components = std::is_same_v<T, Math::Vec4> ? 4 : 3;
                        lua_createtable(state, components, 0);
                        for(int i = 0; i < components; ++i) {
                            lua_pushnumber(state, value[i]);
                            lua_rawseti(state, -2, i + 1);
                        }
                        make_readonly(state);
                    }
                },
                parameter);
        }

        static int dispatch(lua_State* state) {
            auto& vm = current(state);
            if(vm.parameters_changed) {
                lua_newtable(state);
                for(const auto& [name, value] : *vm.parameters) {
                    push_parameter(state, value);
                    lua_setfield(state, -2, name.c_str());
                }
                make_readonly(state);
                const int replacement = luaL_ref(state, LUA_REGISTRYINDEX);
                luaL_unref(state, LUA_REGISTRYINDEX, vm.parameter_table);
                vm.parameter_table = replacement;
            }
            lua_rawgeti(state, LUA_REGISTRYINDEX, vm.definition);
            if(vm.phase == Phase::Event) {
                lua_pushlstring(state, vm.event_handler.data(), vm.event_handler.size());
                lua_gettable(state, -2);
                if(!lua_isfunction(state, -1))
                    return luaL_error(state, "Event handler must be a function");
            } else {
                lua_getfield(state, -1, PHASE_NAMES[static_cast<size_t>(vm.phase)]);
                if(lua_isnil(state, -1))
                    return 0;
            }
            lua_rawgeti(state, LUA_REGISTRYINDEX, vm.self);
            lua_rawgeti(state, LUA_REGISTRYINDEX, vm.parameter_table);
            lua_setfield(state, -2, "parameters");
            if(vm.phase == Phase::Event) {
                if(vm.event_value)
                    push_parameter(state, *vm.event_value);
                else
                    lua_pushnil(state);
            } else if(vm.phase >= Phase::CollisionEnter && vm.phase <= Phase::TriggerExit) {
                if(!vm.contact_other)
                    return luaL_error(state, "Contact entity is no longer available");
                LuaBindings::push_entity_reference(
                    state, vm.contact_other, vm.bindings.scene_generation);
            } else {
                lua_pushnumber(state, vm.delta_time);
            }
            lua_call(state, 2, 0);
            return 0;
        }

        static int traceback(lua_State* state) {
            const char* message = "Lua raised a non-string error";
            if(lua_type(state, 1) == LUA_TSTRING)
                message = lua_tostring(state, 1);
            luaL_traceback(state, state, message, 1);
            return 1;
        }

        Result<void, Error> call(lua_CFunction function, void* argument = nullptr) {
            const int initial_top = lua_gettop(state);
            budget = 200;
            lua_sethook(state, limit, LUA_MASKCOUNT, 1000);
            lua_pushcfunction(state, traceback);
            lua_pushcfunction(state, function);
            lua_pushlightuserdata(state, argument);
            const int status = lua_pcall(state, 1, 0, initial_top + 1);
            lua_sethook(state, nullptr, 0, 0);
            if(status != LUA_OK) {
                std::string message = "Lua raised a non-string error";
                if(lua_type(state, -1) == LUA_TSTRING)
                    message = lua_tostring(state, -1);
                lua_settop(state, initial_top);
                return Result<void, Error>::failure({name + ": " + message});
            }
            lua_settop(state, initial_top);
            return Result<void, Error>::success();
        }

        static int read_vector_value(
            lua_State* state, ParameterValue& value, const bool color = false) {
            if(!lua_istable(state, -1))
                return luaL_error(state, "Color default needs four numbers");
            const auto components = lua_rawlen(state, -1);
            if(color && components != 4)
                return luaL_error(state, "Color default needs four numbers");
            if(components != 3 && components != 4)
                return luaL_error(state, "Vector property needs three or four numbers");
            lua_pushnil(state);
            while(lua_next(state, -2)) {
                if(!lua_isinteger(state, -2) || lua_tointeger(state, -2) < 1
                    || static_cast<lua_Unsigned>(lua_tointeger(state, -2)) > components)
                    return luaL_error(
                        state, "Vector property needs a dense array without extra keys");
                lua_pop(state, 1);
            }
            Math::Vec4 vector(0);
            for(size_t i = 0; i < components; ++i) {
                lua_rawgeti(state, -1, static_cast<lua_Integer>(i + 1));
                if(lua_type(state, -1) != LUA_TNUMBER)
                    return luaL_error(state, "Vector property needs numbers");
                const auto number = lua_tonumber(state, -1);
                if(!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
                    return luaL_error(state, "Vector needs finite floats");
                vector[i] = static_cast<float>(number);
                lua_pop(state, 1);
            }
            if(components == 4)
                value = vector;
            else
                value = Math::Vec3(vector);
            return 0;
        }

        static int read_property(lua_State* state, Property& property) {
            auto& value = property.default_value;
            switch(lua_type(state, -1)) {
                case LUA_TBOOLEAN:
                    value = static_cast<bool>(lua_toboolean(state, -1));
                    break;
                case LUA_TNUMBER: {
                    const auto number = lua_tonumber(state, -1);
                    if(!std::isfinite(number)
                        || std::abs(number) > std::numeric_limits<float>::max())
                        return luaL_error(state, "Property needs a finite float");
                    value = static_cast<float>(number);
                    break;
                }
                case LUA_TSTRING: {
                    size_t size = 0;
                    const char* text = lua_tolstring(state, -1, &size);
                    if(size > 4096)
                        return luaL_error(state, "Property string exceeds 4096 bytes");
                    value = std::string(text, size);
                    break;
                }
                case LUA_TTABLE: {
                    lua_getfield(state, -1, "type");
                    const bool declared = !lua_isnil(state, -1);
                    const bool entity_reference =
                        lua_type(state, -1) == LUA_TSTRING && lua_rawlen(state, -1) == 6
                        && std::string_view(lua_tostring(state, -1)) == "entity";
                    const bool color = lua_type(state, -1) == LUA_TSTRING
                                       && lua_rawlen(state, -1) == 5
                                       && std::string_view(lua_tostring(state, -1)) == "color";
                    lua_pop(state, 1);
                    if(declared) {
                        if(!entity_reference && !color)
                            return luaL_error(state, "Unknown script property type");
                        lua_pushnil(state);
                        while(lua_next(state, -2)) {
                            const bool type_field =
                                lua_type(state, -2) == LUA_TSTRING && lua_rawlen(state, -2) == 4
                                && std::string_view(lua_tostring(state, -2)) == "type";
                            const bool default_field =
                                color && lua_type(state, -2) == LUA_TSTRING
                                && lua_rawlen(state, -2) == 7
                                && std::string_view(lua_tostring(state, -2)) == "default";
                            if(!type_field && !default_field) {
                                if(color)
                                    return luaL_error(
                                        state, "Color property only accepts type and default");
                                return luaL_error(state,
                                    "Entity property only accepts type; assign its target in the scene");
                            }
                            lua_pop(state, 1);
                        }
                        if(color) {
                            lua_getfield(state, -1, "default");
                            read_vector_value(state, value, true);
                            lua_pop(state, 1);
                            property.semantic = Property::Semantic::Color;
                        } else {
                            value = EntityUuid{};
                        }
                        break;
                    }
                    read_vector_value(state, value);
                    break;
                }
                default:
                    return luaL_error(state, "Unsupported script property type");
            }
            return 0;
        }

        static int collect_properties(lua_State* state) {
            auto& vm = current(state);
            auto& result = *static_cast<PropertyMap*>(lua_touserdata(state, 1));
            lua_rawgeti(state, LUA_REGISTRYINDEX, vm.definition);
            lua_getfield(state, -1, "properties");
            if(lua_isnil(state, -1))
                return 0;
            if(!lua_istable(state, -1))
                return luaL_error(state, "properties must be a table");
            lua_pushnil(state);
            while(lua_next(state, -2)) {
                if(lua_type(state, -2) != LUA_TSTRING || result.size() >= 128)
                    return luaL_error(state, "Invalid script property name/count");
                size_t length = 0;
                const char* name = lua_tolstring(state, -2, &length);
                if(!valid_parameter_name(std::string_view(name, length)))
                    return luaL_error(state, "Invalid script property name/count");
                auto& property = result[std::string(name, length)];
                read_property(state, property);
                lua_pop(state, 1);
            }
            return 0;
        }

        Result<PropertyMap, Error> read_properties() {
            // 回调只借用结果；Lua longjmp 返回后，外层仍能正常释放已解析的 C++ 值。
            PropertyMap result;
            const auto read = call(collect_properties, &result);
            if(!read)
                return Result<PropertyMap, Error>::failure(read.error());
            return Result<PropertyMap, Error>::success(std::move(result));
        }

        static int collect_event_handlers(lua_State* state) {
            auto& vm = current(state);
            auto& result = *static_cast<EventHandlers*>(lua_touserdata(state, 1));
            lua_rawgeti(state, LUA_REGISTRYINDEX, vm.definition);
            const int definition = lua_absindex(state, -1);
            lua_getfield(state, definition, "events");
            if(lua_isnil(state, -1))
                return 0;
            if(!lua_istable(state, -1))
                return luaL_error(state, "events must be a table");
            lua_pushnil(state);
            while(lua_next(state, -2)) {
                if(lua_type(state, -2) != LUA_TSTRING || lua_type(state, -1) != LUA_TSTRING
                    || result.size() >= 128)
                    return luaL_error(state, "Invalid script event name/handler/count");
                size_t name_length = 0;
                size_t handler_length = 0;
                const char* name = lua_tolstring(state, -2, &name_length);
                const char* handler = lua_tolstring(state, -1, &handler_length);
                if(!valid_parameter_name(std::string_view(name, name_length))
                    || !valid_parameter_name(std::string_view(handler, handler_length)))
                    return luaL_error(state, "Invalid script event name/handler/count");
                lua_getfield(state, definition, handler);
                if(!lua_isfunction(state, -1))
                    return luaL_error(state, "Event handler '%s' must be a function", handler);
                lua_pop(state, 1);
                result.emplace(
                    std::string(name, name_length), std::string(handler, handler_length));
                lua_pop(state, 1);
            }
            return 0;
        }

        Result<EventHandlers, Error> read_event_handlers() {
            EventHandlers result;
            const auto read = call(collect_event_handlers, &result);
            if(!read)
                return Result<EventHandlers, Error>::failure(read.error());
            return Result<EventHandlers, Error>::success(std::move(result));
        }
    };

    ScriptInstance::ScriptInstance(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
    ScriptInstance::~ScriptInstance() = default;

    Result<void, Error> ScriptInstance::invoke(
        Phase phase, Entity entity, const ParameterMap& parameters, Invocation invocation) {
        if(static_cast<std::size_t>(phase) > static_cast<std::size_t>(Phase::Event))
            return Result<void, Error>::failure({"Invalid script phase"});
        if(phase == Phase::Stop) {
            entity = {};
            invocation.scene = nullptr;
            invocation.session = nullptr;
            invocation.audio = nullptr;
            invocation.physics = nullptr;
            invocation.input = nullptr;
            invocation.materials = nullptr;
        }
        if(invocation.session
            && (!invocation.scene || !invocation.session->is_bound_to(*invocation.scene)))
            return Result<void, Error>::failure(
                {"Script session is inactive or belongs to another scene"});
        if(invocation.audio
            && (!invocation.scene || !invocation.audio->is_bound_to(*invocation.scene)))
            return Result<void, Error>::failure(
                {"Script audio service is inactive or belongs to another scene"});
        if(invocation.physics
            && (!invocation.scene || !invocation.physics->is_bound_to(*invocation.scene)))
            return Result<void, Error>::failure(
                {"Script physics service is inactive or belongs to another scene"});
        if(phase == Phase::Event) {
            if(!valid_parameter_name(invocation.event_handler))
                return Result<void, Error>::failure({"Invalid script event handler"});
            if(invocation.event_value && !valid_parameter_value(*invocation.event_value))
                return Result<void, Error>::failure({"Invalid script event value"});
        }
        m_impl->parameters_changed =
            !m_impl->previous_parameters || *m_impl->previous_parameters != parameters;
        if(m_impl->parameters_changed && !valid_parameters(parameters))
            return Result<void, Error>::failure({"Invalid script parameters"});
        if(invocation.scene && entity && !invocation.scene->is_valid(entity))
            return Result<void, Error>::failure({"Script entity belongs to another scene"});
        if(m_impl->active_scene != invocation.scene) {
            m_impl->active_scene = invocation.scene;
            ++m_impl->scene_generation;
            m_impl->parameters_changed = true;
        }
        m_impl->bindings = {
            .entity = entity,
            .scene = invocation.scene,
            .session = invocation.session,
            .audio = invocation.audio,
            .physics = invocation.physics,
            .input = invocation.input,
            .scene_generation = m_impl->scene_generation,
            .materials = invocation.materials,
            .can_request_restart = phase != Phase::Start && phase != Phase::Stop,
            .can_log = true,
        };
        if(phase == Phase::Stop)
            m_impl->bindings.disabled_input_contexts = invocation.disabled_input_contexts;
        m_impl->parameters = &parameters;
        m_impl->delta_time = invocation.delta_time;
        m_impl->phase = phase;
        m_impl->contact_other = invocation.contact_other;
        m_impl->event_handler = invocation.event_handler;
        m_impl->event_value = invocation.event_value;
        const auto result = m_impl->call(Impl::dispatch);
        m_impl->bindings = {};
        m_impl->contact_other = {};
        m_impl->event_handler = {};
        m_impl->event_value = nullptr;
        m_impl->parameters = nullptr;
        if(result && m_impl->parameters_changed)
            m_impl->previous_parameters = parameters;
        if(!result)
            m_impl->previous_parameters.reset();
        return result;
    }

    Result<void, Error> ScriptInstance::invoke(
        Phase phase, Entity entity, const ParameterMap& parameters) {
        return invoke(phase, entity, parameters, {});
    }

    Result<std::unique_ptr<ScriptInstance>, Error> ScriptInstance::create(const Script& script) {
        return create(script, nullptr, nullptr);
    }

    Result<std::unique_ptr<ScriptInstance>, Error> ScriptInstance::create(const Script& script,
        ScriptSources* collecting, std::vector<std::filesystem::path>* dependencies) {
        auto impl = std::make_unique<ScriptInstance::Impl>();
        impl->sources = script.m_sources;
        impl->collecting = collecting;
        impl->collected_dependencies = dependencies;
        impl->allowed_dependencies = script.m_dependencies;
        if(script.m_sources)
            impl->source = script.m_sources->files.at(script.m_source_path).source;
        else
            impl->source = script.m_source;
        impl->name = "@" + script.m_name;
        impl->state = lua_newstate(ScriptInstance::Impl::allocate, impl.get());
        if(!impl->state)
            return Result<std::unique_ptr<ScriptInstance>, Error>::failure(
                {"Cannot create Lua state"});
        *static_cast<ScriptInstance::Impl**>(lua_getextraspace(impl->state)) = impl.get();
        if(auto initialized = impl->call(ScriptInstance::Impl::initialize); !initialized)
            return Result<std::unique_ptr<ScriptInstance>, Error>::failure(initialized.error());
        impl->source = {};
        impl->collecting = nullptr;
        impl->collected_dependencies = nullptr;
        impl->initializing = false;
        return Result<std::unique_ptr<ScriptInstance>, Error>::success(
            std::unique_ptr<ScriptInstance>(new ScriptInstance(std::move(impl))));
    }

    Result<void, Error> prepare_script_definition(Script& script, ScriptSources* collecting,
        std::vector<std::filesystem::path>* dependencies) {
        auto instance = ScriptInstance::create(script, collecting, dependencies);
        if(!instance)
            return Result<void, Error>::failure(instance.error());
        auto properties = instance.value()->m_impl->read_properties();
        if(!properties)
            return Result<void, Error>::failure(properties.error());
        script.m_properties = std::move(properties).value();
        auto events = instance.value()->m_impl->read_event_handlers();
        if(!events)
            return Result<void, Error>::failure(events.error());
        script.m_event_handlers = std::move(events).value();
        return Result<void, Error>::success();
    }
}

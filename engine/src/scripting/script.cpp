#include "scripting/script.h"
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
#include <fstream>
#include <limits>
#include <optional>
#include <string_view>

namespace Comet {
    namespace {
        constexpr std::size_t MAX_SOURCE_BYTES = 1024 * 1024;
        constexpr std::size_t MAX_GROUP_BYTES = 8 * MAX_SOURCE_BYTES;
        constexpr std::size_t MAX_GROUP_FILES = 256;
        constexpr std::size_t MAX_GROUP_ROOTS = 128;
        constexpr std::size_t MAX_MODULES = 64;
        constexpr std::size_t MAX_MODULE_DEPTH = 16;
        constexpr std::size_t MAX_MODULE_NAME = 256;

        bool is_module_path(const std::filesystem::path& path) {
            auto name = path.filename().string();
            std::ranges::transform(name, name.begin(), [](const unsigned char value) {
                if(value >= 'A' && value <= 'Z')
                    return static_cast<char>(value + 'a' - 'A');
                return static_cast<char>(value);
            });
            return name.ends_with(".module.lua");
        }

        bool safe_source_path(const std::filesystem::path& path) {
            if(path.empty() || path.is_absolute() || path.has_root_name())
                return false;
            const auto text = path.generic_string();
            if(text.find('\0') != std::string::npos || text.find('\\') != std::string::npos)
                return false;
            for(const auto& part : path)
                if(part == "..")
                    return false;
            return path.lexically_normal() != ".";
        }

        Result<std::filesystem::path> resolve_source(
            const std::filesystem::path& root, const std::filesystem::path& relative) {
            if(!safe_source_path(relative))
                return Result<std::filesystem::path>::failure("Invalid project script path");
            std::error_code error;
            auto resolved = std::filesystem::weakly_canonical(root / relative, error);
            if(error)
                return Result<std::filesystem::path>::failure(
                    "Cannot resolve project script: " + relative.generic_string());
            const auto inside = resolved.lexically_relative(root);
            if(!safe_source_path(inside))
                return Result<std::filesystem::path>::failure(
                    "Script source is outside project assets: " + relative.generic_string());
            if(resolved != (root / relative).lexically_normal())
                return Result<std::filesystem::path>::failure(
                    "Script source symlink aliases are not supported: "
                    + relative.generic_string());
            return Result<std::filesystem::path>::success(std::move(resolved));
        }

        Result<std::string> read_source(const std::filesystem::path& path) {
            std::error_code error;
            if(!std::filesystem::is_regular_file(path, error) || error)
                return Result<std::string>::failure("Cannot read script: " + path.string());
            std::ifstream input(path, std::ios::binary);
            if(!input)
                return Result<std::string>::failure("Cannot read script: " + path.string());
            std::string source;
            std::array<char, 8192> buffer;
            while(input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto count = static_cast<std::size_t>(input.gcount());
                if(count > MAX_SOURCE_BYTES - source.size())
                    return Result<std::string>::failure(
                        "Script exceeds 1 MiB limit: " + path.string());
                source.append(buffer.data(), count);
            }
            if(!input.eof())
                return Result<std::string>::failure("Cannot read script: " + path.string());
            return Result<std::string>::success(std::move(source));
        }

        bool valid_module_name(const std::string_view name) {
            if(name.empty() || name.size() > MAX_MODULE_NAME)
                return false;
            bool first = true;
            for(const unsigned char value : name) {
                if(value == '.') {
                    if(first)
                        return false;
                    first = true;
                    continue;
                }
                const bool letter = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
                                    || value == '_';
                if(!letter && (first || value < '0' || value > '9'))
                    return false;
                first = false;
            }
            return !first;
        }
    }

    struct Script::SourceSet {
        struct File {
            std::filesystem::path resolved;
            std::string source;
            std::string name;
        };
        struct Unreadable {
            std::filesystem::path resolved;
            std::filesystem::file_type type;
            std::optional<std::filesystem::file_time_type> write_time;
            std::optional<std::uintmax_t> size;
            bool read_failed = true;

            bool operator==(const Unreadable&) const = default;
        };
        std::filesystem::path root;
        std::map<std::filesystem::path, File> files;
        std::map<std::filesystem::path, Unreadable> unreadable;
        std::size_t bytes = 0;

        static Unreadable inspect_unreadable(const std::filesystem::path& path) {
            std::error_code error;
            Unreadable result{path, std::filesystem::status(path, error).type(), {}, {}};
            error.clear();
            const auto time = std::filesystem::last_write_time(path, error);
            if(!error)
                result.write_time = time;
            error.clear();
            const auto size = std::filesystem::file_size(path, error);
            if(!error)
                result.size = size;
            return result;
        }

        Result<const File*> capture(const std::filesystem::path& path,
            std::vector<std::filesystem::path>* dependencies = nullptr) {
            const auto existing = files.find(path);
            if(existing != files.end()) {
                if(dependencies && std::ranges::find(*dependencies, path) == dependencies->end())
                    dependencies->push_back(path);
                return Result<const File*>::success(&existing->second);
            }
            auto resolved = resolve_source(root, path);
            if(!resolved)
                return Result<const File*>::failure(resolved.error());
            if(dependencies && std::ranges::find(*dependencies, path) == dependencies->end())
                dependencies->push_back(path);
            if(files.size() >= MAX_GROUP_FILES)
                return Result<const File*>::failure("Script group exceeds 256 source files");
            const auto observation = inspect_unreadable(resolved.value());
            auto source = read_source(resolved.value());
            if(!source) {
                unreadable.try_emplace(path, observation);
                return Result<const File*>::failure(source.error());
            }
            if(source.value().size() > MAX_GROUP_BYTES - bytes) {
                auto rejected = observation;
                rejected.read_failed = false;
                unreadable.try_emplace(path, std::move(rejected));
                return Result<const File*>::failure("Script group exceeds 8 MiB source limit");
            }
            bytes += source.value().size();
            const auto inserted =
                files.emplace(path, File{std::move(resolved).value(), std::move(source).value(),
                                        "@" + path.generic_string()});
            return Result<const File*>::success(&inserted.first->second);
        }

        bool inputs_are_current() const {
            for(const auto& [path, file] : files) {
                const auto resolved = resolve_source(root, path);
                if(!resolved || resolved.value() != file.resolved)
                    return false;
                const auto source = read_source(resolved.value());
                if(!source || source.value() != file.source)
                    return false;
            }
            for(const auto& [path, observation] : unreadable) {
                const auto resolved = resolve_source(root, path);
                if(!resolved)
                    return false;
                auto current = inspect_unreadable(resolved.value());
                current.read_failed = observation.read_failed;
                if(current != observation)
                    return false;
                // 同样状态的不可读文件若已可读，也必须重试原候选。
                if(observation.read_failed && observation.size
                    && *observation.size <= MAX_SOURCE_BYTES
                    && observation.type == std::filesystem::file_type::regular
                    && read_source(resolved.value()))
                    return false;
            }
            return true;
        }
    };

    struct Script::Instance::Impl {
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
            const SourceSet::File* source;
            int reference = LUA_NOREF;
        };
        std::shared_ptr<const SourceSet> sources;
        SourceSet* collecting = nullptr;
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
            const SourceSet::File* file = nullptr;
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

    Script::Instance::Instance(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
    Script::Instance::~Instance() = default;

    Result<void, Error> Script::Instance::invoke(
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

    Result<void, Error> Script::Instance::invoke(
        Phase phase, Entity entity, const ParameterMap& parameters) {
        return invoke(phase, entity, parameters, {});
    }

    Result<std::unique_ptr<Script::Instance>, Error> Script::instantiate() const {
        return instantiate(nullptr, nullptr);
    }

    Result<std::unique_ptr<Script::Instance>, Error> Script::instantiate(
        SourceSet* collecting, std::vector<std::filesystem::path>* dependencies) const {
        auto impl = std::make_unique<Instance::Impl>();
        impl->sources = m_sources;
        impl->collecting = collecting;
        impl->collected_dependencies = dependencies;
        impl->allowed_dependencies = m_dependencies;
        impl->source = m_sources ? m_sources->files.at(m_source_path).source : m_source;
        impl->name = "@" + m_name;
        impl->state = lua_newstate(Instance::Impl::allocate, impl.get());
        if(!impl->state)
            return Result<std::unique_ptr<Instance>, Error>::failure({"Cannot create Lua state"});
        *static_cast<Instance::Impl**>(lua_getextraspace(impl->state)) = impl.get();
        if(auto initialized = impl->call(Instance::Impl::initialize); !initialized)
            return Result<std::unique_ptr<Instance>, Error>::failure(initialized.error());
        impl->source = {};
        impl->collecting = nullptr;
        impl->collected_dependencies = nullptr;
        impl->initializing = false;
        return Result<std::unique_ptr<Instance>, Error>::success(
            std::unique_ptr<Instance>(new Instance(std::move(impl))));
    }

    Result<std::filesystem::path> Script::module_path(const std::string_view name) {
        if(!valid_module_name(name))
            return Result<std::filesystem::path>::failure(
                "require expects a bounded dotted module name");
        std::string relative(name);
        std::ranges::replace(relative, '.', '/');
        return Result<std::filesystem::path>::success(relative + ".module.lua");
    }

    Result<std::string> Script::module_name(const std::filesystem::path& relative_path) {
        constexpr std::string_view suffix = ".module.lua";
        const auto path = relative_path.generic_string();
        if(!safe_source_path(relative_path) || !path.ends_with(suffix))
            return Result<std::string>::failure(
                "Module source must be a project-relative .module.lua file");
        auto name = path.substr(0, path.size() - suffix.size());
        std::ranges::replace(name, '/', '.');
        const auto resolved = module_path(name);
        if(!resolved || resolved.value() != relative_path)
            return Result<std::string>::failure(
                "Module path must use ASCII identifier segments with at most 256 name bytes");
        return Result<std::string>::success(std::move(name));
    }

    Result<std::shared_ptr<Script>, Error> Script::create(std::string source, std::string name) {
        if(source.size() > MAX_SOURCE_BYTES)
            return Result<std::shared_ptr<Script>, Error>::failure({"Script exceeds 1 MiB limit"});
        auto script = std::make_shared<Script>();
        script->m_source = std::move(source);
        script->m_name = std::move(name);
        if(auto prepared = script->prepare_definition(); !prepared)
            return Result<std::shared_ptr<Script>, Error>::failure(prepared.error());
        return Result<std::shared_ptr<Script>, Error>::success(std::move(script));
    }

    Result<void, Error> Script::prepare_definition(
        SourceSet* collecting, std::vector<std::filesystem::path>* dependencies) {
        auto instance = instantiate(collecting, dependencies);
        if(!instance)
            return Result<void, Error>::failure(instance.error());
        auto properties = instance.value()->m_impl->read_properties();
        if(!properties)
            return Result<void, Error>::failure(properties.error());
        m_properties = std::move(properties).value();
        auto events = instance.value()->m_impl->read_event_handlers();
        if(!events)
            return Result<void, Error>::failure(events.error());
        m_event_handlers = std::move(events).value();
        return Result<void, Error>::success();
    }

    Result<std::shared_ptr<Script>, Error> Script::load(const std::filesystem::path& path) {
        std::error_code error;
        const auto resolved = std::filesystem::weakly_canonical(path, error);
        if(is_module_path(path) || (!error && is_module_path(resolved)))
            return Result<std::shared_ptr<Script>, Error>::failure(
                {"Module sources cannot be attached as scripts: " + path.string()});
        auto source = read_source(path);
        if(!source)
            return Result<std::shared_ptr<Script>, Error>::failure({source.error()});
        return create(std::move(source).value(), path.string());
    }

    Result<std::vector<std::shared_ptr<Script>>, Script::LoadFailure> Script::load_group(
        const std::filesystem::path& assets_root,
        const std::span<const std::filesystem::path> relative_paths) {
        using Loaded = Result<std::vector<std::shared_ptr<Script>>, LoadFailure>;
        LoadFailure failure;
        if(relative_paths.empty() || relative_paths.size() > MAX_GROUP_ROOTS)
            return Loaded::failure({"Script group needs between 1 and 128 roots", {}});
        std::error_code error;
        auto sources = std::make_shared<SourceSet>();
        sources->root = std::filesystem::canonical(assets_root, error);
        if(error || !std::filesystem::is_directory(sources->root, error) || error)
            return Loaded::failure({"Cannot resolve project assets directory", {}});
        failure.m_sources = sources;
        std::vector<std::shared_ptr<Script>> scripts;
        const auto fail = [&](std::string message) {
            if(failure.message.empty())
                failure.message = std::move(message);
        };
        for(const auto& input : relative_paths) {
            if(!safe_source_path(input) || is_module_path(input)) {
                fail("Invalid component script path: " + input.generic_string());
                continue;
            }
            const auto path = input.lexically_normal();
            if(failure.dependencies.contains(path)) {
                fail("Duplicate script root: " + path.generic_string());
                continue;
            }
            auto& dependencies = failure.dependencies[path];
            const auto captured = sources->capture(path);
            if(!captured) {
                fail(captured.error());
                continue;
            }
            if(is_module_path(captured.value()->resolved)) {
                fail("Module sources cannot be attached as scripts: " + path.generic_string());
                continue;
            }
            auto script = std::make_shared<Script>();
            script->m_sources = sources;
            script->m_source_path = path;
            script->m_name = path.generic_string();
            const auto prepared = script->prepare_definition(sources.get(), &dependencies);
            std::ranges::sort(dependencies);
            if(!prepared) {
                fail(prepared.error().message);
                continue;
            }
            script->m_dependencies = dependencies;
            scripts.push_back(std::move(script));
        }
        if(!failure.message.empty())
            return Loaded::failure(std::move(failure));
        if(!sources->inputs_are_current()) {
            failure.message = "Script sources changed during preparation";
            return Loaded::failure(std::move(failure));
        }
        return Loaded::success(std::move(scripts));
    }

    bool Script::inputs_are_current() const {
        return !m_sources || m_sources->inputs_are_current();
    }

    bool Script::has_same_sources(const Script& other) const {
        if(m_name != other.m_name || m_source_path != other.m_source_path
            || m_dependencies != other.m_dependencies
            || static_cast<bool>(m_sources) != static_cast<bool>(other.m_sources))
            return false;
        if(!m_sources)
            return m_source == other.m_source;
        if(m_sources->files.at(m_source_path).source
            != other.m_sources->files.at(other.m_source_path).source)
            return false;
        for(const auto& path : m_dependencies)
            if(m_sources->files.at(path).source != other.m_sources->files.at(path).source)
                return false;
        return true;
    }

    bool Script::LoadFailure::inputs_are_current() const {
        return !m_sources || m_sources->inputs_are_current();
    }

    Result<void, Error> Script::validate_overrides(const ParameterMap& overrides) const {
        if(!valid_parameters(overrides))
            return Result<void, Error>::failure({"Invalid script parameter values"});
        for(const auto& [name, value] : overrides) {
            const auto found = m_properties.find(name);
            if(found == m_properties.end() || found->second.default_value.index() != value.index())
                return Result<void, Error>::failure(
                    {"Script parameter no longer matches declaration: " + name});
        }
        return Result<void, Error>::success();
    }

    void Script::retain_compatible_overrides(ParameterMap& overrides) const {
        std::erase_if(overrides, [&](const auto& value) {
            const auto property = m_properties.find(value.first);
            return property == m_properties.end()
                   || property->second.default_value.index() != value.second.index();
        });
    }

    Result<ParameterMap, Error> Script::resolve_parameters(const ParameterMap& overrides) const {
        if(auto checked = validate_overrides(overrides); !checked)
            return Result<ParameterMap, Error>::failure(checked.error());
        ParameterMap values;
        for(const auto& [name, property] : m_properties)
            values.emplace(name, property.default_value);
        for(const auto& [name, value] : overrides)
            values.at(name) = value;
        return Result<ParameterMap, Error>::success(std::move(values));
    }
}

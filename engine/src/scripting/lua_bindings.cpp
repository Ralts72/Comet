#include "scripting/lua_bindings.h"
#include "scene/scene.h"
#include "input/input_state.h"
#include "input/input_actions.h"
#include "diagnostics/logger.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace Comet::LuaBindings {
    namespace {
        constexpr char ENTITY_REFERENCE_METATABLE[] = "Comet.EntityReference";

        struct EntityReference {
            EntityUuid uuid;
            EntityId entity_id;
            std::uint64_t scene_generation;
        };

        Context& current(lua_State* state) {
            return *static_cast<Context*>(lua_touserdata(state, lua_upvalueindex(1)));
        }
        int log_message(lua_State* state) {
            if(lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TSTRING)
                return luaL_error(state, "comet.log expects exactly one string");
            auto& context = current(state);
            if(!context.can_log)
                return luaL_error(state, "Logging is only available in script callbacks");
            size_t length = 0;
            const char* message = lua_tolstring(state, 1, &length);
            lua_Debug location{};
            const char* source = "<script>";
            int line = 0;
            if(lua_getstack(state, 1, &location) && lua_getinfo(state, "Sl", &location)) {
                source = location.short_src;
                line = location.currentline;
            }
            if(length > 4096 || context.log_messages >= 16) {
                if(!context.log_overflow_reported) {
                    context.log_overflow_reported = true;
                    LOG_WARN("[Lua] {}:{}: Log output exceeds 4096 bytes per message or 16 "
                             "messages per callback; excess output is omitted",
                        source, line);
                }
                return 0;
            }
            ++context.log_messages;
            LOG_INFO("[Lua] {}:{}: {}", source, line, std::string_view(message, length));
            return 0;
        }
        const TransformComponent& transform(lua_State* state, const Entity entity) {
            if(!entity || !entity.has_component<TransformComponent>())
                luaL_error(state, "Entity is unavailable in this script phase");
            return entity.get_component<TransformComponent>();
        }
        const EntityReference& reference(lua_State* state, const int index = 1) {
            return *static_cast<EntityReference*>(
                luaL_checkudata(state, index, ENTITY_REFERENCE_METATABLE));
        }
        Entity resolve(lua_State* state, const EntityReference& reference) {
            const auto& context = current(state);
            if(!context.scene || context.scene_generation != reference.scene_generation)
                return {};
            const Entity entity = context.scene->find_entity(reference.uuid);
            return entity && entity.get_id() == reference.entity_id ? entity : Entity{};
        }
        Entity require_entity(lua_State* state, const int index = 1) {
            const Entity entity = resolve(state, reference(state, index));
            if(!entity)
                luaL_error(state, "Entity reference is stale or outside the active scene");
            return entity;
        }
        float number(lua_State* state, int index) {
            const auto value = luaL_checknumber(state, index);
            if(!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                luaL_error(state, "Expected a finite float");
            return static_cast<float>(value);
        }
        int rotate_entity(lua_State* state, const Entity entity, const int first_argument) {
            const Math::Vec3 value{number(state, first_argument), number(state, first_argument + 1),
                number(state, first_argument + 2)};
            auto target = transform(state, entity);
            if(!Math::is_finite(target.rotation + value))
                return luaL_error(state, "Rotation overflow");
            target.rotate(value);
            if(!entity.try_set_transform(target))
                return luaL_error(state, "Invalid transform");
            return 0;
        }
        int translate_entity(lua_State* state, const Entity entity, const int first_argument) {
            const Math::Vec3 value{number(state, first_argument), number(state, first_argument + 1),
                number(state, first_argument + 2)};
            auto target = transform(state, entity);
            if(!Math::is_finite(target.translation + value))
                return luaL_error(state, "Translation overflow");
            target.translation += value;
            if(!entity.try_set_transform(target))
                return luaL_error(state, "Invalid transform");
            return 0;
        }
        int push_position(lua_State* state, const Entity entity) {
            const auto value = transform(state, entity).translation;
            lua_pushnumber(state, value.x);
            lua_pushnumber(state, value.y);
            lua_pushnumber(state, value.z);
            return 3;
        }
        int rotate(lua_State* state) {
            return rotate_entity(state, current(state).entity, 1);
        }
        int translate(lua_State* state) {
            return translate_entity(state, current(state).entity, 1);
        }
        int position(lua_State* state) {
            return push_position(state, current(state).entity);
        }
        int self_entity(lua_State* state) {
            const auto& context = current(state);
            if(!context.scene || !context.entity || !context.scene->is_valid(context.entity))
                return luaL_error(state, "Entity is unavailable in this script phase");
            return push_entity_reference(state, context.entity, context.scene_generation);
        }
        int restart_scene(lua_State* state) {
            const auto& context = current(state);
            if(!context.can_request_restart || !context.scene
                || !context.scene->is_valid(context.entity) || !context.scene->request_restart())
                return luaL_error(state, "Scene restart requires an active runtime update");
            return 0;
        }
        int find_entity(lua_State* state) {
            const auto& context = current(state);
            if(!context.scene)
                return luaL_error(state, "Entity lookup requires an active scene");
            size_t length = 0;
            const char* text = luaL_checklstring(state, 1, &length);
            const auto uuid = EntityUuid::parse(std::string_view(text, length));
            if(!uuid)
                return luaL_error(state, "Expected an entity UUID");
            const Entity entity = context.scene->find_entity(*uuid);
            if(!entity) {
                lua_pushnil(state);
                return 1;
            }
            return push_entity_reference(state, entity, context.scene_generation);
        }
        Math::Vec3 read_vector3(lua_State* state, int index) {
            luaL_checktype(state, index, LUA_TTABLE);
            index = lua_absindex(state, index);
            if(luaL_len(state, index) != 3)
                luaL_error(state, "Vector needs exactly three finite numbers");
            lua_pushnil(state);
            while(lua_next(state, index)) {
                if(!lua_isinteger(state, -2) || lua_tointeger(state, -2) < 1
                    || lua_tointeger(state, -2) > 3)
                    luaL_error(state, "Vector needs exactly three finite numbers");
                lua_pop(state, 1);
            }
            // 长度与索引遵循只读参数代理；原始键检查仍拒绝普通数组的额外字段。
            Math::Vec3 vector{};
            for(int i = 0; i < 3; ++i) {
                lua_geti(state, index, i + 1);
                if(lua_type(state, -1) != LUA_TNUMBER)
                    luaL_error(state, "Vector needs exactly three finite numbers");
                vector[i] = number(state, -1);
                lua_pop(state, 1);
            }
            return vector;
        }
        void read_creation_options(lua_State* state, Scene::EntityCreation& creation) {
            if(lua_isnoneornil(state, 2))
                return;
            luaL_checktype(state, 2, LUA_TTABLE);
            if(lua_getmetatable(state, 2))
                luaL_error(state, "Entity creation options must be a plain table");
            lua_pushnil(state);
            while(lua_next(state, 2)) {
                if(lua_type(state, -2) != LUA_TSTRING)
                    luaL_error(state, "Entity creation option keys must be strings");
                size_t length = 0;
                const char* text = lua_tolstring(state, -2, &length);
                const std::string_view key(text, length);
                if(key == "translation")
                    creation.transform.translation = read_vector3(state, -1);
                else if(key == "rotation")
                    creation.transform.rotation = read_vector3(state, -1);
                else if(key == "scale")
                    creation.transform.scale = read_vector3(state, -1);
                else if(key == "mesh_source") {
                    const Entity source = require_entity(state, -1);
                    if(!source.has_component<MeshRendererComponent>())
                        luaL_error(state, "Mesh source needs a MeshRenderer");
                    const auto& renderer = source.get_component<MeshRendererComponent>();
                    if(!renderer.mesh || !renderer.material)
                        luaL_error(state, "Mesh source needs valid mesh and material handles");
                    creation.mesh_renderer = renderer;
                } else
                    luaL_error(state, "Unknown entity creation option");
                lua_pop(state, 1);
            }
        }
        int create_entity(lua_State* state) {
            auto& context = current(state);
            auto* scene = context.scene;
            if(!scene)
                return luaL_error(state, "Entity creation requires an active scene");
            size_t length = 0;
            const char* name = luaL_optlstring(state, 1, "Entity", &length);
            // Lua 参数错误会 longjmp；创建描述不得持有需要析构的资源。
            static_assert(std::is_trivially_destructible_v<Scene::EntityCreation>);
            Scene::EntityCreation creation;
            read_creation_options(state, creation);
            const auto uuid =
                scene->request_create_entity(std::string_view(name, length), creation);
            if(!uuid)
                return luaL_error(state, "Cannot queue entity creation");
            context.return_value = uuid->to_string();
            const auto& value = std::get<std::string>(*context.return_value);
            lua_pushlstring(state, value.data(), value.size());
            return 1;
        }
        int destroy_entity(lua_State* state) {
            auto* scene = current(state).scene;
            if(!scene)
                return luaL_error(state, "Entity destruction requires an active scene");
            if(!scene->request_destroy_entity(require_entity(state)))
                return luaL_error(state, "Cannot queue entity destruction");
            return 0;
        }
        int has_rigid_body(lua_State* state) {
            lua_pushboolean(state, require_entity(state).has_component<RigidBodyComponent>());
            return 1;
        }
        int remove_rigid_body(lua_State* state) {
            auto* scene = current(state).scene;
            if(!scene)
                return luaL_error(state, "Rigid body removal requires an active scene");
            if(!scene->request_remove_rigid_body(require_entity(state)))
                return luaL_error(state, "Cannot queue rigid body removal");
            return 0;
        }
        int play_one_shot(lua_State* state) {
            const auto& context = current(state);
            if(!context.scene || !context.scene->request_play_one_shot(context.entity))
                return luaL_error(state, "Current entity needs a valid Audio Source");
            return 0;
        }
        int apply_impulse(lua_State* state) {
            if(lua_gettop(state) != 3)
                return luaL_error(state, "apply_impulse requires three finite numbers");
            for(int argument = 1; argument <= 3; ++argument)
                luaL_checktype(state, argument, LUA_TNUMBER);
            const Math::Vec3 impulse{number(state, 1), number(state, 2), number(state, 3)};
            const auto& context = current(state);
            if(!context.scene || !context.scene->request_apply_impulse(context.entity, impulse))
                return luaL_error(state,
                    "Cannot queue impulse: active dynamic body with Collider required, or queue full");
            return 0;
        }
        int set_material_scalar(lua_State* state) {
            auto& context = current(state);
            if(!context.scene || !context.materials)
                return luaL_error(state, "Material parameters require an active material service");
            size_t length = 0;
            const char* name = luaL_checklstring(state, 1, &length);
            const float value = number(state, 2);
            context.return_value.reset();
            {
                const auto result = context.scene->set_material_scalar(
                    context.entity, std::string_view(name, length), value, *context.materials);
                if(!result)
                    context.return_value = result.error();
            }
            // Result 已析构；错误文本由 lua_pcall 外的 Context 持有。
            if(context.return_value)
                return luaL_error(
                    state, "%s", std::get<std::string>(*context.return_value).c_str());
            return 0;
        }
        int set_material_vector(lua_State* state) {
            auto& context = current(state);
            if(!context.scene || !context.materials)
                return luaL_error(state, "Material parameters require an active material service");
            size_t length = 0;
            const char* name = luaL_checklstring(state, 1, &length);
            const Math::Vec4 value{
                number(state, 2), number(state, 3), number(state, 4), number(state, 5)};
            context.return_value.reset();
            {
                const auto result = context.scene->set_material_vector(
                    context.entity, std::string_view(name, length), value, *context.materials);
                if(!result)
                    context.return_value = result.error();
            }
            if(context.return_value)
                return luaL_error(
                    state, "%s", std::get<std::string>(*context.return_value).c_str());
            return 0;
        }
        std::string_view session_key(lua_State* state) {
            size_t length = 0;
            const char* text = luaL_checklstring(state, 1, &length);
            const std::string_view key(text, length);
            if(!valid_parameter_name(key))
                luaL_error(state, "Expected a session key of at most 128 bytes");
            return key;
        }
        Scene& session_scene(lua_State* state) {
            auto* scene = current(state).scene;
            if(!scene)
                luaL_error(state, "Session state requires an active scene");
            return *scene;
        }
        int session_get(lua_State* state) {
            const auto key = session_key(state);
            auto& value = current(state).return_value;
            value = session_scene(state).get_session_value(key);
            if(!value) {
                lua_pushnil(state);
                return 1;
            }
            std::visit(
                [state](const auto& item) {
                    using T = std::remove_cvref_t<decltype(item)>;
                    if constexpr(std::is_same_v<T, bool>)
                        lua_pushboolean(state, item);
                    else if constexpr(std::is_same_v<T, float>)
                        lua_pushnumber(state, item);
                    else if constexpr(std::is_same_v<T, std::string>)
                        lua_pushlstring(state, item.data(), item.size());
                    else if constexpr(std::is_same_v<T, EntityUuid>)
                        luaL_error(state, "Entity references are not session values");
                    else if constexpr(std::is_same_v<T, Math::Vec4>)
                        luaL_error(state, "Four-component vectors are not session values");
                    else {
                        lua_createtable(state, 3, 0);
                        for(int i = 0; i < 3; ++i) {
                            lua_pushnumber(state, item[i]);
                            lua_rawseti(state, -2, i + 1);
                        }
                    }
                },
                *value);
            return 1;
        }
        void read_runtime_value(lua_State* state, const int index) {
            auto& value = current(state).return_value;
            switch(lua_type(state, index)) {
                case LUA_TBOOLEAN:
                    value = static_cast<bool>(lua_toboolean(state, index));
                    break;
                case LUA_TNUMBER:
                    value = number(state, index);
                    break;
                case LUA_TSTRING: {
                    size_t length = 0;
                    const char* text = lua_tolstring(state, index, &length);
                    if(length > 4096)
                        luaL_error(state, "Runtime string exceeds 4096 bytes");
                    value = std::string(text, length);
                    break;
                }
                case LUA_TTABLE:
                    value = read_vector3(state, index);
                    break;
                default:
                    luaL_error(state, "Unsupported runtime value type");
            }
        }
        int session_set(lua_State* state) {
            const auto key = session_key(state);
            auto& scene = session_scene(state);
            if(lua_isnil(state, 2)) {
                if(!scene.erase_session_value(key))
                    return luaL_error(state, "Cannot remove session value");
                return 0;
            }
            read_runtime_value(state, 2);
            auto& value = current(state).return_value;
            const bool accepted = scene.set_session_value(key, std::move(*value));
            value.reset();
            if(!accepted)
                return luaL_error(state, "Cannot set session value");
            return 0;
        }
        int emit(lua_State* state) {
            luaL_checktype(state, 1, LUA_TSTRING);
            size_t length = 0;
            const char* text = luaL_checklstring(state, 1, &length);
            const std::string_view name(text, length);
            if(!valid_parameter_name(name))
                return luaL_error(state, "Expected an event name of at most 128 bytes");
            auto& context = current(state);
            if(!context.scene)
                return luaL_error(state, "Events require an active runtime scene");
            if(lua_isnoneornil(state, 2))
                context.return_value.reset();
            else
                read_runtime_value(state, 2);
            const bool accepted = context.scene->emit_event(name, std::move(context.return_value));
            context.return_value.reset();
            if(!accepted)
                return luaL_error(
                    state, "Cannot queue event: runtime inactive or event limit reached");
            return 0;
        }
        bool record_input_context_cleanup(
            std::vector<std::string>& contexts, const std::string_view name) {
            if(!InputActions::valid_name(name))
                return false;
            if(std::ranges::find(contexts, name) != contexts.end())
                return true;
            if(contexts.size() >= InputActions::MAX_CONTEXTS)
                return false;
            contexts.emplace_back(name);
            return true;
        }
        int set_input_context(lua_State* state) {
            luaL_checktype(state, 1, LUA_TSTRING);
            luaL_checktype(state, 2, LUA_TBOOLEAN);
            size_t length = 0;
            const char* name = lua_tolstring(state, 1, &length);
            auto& context = current(state);
            const bool enabled = lua_toboolean(state, 2);
            if(context.disabled_input_contexts) {
                if(enabled)
                    return luaL_error(state, "Script cleanup can only disable input contexts");
                if(!record_input_context_cleanup(
                       *context.disabled_input_contexts, std::string_view(name, length)))
                    return luaL_error(state, "Cannot record input context cleanup");
                return 0;
            }
            auto* scene = context.scene;
            if(!scene || !scene->request_input_context(std::string_view(name, length), enabled))
                return luaL_error(state, "Cannot request input context change");
            return 0;
        }
        int reference_valid(lua_State* state) {
            lua_pushboolean(state, static_cast<bool>(resolve(state, reference(state))));
            return 1;
        }
        int reference_equal(lua_State* state) {
            const auto* right =
                static_cast<EntityReference*>(luaL_testudata(state, 2, ENTITY_REFERENCE_METATABLE));
            const Entity left = resolve(state, reference(state));
            lua_pushboolean(state, right && left && left == resolve(state, *right));
            return 1;
        }
        int reference_position(lua_State* state) {
            return push_position(state, require_entity(state));
        }
        int reference_rotate(lua_State* state) {
            return rotate_entity(state, require_entity(state), 2);
        }
        int reference_translate(lua_State* state) {
            return translate_entity(state, require_entity(state), 2);
        }
        const InputState::Action& action(lua_State* state, bool button) {
            const auto* input = current(state).input;
            const char* name = luaL_checkstring(state, 1);
            if(!input)
                luaL_error(state, "Input actions are only available during update/fixed_update");
            const auto* value = input->action(name);
            if(!value)
                luaL_error(state, "Unknown input action: %s", name);
            if(button && value->type != InputState::Action::Type::Button)
                luaL_error(state, "Expected button action: %s", name);
            return *value;
        }
        int action_value(lua_State* state) {
            lua_pushnumber(state, action(state, false).value);
            return 1;
        }
        int action_down(lua_State* state) {
            lua_pushboolean(state, action(state, true).down);
            return 1;
        }
        int action_pressed(lua_State* state) {
            lua_pushboolean(state, action(state, true).pressed);
            return 1;
        }
        int action_released(lua_State* state) {
            lua_pushboolean(state, action(state, true).released);
            return 1;
        }
    }

    int push_entity_reference(
        lua_State* state, const Entity entity, const std::uint64_t scene_generation) {
        auto* storage =
            static_cast<EntityReference*>(lua_newuserdatauv(state, sizeof(EntityReference), 0));
        std::construct_at(
            storage, EntityReference{entity.get_uuid(), entity.get_id(), scene_generation});
        luaL_getmetatable(state, ENTITY_REFERENCE_METATABLE);
        lua_setmetatable(state, -2);
        return 1;
    }

    void install(lua_State* state, Context& context) {
        luaL_newmetatable(state, ENTITY_REFERENCE_METATABLE);
        lua_pushlightuserdata(state, &context);
        lua_pushcclosure(state, reference_equal, 1);
        lua_setfield(state, -2, "__eq");
        lua_newtable(state);
        lua_pushlightuserdata(state, &context);
        const luaL_Reg entity_api[]{{"is_valid", reference_valid}, {"position", reference_position},
            {"rotate", reference_rotate}, {"translate", reference_translate}, {nullptr, nullptr}};
        luaL_setfuncs(state, entity_api, 1);
        lua_setfield(state, -2, "__index");
        lua_pop(state, 1);

        lua_newtable(state);
        lua_pushlightuserdata(state, &context);
        const luaL_Reg api[]{{"log", log_message}, {"rotate", rotate}, {"translate", translate},
            {"position", position}, {"self_entity", self_entity}, {"find_entity", find_entity},
            {"create_entity", create_entity}, {"destroy_entity", destroy_entity},
            {"has_rigid_body", has_rigid_body}, {"remove_rigid_body", remove_rigid_body},
            {"restart_scene", restart_scene}, {"play_one_shot", play_one_shot},
            {"apply_impulse", apply_impulse}, {"set_material_scalar", set_material_scalar},
            {"set_material_vector", set_material_vector}, {"session_get", session_get},
            {"session_set", session_set}, {"emit", emit}, {"set_input_context", set_input_context},
            {"action_value", action_value}, {"action_down", action_down},
            {"action_pressed", action_pressed}, {"action_released", action_released},
            {nullptr, nullptr}};
        luaL_setfuncs(state, api, 1);
        lua_setglobal(state, "comet");
    }
}

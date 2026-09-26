#include "scripting/lua_bindings.h"
#include "scene/scene.h"
#include "input/input_state.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>

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
        const TransformComponent& transform(lua_State* state, const Entity entity) {
            if(!entity || !entity.has_component<TransformComponent>())
                luaL_error(state, "Entity is unavailable in this script phase");
            return entity.get_component<TransformComponent>();
        }
        const EntityReference& reference(lua_State* state) {
            return *static_cast<EntityReference*>(
                luaL_checkudata(state, 1, ENTITY_REFERENCE_METATABLE));
        }
        Entity resolve(lua_State* state, const EntityReference& reference) {
            const auto& context = current(state);
            if(!context.scene || context.scene_generation != reference.scene_generation)
                return {};
            const Entity entity = context.scene->find_entity(reference.uuid);
            return entity && entity.get_id() == reference.entity_id ? entity : Entity{};
        }
        Entity require_entity(lua_State* state) {
            const Entity entity = resolve(state, reference(state));
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
        int create_entity(lua_State* state) {
            auto* scene = current(state).scene;
            if(!scene)
                return luaL_error(state, "Entity creation requires an active scene");
            size_t length = 0;
            const char* name = luaL_optlstring(state, 1, "Entity", &length);
            const auto uuid = scene->request_create_entity(std::string_view(name, length));
            if(!uuid)
                return luaL_error(state, "Cannot queue entity creation");
            const auto value = uuid->to_string();
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
        int reference_valid(lua_State* state) {
            lua_pushboolean(state, static_cast<bool>(resolve(state, reference(state))));
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
        int key_down(lua_State* state) {
            const auto* input = current(state).input;
            const char* name = luaL_checkstring(state, 1);
            Input::Key key = Input::Key::Unknown;
            if(name[0] >= 'A' && name[0] <= 'Z' && name[1] == '\0')
                key = static_cast<Input::Key>(static_cast<int>(Input::Key::A) + name[0] - 'A');
            else
                return luaL_error(state, "key_down currently accepts A-Z");
            lua_pushboolean(state, input && input->focused() && input->physical().key(key).down);
            return 1;
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

    int push_entity_reference(lua_State* state, const Entity entity,
        const std::uint64_t scene_generation) {
        auto* storage =
            static_cast<EntityReference*>(lua_newuserdatauv(state, sizeof(EntityReference), 0));
        std::construct_at(storage,
            EntityReference{entity.get_uuid(), entity.get_id(), scene_generation});
        luaL_getmetatable(state, ENTITY_REFERENCE_METATABLE);
        lua_setmetatable(state, -2);
        return 1;
    }

    void install(lua_State* state, Context& context) {
        luaL_newmetatable(state, ENTITY_REFERENCE_METATABLE);
        lua_newtable(state);
        lua_pushlightuserdata(state, &context);
        const luaL_Reg entity_api[]{{"is_valid", reference_valid}, {"position", reference_position},
            {"rotate", reference_rotate}, {"translate", reference_translate}, {nullptr, nullptr}};
        luaL_setfuncs(state, entity_api, 1);
        lua_setfield(state, -2, "__index");
        lua_pop(state, 1);

        lua_newtable(state);
        lua_pushlightuserdata(state, &context);
        const luaL_Reg api[]{{"rotate", rotate}, {"translate", translate}, {"position", position},
            {"self_entity", self_entity}, {"find_entity", find_entity},
            {"create_entity", create_entity}, {"destroy_entity", destroy_entity},
            {"key_down", key_down}, {"action_value", action_value}, {"action_down", action_down},
            {"action_pressed", action_pressed}, {"action_released", action_released},
            {nullptr, nullptr}};
        luaL_setfuncs(state, api, 1);
        lua_setglobal(state, "comet");
    }
}

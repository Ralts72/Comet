#include "scripting/lua_bindings.h"
#include "asset/data/game_data.h"
#include "animation/animation_controller.h"
#include "asset/registry.h"
#include "asset/script.h"
#include "scene/prefab.h"
#include "gameplay/character_controller.h"
#include "gameplay/navigation_access.h"
#include "audio/audio_commands.h"
#include "physics/physics_access.h"
#include "scene/scene.h"
#include "scene/runtime_session.h"
#include "input/input_state.h"
#include "input/input_actions.h"
#include "diagnostics/logger.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <array>
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
        const GameData& data_asset(lua_State* state) {
            if(lua_type(state, 1) != LUA_TSTRING)
                luaL_error(state, "Data asset requires an identity string");
            size_t size = 0;
            const char* text = lua_tolstring(state, 1, &size);
            const auto identity = AssetHandle::parse(std::string_view(text, size));
            if(!identity)
                luaL_error(state, "Invalid data asset identity");
            auto& context = current(state);
            context.return_data =
                context.assets ? context.assets->resolve<GameData>(*identity) : nullptr;
            if(!context.return_data)
                luaL_error(state, "Data asset is not loaded; assign a data property in the scene");
            return *context.return_data;
        }

        void push_data_value(lua_State* state, const GameDataValue& value);
        void push_data_record(lua_State* state, const GameDataValue::Object& record) {
            lua_createtable(state, 0, int(record.size()));
            for(const auto& [key, value] : record) {
                lua_pushlstring(state, key.data(), key.size());
                push_data_value(state, value);
                lua_rawset(state, -3);
            }
        }
        void push_data_value(lua_State* state, const GameDataValue& value) {
            if(!lua_checkstack(state, 4))
                luaL_error(state, "Data nesting exceeds Lua stack capacity");
            std::visit(
                [&](const auto& data) {
                    using T = std::decay_t<decltype(data)>;
                    if constexpr(std::is_same_v<T, GameDataValue::Object>)
                        push_data_record(state, data);
                    else if constexpr(std::is_same_v<T, GameDataValue::Array>) {
                        lua_createtable(state, int(data.size()), 0);
                        lua_Integer index = 1;
                        for(const auto& item : data) {
                            push_data_value(state, item);
                            lua_rawseti(state, -2, index++);
                        }
                    } else if constexpr(std::is_same_v<T, std::string>)
                        lua_pushlstring(state, data.data(), data.size());
                    else if constexpr(std::is_same_v<T, bool>)
                        lua_pushboolean(state, data);
                    else if constexpr(std::is_same_v<T, int64_t>)
                        lua_pushinteger(state, data);
                    else
                        lua_pushnumber(state, data);
                },
                value.value);
        }
        int data_get(lua_State* state) {
            if(lua_gettop(state) != 2 || lua_type(state, 2) != LUA_TSTRING)
                return luaL_error(state, "data_get expects a data asset and record id");
            const auto& data = data_asset(state);
            size_t size = 0;
            const char* id = lua_tolstring(state, 2, &size);
            const auto* record = data.find(std::string_view(id, size));
            if(record)
                push_data_record(state, *record);
            else
                lua_pushnil(state);
            return 1;
        }
        int data_ids(lua_State* state) {
            if(lua_gettop(state) != 1)
                return luaL_error(state, "data_ids expects one data asset");
            const auto& data = data_asset(state);
            lua_createtable(state, int(data.records().size()), 0);
            lua_Integer index = 1;
            for(const auto& [id, record] : data.records()) {
                lua_pushlstring(state, id.data(), id.size());
                lua_rawseti(state, -2, index++);
            }
            return 1;
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
            if(!context.can_request_scene || !context.scene || !context.session
                || !context.scene->is_valid(context.entity) || !context.session->request_restart())
                return luaL_error(state, "Scene restart requires an active runtime update");
            return 0;
        }
        int load_scene(lua_State* state) {
            if(lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TSTRING)
                return luaL_error(
                    state, "comet.load_scene expects one project-relative .scene path");
            size_t length = 0;
            const char* path = lua_tolstring(state, 1, &length);
            const auto& context = current(state);
            if(!context.can_request_scene || !context.scene || !context.session
                || !context.scene->is_valid(context.entity)
                || !context.session->request_scene(std::string_view(path, length)))
                return luaL_error(state,
                    "Cannot request scene transition in this runtime phase or with this path");
            return 0;
        }
        int load_checkpoint(lua_State* state) {
            const auto& context = current(state);
            if(lua_gettop(state) != 0 || !context.can_request_scene || !context.scene
                || !context.session || !context.scene->is_valid(context.entity)
                || !context.session->request_continue())
                return luaL_error(state,
                    "comet.load_checkpoint requires an active runtime update and no arguments");
            return 0;
        }
        int save_checkpoint(lua_State* state) {
            if(lua_gettop(state) != 2 || lua_type(state, 1) != LUA_TSTRING
                || !lua_istable(state, 2))
                return luaL_error(
                    state, "comet.save_checkpoint expects a name and an array of session keys");
            size_t length = 0;
            const char* marker = lua_tolstring(state, 1, &length);
            const auto count = lua_rawlen(state, 2);
            if(count > 128)
                return luaL_error(state, "Checkpoint has too many session keys");
            std::array<std::string_view, 128> keys;
            for(size_t i = 0; i < count; ++i) {
                lua_rawgeti(state, 2, static_cast<lua_Integer>(i + 1));
                if(lua_type(state, -1) != LUA_TSTRING)
                    return luaL_error(state, "Checkpoint keys must be a dense string array");
                size_t size = 0;
                const char* key = lua_tolstring(state, -1, &size);
                keys[i] = std::string_view(key, size);
                lua_pop(state, 1);
            }
            lua_pushnil(state);
            while(lua_next(state, 2)) {
                if(!lua_isinteger(state, -2) || lua_tointeger(state, -2) < 1
                    || static_cast<lua_Unsigned>(lua_tointeger(state, -2)) > count)
                    return luaL_error(state, "Checkpoint keys must be a dense string array");
                lua_pop(state, 1);
            }
            const auto& context = current(state);
            if(!context.can_request_scene || !context.scene || !context.session
                || !context.scene->is_valid(context.entity)
                || !context.session->request_checkpoint(
                    std::string_view(marker, length), std::span(keys.data(), count)))
                return luaL_error(state,
                    "Cannot queue checkpoint: invalid phase, name, missing or duplicate session key");
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
        int set_primary_camera(lua_State* state) {
            if(lua_gettop(state) != 1)
                return luaL_error(state, "set_primary_camera expects one camera entity");
            const auto camera = require_entity(state);
            if(!camera.has_component<CameraComponent>())
                return luaL_error(state, "Primary camera entity needs a Camera component");
            current(state).scene->each<const CameraComponent>(
                [&](Entity entity, const CameraComponent& value) {
                    const bool primary = entity == camera;
                    if(value.primary != primary)
                        entity.edit_component<CameraComponent>(
                            [primary](auto& component) { component.primary = primary; });
                });
            return 0;
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
        std::optional<EntityUuid> queue_prefab(
            Context& context, std::string_view identity, std::optional<Math::Vec3> position) {
            const auto handle = Script::asset_handle(std::string(identity));
            if(!handle || !*handle)
                return std::nullopt;
            const auto prefab = context.assets->resolve<Prefab>(*handle);
            return context.scene->request_instantiate_prefab(prefab, context.components, position);
        }

        int instantiate_prefab(lua_State* state) {
            auto& context = current(state);
            if(!context.scene || !context.scene->is_valid(context.entity) || !context.assets
                || !context.components)
                return luaL_error(state, "Prefab instantiation requires an active runtime entity");
            if(lua_gettop(state) < 1 || lua_gettop(state) > 2 || lua_type(state, 1) != LUA_TSTRING)
                return luaL_error(
                    state, "instantiate_prefab expects a prefab parameter and optional position");
            size_t length = 0;
            const char* identity = lua_tolstring(state, 1, &length);
            std::optional<Math::Vec3> position;
            if(!lua_isnoneornil(state, 2))
                position = read_vector3(state, 2);
            const auto uuid = queue_prefab(context, std::string_view(identity, length), position);
            if(!uuid)
                return luaL_error(state,
                    "Prefab is unassigned, not prepared, or exceeds the entity request budget");
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
        int emit_particles(lua_State* state) {
            const auto& context = current(state);
            if(lua_gettop(state) < 1 || lua_gettop(state) > 2)
                return luaL_error(
                    state, "comet.emit_particles expects count and an optional entity");
            const auto count = luaL_checkinteger(state, 1);
            const auto entity =
                lua_isnoneornil(state, 2) ? context.entity : require_entity(state, 2);
            if(count <= 0 || count > ParticleEmitterComponent::MAX_GPU_CAPACITY || !context.scene
                || !context.scene->request_particle_burst(entity, uint32_t(count)))
                return luaL_error(
                    state, "Cannot emit particles: invalid emitter, count or request limit");
            return 0;
        }
        int clear_particles(lua_State* state) {
            const auto& context = current(state);
            if(lua_gettop(state) > 1)
                return luaL_error(state, "comet.clear_particles expects an optional entity");
            const auto entity = lua_isnoneornil(state, 1) ? context.entity : require_entity(state);
            if(!context.scene || !context.scene->request_particle_clear(entity))
                return luaL_error(state, "Cannot clear particles for this entity");
            return 0;
        }
        int play_one_shot(lua_State* state) {
            const auto& context = current(state);
            if(lua_gettop(state) > 1)
                return luaL_error(state, "comet.play_one_shot expects an optional entity");
            const auto entity = lua_isnoneornil(state, 1) ? context.entity : require_entity(state);
            if(!context.scene || !context.scene->is_valid(entity)
                || !entity.has_component<AudioSourceComponent>())
                return luaL_error(state, "Entity needs a valid Audio Source");
            if(!context.audio)
                return luaL_error(state, "Audio service is unavailable in this runtime");
            const auto& source = entity.get_component<AudioSourceComponent>();
            const auto spatial =
                source.spatial_settings(Math::Vec3(context.scene->get_world_matrix(entity)[3]));
            if(!context.audio->request_one_shot(
                   source.clip, source.volume, source.category, spatial))
                return luaL_error(
                    state, "Cannot queue one-shot: invalid source or audio request limit reached");
            return 0;
        }
        PhysicsAccess& require_physics(lua_State* state) {
            const auto& context = current(state);
            if(!context.scene || !context.scene->is_valid(context.entity))
                luaL_error(state, "Physics access requires an active entity");
            if(!context.physics)
                luaL_error(state, "Physics service is unavailable in this runtime");
            return *context.physics;
        }

        PhysicsAccess::QueryFilter query_filter(lua_State* state, int index = 3) {
            PhysicsAccess::QueryFilter filter{.ignored = current(state).entity.get_uuid()};
            if(lua_isnoneornil(state, index))
                return filter;
            luaL_checktype(state, index, LUA_TTABLE);
            lua_pushnil(state);
            while(lua_next(state, index)) {
                if(lua_type(state, -2) != LUA_TSTRING)
                    luaL_error(state, "Query option names must be strings");
                size_t name_length = 0;
                const char* name_data = lua_tolstring(state, -2, &name_length);
                const std::string_view name(name_data, name_length);
                if(name == "ignore") {
                    if(lua_type(state, -1) == LUA_TBOOLEAN && !lua_toboolean(state, -1))
                        filter.ignored = {};
                    else
                        filter.ignored = require_entity(state, -1).get_uuid();
                } else if(name == "layers") {
                    if(!lua_isinteger(state, -1) || lua_tointeger(state, -1) < 0
                        || static_cast<lua_Unsigned>(lua_tointeger(state, -1)) > UINT32_MAX)
                        luaL_error(state, "Query layers must be an unsigned 32-bit integer");
                    filter.layers = static_cast<uint32_t>(lua_tointeger(state, -1));
                } else if(name == "include_triggers") {
                    luaL_checktype(state, -1, LUA_TBOOLEAN);
                    filter.include_triggers = lua_toboolean(state, -1);
                } else {
                    luaL_error(state, "Unknown query option: %s", name.data());
                }
                lua_pop(state, 1);
            }
            return filter;
        }

        void push_vector3(lua_State* state, Math::Vec3 value) {
            lua_createtable(state, 3, 0);
            for(int axis = 0; axis < 3; ++axis) {
                lua_pushnumber(state, value[axis]);
                lua_rawseti(state, -2, axis + 1);
            }
        }

        int push_query_hit(lua_State* state, const std::optional<PhysicsAccess::QueryHit>& hit) {
            auto& context = current(state);
            const auto entity = hit ? context.scene->find_entity(hit->entity) : Entity{};
            if(!entity) {
                lua_pushnil(state);
                return 1;
            }
            lua_createtable(state, 0, 4);
            push_entity_reference(state, entity, context.scene_generation);
            lua_setfield(state, -2, "entity");
            push_vector3(state, hit->position);
            lua_setfield(state, -2, "position");
            push_vector3(state, hit->normal);
            lua_setfield(state, -2, "normal");
            lua_pushnumber(state, hit->fraction);
            lua_setfield(state, -2, "fraction");
            return 1;
        }

        int raycast(lua_State* state) {
            if(lua_gettop(state) < 2 || lua_gettop(state) > 3)
                return luaL_error(
                    state, "raycast expects origin, displacement and optional filter");
            const auto origin = read_vector3(state, 1);
            const auto displacement = read_vector3(state, 2);
            const auto filter = query_filter(state);
            auto& physics = require_physics(state);
            auto& context = current(state);
            context.return_value.reset();
            std::optional<PhysicsAccess::QueryHit> hit;
            {
                auto result = physics.raycast(origin, displacement, filter);
                if(result)
                    hit = result.value();
                else
                    context.return_value = result.error();
            }
            if(context.return_value)
                return luaL_error(
                    state, "%s", std::get<std::string>(*context.return_value).c_str());
            return push_query_hit(state, hit);
        }

        ColliderComponent read_query_shape(lua_State* state, TransformComponent& transform) {
            luaL_checktype(state, 1, LUA_TTABLE);
            ColliderComponent shape;
            bool named = false;
            lua_pushnil(state);
            while(lua_next(state, 1)) {
                if(lua_type(state, -2) != LUA_TSTRING)
                    luaL_error(state, "Shape fields must have string names");
                size_t length = 0;
                const char* data = lua_tolstring(state, -2, &length);
                const std::string_view key(data, length);
                if(key == "shape") {
                    if(lua_type(state, -1) != LUA_TSTRING)
                        luaL_error(state, "Shape must be box, sphere or capsule");
                    const char* type_data = lua_tolstring(state, -1, &length);
                    const std::string_view type(type_data, length);
                    if(type == "box")
                        shape.shape = ColliderShape::Box;
                    else if(type == "sphere")
                        shape.shape = ColliderShape::Sphere;
                    else if(type == "capsule")
                        shape.shape = ColliderShape::Capsule;
                    else
                        luaL_error(state, "Shape must be box, sphere or capsule");
                    named = true;
                } else if(key == "half_extents") {
                    shape.half_extents = read_vector3(state, -1);
                } else if(key == "radius") {
                    shape.radius = number(state, -1);
                } else if(key == "height") {
                    shape.height = number(state, -1);
                } else if(key == "rotation") {
                    transform.rotation = read_vector3(state, -1);
                } else {
                    luaL_error(state, "Unknown shape field: %s", key.data());
                }
                lua_pop(state, 1);
            }
            if(!named)
                luaL_error(state, "Shape descriptor requires a shape name");
            return shape;
        }

        int shape_cast(lua_State* state) {
            if(lua_gettop(state) < 3 || lua_gettop(state) > 4)
                return luaL_error(
                    state, "shape_cast expects shape, origin, displacement and optional filter");
            TransformComponent transform;
            const auto shape = read_query_shape(state, transform);
            transform.translation = read_vector3(state, 2);
            const auto displacement = read_vector3(state, 3);
            const auto filter = query_filter(state, 4);
            auto& physics = require_physics(state);
            auto& context = current(state);
            context.return_value.reset();
            std::optional<PhysicsAccess::QueryHit> hit;
            {
                auto result = physics.shape_cast(shape, transform, displacement, filter);
                if(result)
                    hit = result.value();
                else
                    context.return_value = result.error();
            }
            if(context.return_value)
                return luaL_error(
                    state, "%s", std::get<std::string>(*context.return_value).c_str());
            return push_query_hit(state, hit);
        }

        int overlap_sphere(lua_State* state) {
            if(lua_gettop(state) < 2 || lua_gettop(state) > 3)
                return luaL_error(
                    state, "overlap_sphere expects center, radius and optional filter");
            const auto center = read_vector3(state, 1);
            const auto radius = number(state, 2);
            const auto filter = query_filter(state);
            auto& physics = require_physics(state);
            auto& context = current(state);
            context.return_value.reset();
            context.query_entities.clear();
            {
                auto result = physics.overlap_sphere(center, radius, filter);
                if(result)
                    context.query_entities = std::move(result).value();
                else
                    context.return_value = result.error();
            }
            if(context.return_value)
                return luaL_error(
                    state, "%s", std::get<std::string>(*context.return_value).c_str());
            lua_createtable(state, static_cast<int>(context.query_entities.size()), 0);
            int index = 1;
            for(const auto uuid : context.query_entities) {
                const auto entity = context.scene->find_entity(uuid);
                if(!entity)
                    continue;
                push_entity_reference(state, entity, context.scene_generation);
                lua_rawseti(state, -2, index++);
            }
            context.query_entities.clear();
            return 1;
        }

        template<typename Request>
        int apply_physics(lua_State* state, Request request, const char* name) {
            constexpr bool at_point =
                std::is_invocable_v<Request, PhysicsAccess&, Entity, Math::Vec3, Math::Vec3>;
            constexpr int arguments = at_point ? 6 : 3;
            if(lua_gettop(state) != arguments)
                return luaL_error(state, "%s requires %d finite numbers", name, arguments);
            for(int argument = 1; argument <= arguments; ++argument)
                luaL_checktype(state, argument, LUA_TNUMBER);
            const Math::Vec3 value{number(state, 1), number(state, 2), number(state, 3)};
            auto& physics = require_physics(state);
            bool queued;
            if constexpr(at_point) {
                const Math::Vec3 point{number(state, 4), number(state, 5), number(state, 6)};
                queued = (physics.*request)(current(state).entity, value, point);
            } else {
                queued = (physics.*request)(current(state).entity, value);
            }
            if(!queued)
                return luaL_error(state,
                    "%s requires an active dynamic body with Collider and room in the physics queue",
                    name);
            return 0;
        }
        int apply_impulse(lua_State* state) {
            return apply_physics(state, &PhysicsAccess::request_impulse, "apply_impulse");
        }
        int apply_force(lua_State* state) {
            return apply_physics(state, &PhysicsAccess::request_force, "apply_force");
        }
        int apply_torque(lua_State* state) {
            return apply_physics(state, &PhysicsAccess::request_torque, "apply_torque");
        }
        int apply_impulse_at(lua_State* state) {
            return apply_physics(state, &PhysicsAccess::request_impulse_at, "apply_impulse_at");
        }
        int apply_force_at(lua_State* state) {
            return apply_physics(state, &PhysicsAccess::request_force_at, "apply_force_at");
        }
        int push_velocity(lua_State* state, const bool angular) {
            if(lua_gettop(state) != 0)
                return luaL_error(state, "Velocity query takes no arguments");
            const auto velocity = require_physics(state).get_velocity(current(state).entity);
            if(!velocity)
                return luaL_error(state, "Velocity requires a registered body with Collider");
            const auto value = angular ? velocity->angular : velocity->linear;
            lua_pushnumber(state, value.x);
            lua_pushnumber(state, value.y);
            lua_pushnumber(state, value.z);
            return 3;
        }
        int linear_velocity(lua_State* state) {
            return push_velocity(state, false);
        }
        int angular_velocity(lua_State* state) {
            return push_velocity(state, true);
        }
        int move_character(lua_State* state) {
            const auto count = lua_gettop(state);
            if(count < 3 || count > 5)
                return luaL_error(
                    state, "move_character expects X/Z, jump, optional run/crouch booleans");
            luaL_checktype(state, 1, LUA_TNUMBER);
            luaL_checktype(state, 2, LUA_TNUMBER);
            luaL_checktype(state, 3, LUA_TBOOLEAN);
            if(count >= 4)
                luaL_checktype(state, 4, LUA_TBOOLEAN);
            if(count >= 5)
                luaL_checktype(state, 5, LUA_TBOOLEAN);
            const Math::Vec2 direction{number(state, 1), number(state, 2)};
            if(!require_physics(state).request_character_move(
                   current(state).entity, {.direction = direction,
                                              .jump = bool(lua_toboolean(state, 3)),
                                              .run = bool(lua_toboolean(state, 4)),
                                              .crouch = bool(lua_toboolean(state, 5))}))
                return luaL_error(state, "move_character needs an enabled Character Controller");
            return 0;
        }
        int face_movement(lua_State* state) {
            if(lua_gettop(state) != 3)
                return luaL_error(state, "face_movement expects a visual entity and X/Z direction");
            const auto visual = require_entity(state);
            const Math::Vec2 direction{number(state, 2), number(state, 3)};
            auto& context = current(state);
            context.return_value.reset();
            {
                auto result = Gameplay::CharacterController::face_movement(
                    *context.scene, visual.get_uuid(), direction);
                if(!result)
                    context.return_value = result.error();
            }
            if(context.return_value)
                return luaL_error(
                    state, "%s", std::get<std::string>(*context.return_value).c_str());
            return 0;
        }

        int navigate_to(lua_State* state) {
            const auto count = lua_gettop(state);
            if(count != 3 && count != 4)
                return luaL_error(
                    state, "navigate_to expects target X/Y/Z and optional run boolean");
            const Math::Vec3 target{number(state, 1), number(state, 2), number(state, 3)};
            if(count == 4)
                luaL_checktype(state, 4, LUA_TBOOLEAN);
            auto& context = current(state);
            if(!context.navigation || !context.scene)
                return luaL_error(
                    state, "navigate_to is only available in fixed_update with navigation enabled");
            auto& physics = require_physics(state);
            const char* status = nullptr;
            context.return_value.reset();
            {
                auto result = context.navigation->move_to(
                    *context.scene, context.entity, physics, target, lua_toboolean(state, 4));
                if(result)
                    status = NavigationAccess::name(result.value());
                else
                    context.return_value = result.error();
            }
            if(context.return_value) {
                const auto& error = std::get<std::string>(*context.return_value);
                lua_pushlstring(state, error.data(), error.size());
                return lua_error(state);
            }
            lua_pushstring(state, status);
            return 1;
        }
        int character_grounded(lua_State* state) {
            if(lua_gettop(state) != 0)
                return luaL_error(state, "character_grounded takes no arguments");
            const auto character =
                require_physics(state).get_character_state(current(state).entity);
            if(!character)
                return luaL_error(
                    state, "character_grounded needs a registered Character Controller");
            lua_pushboolean(state, character->grounded);
            return 1;
        }
        std::string_view character_action(
            lua_State* state, const char* field, const std::string_view default_name) {
            lua_getfield(state, 2, field);
            if(lua_isnil(state, -1)) {
                lua_pop(state, 1);
                return default_name;
            }
            luaL_checktype(state, -1, LUA_TSTRING);
            size_t length = 0;
            const char* name = lua_tolstring(state, -1, &length);
            const std::string_view value(name, length);
            if(!InputActions::valid_name(value))
                luaL_error(state, "Invalid character action name: %s", field);
            lua_pop(state, 1);
            return value;
        }
        int control_character(lua_State* state) {
            if(lua_gettop(state) > 2)
                return luaL_error(
                    state, "control_character expects an enabled boolean and options");
            bool enabled = true;
            if(lua_gettop(state) >= 1) {
                luaL_checktype(state, 1, LUA_TBOOLEAN);
                enabled = lua_toboolean(state, 1);
            }
            Gameplay::CharacterController controller;
            if(lua_gettop(state) == 2) {
                luaL_checktype(state, 2, LUA_TTABLE);
                if(lua_getmetatable(state, 2))
                    return luaL_error(state, "Character options must be a plain table");
                lua_pushnil(state);
                while(lua_next(state, 2)) {
                    luaL_checktype(state, -2, LUA_TSTRING);
                    size_t length = 0;
                    const char* name = lua_tolstring(state, -2, &length);
                    const std::string_view key(name, length);
                    if(key != "move_x" && key != "move_z" && key != "jump" && key != "run"
                        && key != "crouch" && key != "movement_reference" && key != "facing")
                        return luaL_error(state, "Unknown character option: %s", key.data());
                    lua_pop(state, 1);
                }
                controller.move_x = character_action(state, "move_x", controller.move_x);
                controller.move_z = character_action(state, "move_z", controller.move_z);
                controller.jump = character_action(state, "jump", controller.jump);
                controller.run = character_action(state, "run", controller.run);
                controller.crouch = character_action(state, "crouch", controller.crouch);
                for(const auto& [field, target] :
                    {std::pair{"movement_reference", &controller.movement_reference},
                        std::pair{"facing", &controller.facing}}) {
                    lua_getfield(state, 2, field);
                    if(!lua_isnil(state, -1))
                        *target = require_entity(state, -1).get_uuid();
                    lua_pop(state, 1);
                }
            }
            auto& context = current(state);
            if(!context.input)
                return luaL_error(
                    state, "Character control requires runtime input during an update");
            auto& physics = require_physics(state);
            bool jumping = false;
            context.return_value.reset();
            {
                const auto result = controller.update(
                    *context.scene, context.entity, *context.input, physics, enabled);
                if(result)
                    jumping = result.value();
                else
                    context.return_value = result.error();
            }
            if(context.return_value) {
                const auto& error = std::get<std::string>(*context.return_value);
                lua_pushlstring(state, error.data(), error.size());
                return lua_error(state);
            }
            lua_pushboolean(state, jumping);
            return 1;
        }
        int character_state(lua_State* state) {
            if(lua_gettop(state) != 0)
                return luaL_error(state, "character_state takes no arguments");
            auto& physics = require_physics(state);
            auto& context = current(state);
            Gameplay::CharacterController::State value;
            bool available = false;
            {
                const auto result =
                    Gameplay::CharacterController::get_state(context.entity, physics);
                if(result) {
                    value = result.value();
                    available = true;
                }
            }
            if(!available) {
                lua_pushnil(state);
                return 1;
            }
            lua_createtable(state, 0, 6);
            const auto name = value.name();
            lua_pushlstring(state, name.data(), name.size());
            lua_setfield(state, -2, "motion");
            lua_pushboolean(state, value.grounded);
            lua_setfield(state, -2, "grounded");
            lua_pushboolean(state, value.crouched);
            lua_setfield(state, -2, "crouched");
            lua_pushnumber(state, value.speed);
            lua_setfield(state, -2, "speed");
            lua_pushnumber(state, value.vertical_speed);
            lua_setfield(state, -2, "vertical_speed");
            lua_pushnumber(state, value.height_ratio);
            lua_setfield(state, -2, "height_ratio");
            return 1;
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
        int set_animation_parameter(lua_State* state) {
            if(lua_gettop(state) != 3 || lua_type(state, 2) != LUA_TSTRING)
                return luaL_error(
                    state, "set_animation_parameter expects entity, name, boolean or number");
            const auto entity = require_entity(state);
            size_t length = 0;
            const char* key = lua_tolstring(state, 2, &length);
            if(!entity.has_component<AnimatorComponent>() || !valid_parameter_name({key, length}))
                return luaL_error(state, "Animation parameter requires an Animator and valid name");
            // 所有 Lua 参数先检查完，再创建 C++ 自有值，避免 longjmp 越过析构。
            bool boolean = false;
            float numeric = 0;
            const bool is_boolean = lua_type(state, 3) == LUA_TBOOLEAN;
            if(is_boolean)
                boolean = lua_toboolean(state, 3);
            else if(lua_type(state, 3) == LUA_TNUMBER)
                numeric = number(state, 3);
            else
                return luaL_error(state, "Animation parameter must be a boolean or finite number");
            auto& context = current(state);
            context.return_value.reset();
            {
                const auto& animator = entity.get_component<AnimatorComponent>();
                const auto controller =
                    context.assets
                        ? context.assets->resolve<AnimationController>(animator.controller)
                        : nullptr;
                const std::string name(key, length);
                const ParameterValue value =
                    is_boolean ? ParameterValue(boolean) : ParameterValue(numeric);
                if(!controller)
                    context.return_value = std::string("Animation controller is not loaded");
                else if(const auto found = controller->parameters.find(name);
                    found == controller->parameters.end() || found->second.index() != value.index())
                    context.return_value =
                        std::string("Unknown or mistyped animation parameter: ") + name;
                else {
                    const auto previous = animator.parameters.find(name);
                    if(previous == animator.parameters.end() || previous->second != value)
                        entity.edit_component<AnimatorComponent>(
                            [&](auto& target) { target.parameters[name] = value; });
                }
            }
            if(context.return_value)
                return luaL_error(
                    state, "%s", std::get<std::string>(*context.return_value).c_str());
            return 0;
        }
        int get_animation_parameter(lua_State* state) {
            if(lua_gettop(state) != 2 || lua_type(state, 2) != LUA_TSTRING)
                return luaL_error(state, "get_animation_parameter expects entity and name");
            const auto entity = require_entity(state);
            if(!entity.has_component<AnimatorComponent>())
                return luaL_error(state, "Animation parameter requires an Animator");
            size_t length = 0;
            const char* key = lua_tolstring(state, 2, &length);
            if(!valid_parameter_name({key, length}))
                return luaL_error(state, "Animation parameter requires a valid name");
            auto& context = current(state);
            bool found = false;
            {
                const auto& animator = entity.get_component<AnimatorComponent>();
                const auto controller =
                    context.assets
                        ? context.assets->resolve<AnimationController>(animator.controller)
                        : nullptr;
                if(controller) {
                    if(const auto* value = controller->parameter(key, animator.parameters)) {
                        if(const auto* flag = std::get_if<bool>(value)) {
                            lua_pushboolean(state, *flag);
                            found = true;
                        } else if(const auto* number = std::get_if<float>(value)) {
                            lua_pushnumber(state, *number);
                            found = true;
                        }
                    }
                }
            }
            if(!found)
                return luaL_error(
                    state, "Unknown animation parameter or unloaded controller: %s", key);
            return 1;
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
        RuntimeSession& runtime_session(lua_State* state) {
            auto* session = current(state).session;
            if(!session || !session->is_active())
                luaL_error(state, "Session state requires an active runtime");
            return *session;
        }
        int checkpoint(lua_State* state) {
            if(lua_gettop(state) != 0)
                return luaL_error(state, "comet.checkpoint expects no arguments");
            const auto marker = runtime_session(state).checkpoint();
            if(marker.empty())
                lua_pushnil(state);
            else
                lua_pushlstring(state, marker.data(), marker.size());
            return 1;
        }
        int session_get(lua_State* state) {
            const auto key = session_key(state);
            auto& value = current(state).return_value;
            value = runtime_session(state).get_value(key);
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
                    else
                        push_vector3(state, item);
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
            auto& session = runtime_session(state);
            if(lua_isnil(state, 2)) {
                if(!session.erase_value(key))
                    return luaL_error(state, "Cannot remove session value");
                return 0;
            }
            read_runtime_value(state, 2);
            auto& value = current(state).return_value;
            const bool accepted = session.set_value(key, std::move(*value));
            value.reset();
            if(!accepted)
                return luaL_error(state, "Cannot set session value");
            return 0;
        }
        int emit(lua_State* state) {
            if(lua_gettop(state) < 1 || lua_gettop(state) > 3)
                return luaL_error(state, "emit expects a name, optional value and target entity");
            luaL_checktype(state, 1, LUA_TSTRING);
            size_t length = 0;
            const char* text = luaL_checklstring(state, 1, &length);
            const std::string_view name(text, length);
            if(!valid_parameter_name(name))
                return luaL_error(state, "Expected an event name of at most 128 bytes");
            auto& context = current(state);
            if(!context.scene)
                return luaL_error(state, "Events require an active runtime scene");
            const auto target = lua_isnoneornil(state, 3) ? Entity{} : require_entity(state, 3);
            if(lua_isnoneornil(state, 2))
                context.return_value.reset();
            else
                read_runtime_value(state, 2);
            const bool accepted =
                context.scene->emit_event(name, std::move(context.return_value), target);
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
            auto* session = context.session;
            if(!session || !session->request_input_context(std::string_view(name, length), enabled))
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
        int reference_transform_point(lua_State* state) {
            if(lua_gettop(state) != 4)
                return luaL_error(state, "transform_point expects three local coordinates");
            const auto entity = require_entity(state);
            const Math::Vec3 local{number(state, 2), number(state, 3), number(state, 4)};
            const auto world =
                Math::Vec3(current(state).scene->get_world_matrix(entity) * Math::Vec4(local, 1));
            if(!Math::is_finite(world))
                return luaL_error(state, "Transformed point is not finite");
            lua_pushnumber(state, world.x);
            lua_pushnumber(state, world.y);
            lua_pushnumber(state, world.z);
            return 3;
        }
        int reference_rotate(lua_State* state) {
            return rotate_entity(state, require_entity(state), 2);
        }
        int reference_translate(lua_State* state) {
            return translate_entity(state, require_entity(state), 2);
        }
        int reference_scale(lua_State* state) {
            const auto& scale = transform(state, require_entity(state)).scale;
            lua_pushnumber(state, scale.x);
            lua_pushnumber(state, scale.y);
            lua_pushnumber(state, scale.z);
            return 3;
        }
        int set_transform_vector(
            lua_State* state, const Entity entity, Math::Vec3 TransformComponent::* field) {
            if(lua_gettop(state) != 4)
                return luaL_error(state, "Entity transform setter expects three numbers");
            auto target = transform(state, entity);
            target.*field = {number(state, 2), number(state, 3), number(state, 4)};
            if(!entity.try_set_transform(target))
                return luaL_error(state, "Invalid entity transform");
            return 0;
        }
        int reference_set_scale(lua_State* state) {
            return set_transform_vector(state, require_entity(state), &TransformComponent::scale);
        }
        int reference_set_position(lua_State* state) {
            return set_transform_vector(
                state, require_entity(state), &TransformComponent::translation);
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
            {"transform_point", reference_transform_point}, {"rotate", reference_rotate},
            {"translate", reference_translate}, {"scale", reference_scale},
            {"set_scale", reference_set_scale}, {"set_position", reference_set_position},
            {nullptr, nullptr}};
        luaL_setfuncs(state, entity_api, 1);
        lua_setfield(state, -2, "__index");
        lua_pop(state, 1);

        lua_newtable(state);
        lua_pushlightuserdata(state, &context);
        const luaL_Reg api[]{{"log", log_message}, {"rotate", rotate}, {"translate", translate},
            {"set_primary_camera", set_primary_camera}, {"position", position},
            {"self_entity", self_entity}, {"find_entity", find_entity}, {"data_get", data_get},
            {"data_ids", data_ids}, {"create_entity", create_entity},
            {"instantiate_prefab", instantiate_prefab}, {"destroy_entity", destroy_entity},
            {"has_rigid_body", has_rigid_body}, {"remove_rigid_body", remove_rigid_body},
            {"restart_scene", restart_scene}, {"load_scene", load_scene},
            {"save_checkpoint", save_checkpoint}, {"load_checkpoint", load_checkpoint},
            {"checkpoint", checkpoint}, {"play_one_shot", play_one_shot},
            {"emit_particles", emit_particles}, {"clear_particles", clear_particles},
            {"raycast", raycast}, {"shape_cast", shape_cast},
            {"get_animation_parameter", get_animation_parameter},
            {"overlap_sphere", overlap_sphere}, {"apply_impulse", apply_impulse},
            {"apply_force", apply_force}, {"apply_impulse_at", apply_impulse_at},
            {"apply_force_at", apply_force_at}, {"apply_torque", apply_torque},
            {"linear_velocity", linear_velocity}, {"angular_velocity", angular_velocity},
            {"set_animation_parameter", set_animation_parameter},
            {"set_material_scalar", set_material_scalar}, {"move_character", move_character},
            {"navigate_to", navigate_to}, {"face_movement", face_movement},
            {"character_grounded", character_grounded}, {"character_state", character_state},
            {"control_character", control_character}, {"set_material_vector", set_material_vector},
            {"session_get", session_get}, {"session_set", session_set}, {"emit", emit},
            {"set_input_context", set_input_context}, {"action_value", action_value},
            {"action_down", action_down}, {"action_pressed", action_pressed},
            {"action_released", action_released}, {nullptr, nullptr}};
        luaL_setfuncs(state, api, 1);
        lua_setglobal(state, "comet");
    }
}

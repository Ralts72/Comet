#include "scene/systems/script_system.h"
#include "scene/script_component.h"
#include "scene/scene.h"
#include "asset/registry.h"
#include "diagnostics/logger.h"
#include <algorithm>

namespace Comet {
    ScriptSystem::~ScriptSystem() {
        stop_all();
    }

    bool ScriptSystem::is_live(const Key& key, const Entry& entry) const {
        if(!entry.entity || !entry.entity.has_component<ScriptComponent>())
            return false;
        const auto& component = entry.entity.get_component<ScriptComponent>();
        return entry.entity.get_uuid() == key.entity && component.lifetime() == key.lifetime
               && component.asset == key.asset;
    }

    void ScriptSystem::stop_entry(const Key& key, Entry& entry) noexcept {
        if(auto stopped = entry.instance->invoke(Script::Phase::Stop, {}, entry.parameters);
            !stopped)
            LOG_ERROR("Script cleanup failed: {}", stopped.error().message);
        if(entry.entity && entry.entity.has_component<ScriptComponent>()
            && entry.entity.get_component<ScriptComponent>().lifetime() == key.lifetime)
            entry.entity.get_component<ScriptComponent>().m_running_script.reset();
    }

    void ScriptSystem::stop_all() noexcept {
        for(auto it = m_start_order.rbegin(); it != m_start_order.rend(); ++it) {
            const auto found = m_entries.find(*it);
            stop_entry(found->first, found->second);
            m_entries.erase(found);
        }
        m_start_order.clear();
        m_scene = nullptr;
    }

    Result<void, Error> ScriptSystem::invoke(const Key& key, Entry& entry, Script::Phase phase,
        const Context* context, const Entity contact_other, const std::string_view event_handler,
        const ParameterValue* event_value) {
        const auto& overrides = entry.entity.get_component<ScriptComponent>().parameters;
        if(!entry.overrides || *entry.overrides != overrides) {
            auto parameters = entry.script->resolve_parameters(overrides);
            if(!parameters)
                return Result<void, Error>::failure(
                    {key.entity.to_string() + ": " + parameters.error().message});
            entry.parameters = std::move(parameters).value();
            entry.overrides = overrides;
        }
        Script::Invocation invocation;
        invocation.scene = m_scene;
        invocation.contact_other = contact_other;
        invocation.materials = m_materials;
        invocation.event_handler = event_handler;
        invocation.event_value = event_value;
        if(context) {
            invocation.delta_time = context->delta_time;
            invocation.input = &context->input;
        }
        auto result = entry.instance->invoke(phase, entry.entity, entry.parameters, invocation);
        if(!result)
            return Result<void, Error>::failure(
                {key.entity.to_string() + ": " + result.error().message});
        return result;
    }

    Result<void, Error> ScriptSystem::synchronize(Scene& scene) {
        for(auto it = m_start_order.rbegin(); it != m_start_order.rend(); ++it) {
            auto found = m_entries.find(*it);
            if(!is_live(found->first, found->second)) {
                stop_entry(found->first, found->second);
                m_entries.erase(found);
            }
        }
        std::erase_if(m_start_order, [this](const Key& key) { return !m_entries.contains(key); });
        std::map<Key, Entity> pending;
        scene.each<const ScriptComponent>([&](Entity entity, const ScriptComponent& component) {
            if(!component.asset)
                return;
            const Key key{entity.get_uuid(), component.lifetime(), component.asset};
            if(!m_entries.contains(key))
                pending.emplace(key, entity);
        });
        for(const auto& [key, entity] : pending) {
            auto script = m_assets.resolve<Script>(key.asset);
            if(!script)
                return Result<void, Error>::failure(
                    {"Script asset is unavailable: " + std::to_string(key.asset.value())});
            auto instance = script->instantiate();
            if(!instance)
                return Result<void, Error>::failure(instance.error());
            auto& entry = m_entries
                              .emplace(key, Entry{entity, std::move(script),
                                                std::move(instance).value(), {}, {}})
                              .first->second;
            m_start_order.push_back(key);
            entry.entity.get_component<ScriptComponent>().m_running_script = entry.script;
            if(auto started = invoke(key, entry, Script::Phase::Start); !started)
                return started;
        }
        return Result<void, Error>::success();
    }

    Result<void, Error> ScriptSystem::on_start(Scene& scene) {
        m_scene = &scene;
        return synchronize(scene);
    }
    Result<void, Error> ScriptSystem::dispatch(
        Scene& scene, const Context& context, Script::Phase phase) {
        if(auto synced = synchronize(scene); !synced)
            return synced;
        // 结构请求在阶段结束后提交，synchronize 已保证本轮实例有效。
        for(auto& [key, entry] : m_entries)
            if(auto result = invoke(key, entry, phase, &context); !result)
                return result;
        return Result<void, Error>::success();
    }
    Result<void, Error> ScriptSystem::fixed_update(Scene& scene, const Context& context) {
        return dispatch(scene, context, Script::Phase::FixedUpdate);
    }
    Result<void, Error> ScriptSystem::dispatch_contacts(Scene& scene, const Context& context) {
        for(const auto& event : scene.get_contact_events()) {
            if(!scene.is_valid(event.first) || !scene.is_valid(event.second))
                continue;
            Script::Phase phase;
            switch(event.kind) {
                case Scene::ContactEvent::Kind::CollisionEnter:
                    phase = Script::Phase::CollisionEnter;
                    break;
                case Scene::ContactEvent::Kind::CollisionExit:
                    phase = Script::Phase::CollisionExit;
                    break;
                case Scene::ContactEvent::Kind::TriggerEnter:
                    phase = Script::Phase::TriggerEnter;
                    break;
                case Scene::ContactEvent::Kind::TriggerExit:
                    phase = Script::Phase::TriggerExit;
                    break;
            }
            for(const auto& [self, other] :
                {std::pair{event.first, event.second}, std::pair{event.second, event.first}}) {
                if(!self.has_component<ScriptComponent>())
                    continue;
                const auto& component = self.get_component<ScriptComponent>();
                const Key key{self.get_uuid(), component.lifetime(), component.asset};
                const auto found = m_entries.find(key);
                if(found == m_entries.end() || !is_live(key, found->second))
                    continue;
                if(auto result = invoke(key, found->second, phase, &context, other); !result)
                    return result;
            }
        }
        return Result<void, Error>::success();
    }
    Result<void, Error> ScriptSystem::dispatch_events(Scene& scene, const Context& context) {
        const auto events = scene.take_events();
        for(const auto& event : events) {
            for(auto& [key, entry] : m_entries) {
                const auto& handlers = entry.script->event_handlers();
                const auto handler = handlers.find(event.name);
                if(handler == handlers.end())
                    continue;
                const auto* value = event.value ? &*event.value : nullptr;
                if(auto delivered = invoke(
                       key, entry, Script::Phase::Event, &context, {}, handler->second, value);
                    !delivered)
                    return delivered;
            }
        }
        return Result<void, Error>::success();
    }
    Result<void, Error> ScriptSystem::update(Scene& scene, const Context& context) {
        if(auto updated = dispatch(scene, context, Script::Phase::Update); !updated)
            return updated;
        if(auto contacted = dispatch_contacts(scene, context); !contacted)
            return contacted;
        return dispatch_events(scene, context);
    }
    void ScriptSystem::on_stop(Scene&) noexcept {
        stop_all();
    }
}

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

    Result<ScriptSystem::Entry, Error> ScriptSystem::prepare_entry(
        const Entity entity, std::shared_ptr<const Script> script, ParameterMap overrides) const {
        auto parameters = script->resolve_parameters(overrides);
        if(!parameters)
            return Result<Entry, Error>::failure(parameters.error());
        auto instance = script->instantiate();
        if(!instance)
            return Result<Entry, Error>::failure(instance.error());
        return Result<Entry, Error>::success({entity, std::move(script),
            std::move(instance).value(), std::move(parameters).value(), std::move(overrides), {}});
    }

    Result<void, Error> ScriptSystem::reload_changed_scripts() {
        std::map<AssetHandle, std::shared_ptr<const Script>> changed;
        for(const auto& [key, entry] : m_entries) {
            auto script = m_assets.resolve<Script>(key.asset);
            if(script && script != entry.script && script != entry.failed_reload.lock())
                changed.try_emplace(key.asset, std::move(script));
        }
        for(const auto& [handle, script] : changed) {
            std::map<Key, Entry> prepared;
            std::optional<Error> failure;
            for(const auto& [key, entry] : m_entries) {
                if(key.asset != handle || entry.script == script)
                    continue;
                auto overrides = entry.entity.get_component<ScriptComponent>().parameters;
                std::erase_if(overrides, [&](const auto& value) {
                    const auto property = script->properties().find(value.first);
                    return property == script->properties().end()
                           || property->second.default_value.index() != value.second.index();
                });
                auto candidate = prepare_entry(entry.entity, script, std::move(overrides));
                if(!candidate) {
                    failure = candidate.error();
                    break;
                }
                prepared.emplace(key, std::move(candidate).value());
            }
            if(failure) {
                for(auto& [key, entry] : m_entries)
                    if(key.asset == handle)
                        entry.failed_reload = script;
                LOG_WARN("Cannot reload script asset {}: {}", handle.value(), failure->message);
                continue;
            }

            // 同资产的候选全部可用后才结束旧实例；on_start 的场景副作用不承诺回滚。
            for(auto it = m_start_order.rbegin(); it != m_start_order.rend(); ++it) {
                if(!prepared.contains(*it))
                    continue;
                auto previous = m_entries.find(*it);
                stop_entry(previous->first, previous->second);
                m_entries.erase(previous);
            }
            std::erase_if(m_start_order, [&](const Key& key) { return prepared.contains(key); });
            for(auto& [key, candidate] : prepared) {
                auto& entry = m_entries.emplace(key, std::move(candidate)).first->second;
                auto& component = entry.entity.get_component<ScriptComponent>();
                component.parameters = *entry.overrides;
                component.m_running_script = script;
                m_start_order.push_back(key);
                if(auto started = invoke(key, entry, Script::Phase::Start); !started)
                    return started;
            }
            LOG_INFO("Reloaded script asset {} ({} instances)", handle.value(), prepared.size());
        }
        return Result<void, Error>::success();
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
        if(auto reloaded = reload_changed_scripts(); !reloaded)
            return reloaded;
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
            auto prepared = prepare_entry(
                entity, std::move(script), entity.get_component<ScriptComponent>().parameters);
            if(!prepared)
                return Result<void, Error>::failure(
                    {key.entity.to_string() + ": " + prepared.error().message});
            auto& entry = m_entries.emplace(key, std::move(prepared).value()).first->second;
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

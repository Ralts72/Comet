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

    Result<void, Error> ScriptSystem::invoke(
        const Key& key, Entry& entry, Script::Phase phase, const Context* context) {
        const auto& overrides = entry.entity.get_component<ScriptComponent>().parameters;
        if(!entry.overrides || *entry.overrides != overrides) {
            auto parameters = entry.script->parameters(overrides);
            if(!parameters)
                return Result<void, Error>::failure(
                    {key.entity.to_string() + ": " + parameters.error().message});
            entry.parameters = std::move(parameters).value();
            entry.overrides = overrides;
        }
        Script::Invocation invocation;
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
            auto& entry = m_entries.emplace(key, Entry{entity, std::move(script),
                std::move(instance).value(), {}, {}}).first->second;
            m_start_order.push_back(key);
            entry.entity.get_component<ScriptComponent>().m_running_script = entry.script;
            if(auto started = invoke(key, entry, Script::Phase::Start); !started)
                return started;
        }
        return Result<void, Error>::success();
    }

    Result<void, Error> ScriptSystem::on_start(Scene& scene) {
        if(m_scene)
            return Result<void, Error>::failure({"Scripts are already running"});
        m_scene = &scene;
        return synchronize(scene);
    }
    Result<void, Error> ScriptSystem::dispatch(
        Scene& scene, const Context& context, Script::Phase phase) {
        if(m_scene != &scene)
            return Result<void, Error>::failure({"Scripts require their active scene"});
        if(auto synced = synchronize(scene); !synced)
            return synced;
        for(auto& [key, entry] : m_entries)
            if(is_live(key, entry))
                if(auto result = invoke(key, entry, phase, &context); !result)
                    return result;
        return Result<void, Error>::success();
    }
    Result<void, Error> ScriptSystem::fixed_update(Scene& scene, const Context& context) {
        return dispatch(scene, context, Script::Phase::FixedUpdate);
    }
    Result<void, Error> ScriptSystem::update(Scene& scene, const Context& context) {
        return dispatch(scene, context, Script::Phase::Update);
    }
    void ScriptSystem::on_stop(Scene&) noexcept {
        stop_all();
    }
}

#include "scene/systems/script_system.h"
#include "scene/script_component.h"
#include "scene/scene.h"
#include "asset/registry.h"
#include "diagnostics/logger.h"
#include "scene/runtime_session.h"

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

    void ScriptSystem::stop_entry(const Key& key, Entry& entry, const StopReason reason) noexcept {
        std::vector<std::string> disabled_contexts;
        if(auto stopped = entry.instance->invoke(Script::Phase::Stop, {}, entry.parameters,
               {.disabled_input_contexts = &disabled_contexts});
            !stopped)
            LOG_ERROR("Script cleanup failed: {}", stopped.error().message);
        if(reason == StopReason::LiveChange && m_session) {
            for(const auto& name : disabled_contexts)
                if(!m_session->request_input_context(name, false))
                    LOG_ERROR("Cannot release input context '{}' during script cleanup", name);
        }
        if(entry.entity && entry.entity.has_component<ScriptComponent>()
            && entry.entity.get_component<ScriptComponent>().lifetime() == key.lifetime)
            entry.entity.get_component<ScriptComponent>().m_running_script.reset();
    }

    void ScriptSystem::stop_all() noexcept {
        for(auto it = m_start_order.rbegin(); it != m_start_order.rend(); ++it) {
            const auto found = m_entries.find(*it);
            stop_entry(found->first, found->second, StopReason::Shutdown);
            m_entries.erase(found);
        }
        m_start_order.clear();
        m_failed_reloads.clear();
        m_scene = nullptr;
        m_session = nullptr;
        m_audio = nullptr;
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
        invocation.session = m_session;
        invocation.audio = m_audio;
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
            std::move(instance).value(), std::move(parameters).value(), std::move(overrides)});
    }

    std::vector<ScriptSystem::ReloadGroup> ScriptSystem::reload_groups(
        const std::map<Key, Entity>& pending) const {
        struct Version {
            std::shared_ptr<const Script> current;
            std::set<std::filesystem::path> dependencies;
            bool changed = false;
        };
        std::map<AssetHandle, Version> versions;
        for(const auto& [key, entry] : m_entries) {
            auto& version = versions[key.asset];
            version.dependencies.insert(
                entry.script->dependencies().begin(), entry.script->dependencies().end());
            version.current = m_assets.resolve<Script>(key.asset);
            if(version.current != entry.script) {
                version.changed = true;
            }
        }
        for(const auto& [key, entity] : pending)
            versions[key.asset].current = m_assets.resolve<Script>(key.asset);
        for(auto& [handle, version] : versions) {
            if(version.current)
                version.dependencies.insert(
                    version.current->dependencies().begin(), version.current->dependencies().end());
        }

        std::set<AssetHandle> visited;
        std::vector<ReloadGroup> groups;
        for(const auto& [seed, version] : versions) {
            if(!version.changed || visited.contains(seed))
                continue;
            ReloadGroup group;
            group.scripts.emplace(seed, version.current);
            std::set<std::filesystem::path> dependencies;
            bool expanded = true;
            while(expanded) {
                expanded = false;
                for(const auto& [handle, script] : group.scripts) {
                    const auto& member = versions.at(handle);
                    dependencies.insert(member.dependencies.begin(), member.dependencies.end());
                }
                for(const auto& [handle, member] : versions) {
                    if(group.scripts.contains(handle))
                        continue;
                    const bool shares_dependency = std::ranges::any_of(member.dependencies,
                        [&](const auto& path) { return dependencies.contains(path); });
                    if(!shares_dependency)
                        continue;
                    group.scripts.emplace(handle, member.current);
                    expanded = true;
                }
            }
            for(const auto& [handle, script] : group.scripts)
                visited.insert(handle);
            for(const auto& [key, entry] : m_entries)
                if(group.scripts.contains(key.asset))
                    group.instances.emplace(key, entry.entity);
            for(const auto& [key, entity] : pending)
                if(group.scripts.contains(key.asset))
                    group.instances.emplace(key, entity);
            groups.push_back(std::move(group));
        }
        return groups;
    }

    bool ScriptSystem::matches_failed_reload(
        const ReloadGroup& group, const FailedReload& failed) const {
        if(group.instances.size() != failed.size())
            return false;
        for(const auto& [key, entity] : group.instances) {
            const auto found = failed.find(key);
            if(found == failed.end())
                return false;
            const auto& previous = found->second;
            if(previous.script.lock() != group.scripts.at(key.asset))
                return false;
            if(previous.overrides != entity.get_component<ScriptComponent>().parameters)
                return false;
        }
        return true;
    }

    Result<std::set<AssetHandle>, Error> ScriptSystem::reload_changed_scripts(
        const std::map<Key, Entity>& pending) {
        std::set<AssetHandle> blocked;
        const bool changed = std::ranges::any_of(m_entries, [&](const auto& value) {
            const auto script = m_assets.resolve<Script>(value.first.asset);
            return script != value.second.script;
        });
        if(!changed) {
            m_failed_reloads.clear();
            return Result<std::set<AssetHandle>, Error>::success({});
        }
        std::vector<FailedReload> rejected;
        for(const auto& group : reload_groups(pending)) {
            const auto previous_failure = std::ranges::find_if(m_failed_reloads,
                [&](const FailedReload& failed) { return matches_failed_reload(group, failed); });
            if(previous_failure != m_failed_reloads.end()) {
                for(const auto& [handle, script] : group.scripts)
                    blocked.insert(handle);
                rejected.push_back(std::move(*previous_failure));
                continue;
            }
            std::map<Key, Entry> prepared;
            std::optional<Error> failure;
            for(const auto& [key, entity] : group.instances) {
                const auto& script = group.scripts.at(key.asset);
                if(!script) {
                    failure = Error{"Related script asset is unavailable: "
                                    + std::to_string(key.asset.value())};
                    break;
                }
                const auto existing = m_entries.find(key);
                if(existing != m_entries.end() && existing->second.script == script)
                    continue;
                auto overrides = entity.get_component<ScriptComponent>().parameters;
                if(existing != m_entries.end())
                    script->retain_compatible_overrides(overrides);
                auto candidate = prepare_entry(entity, script, std::move(overrides));
                if(!candidate) {
                    failure = candidate.error();
                    break;
                }
                prepared.emplace(key, std::move(candidate).value());
            }
            if(failure) {
                FailedReload failed;
                for(const auto& [key, entity] : group.instances)
                    failed.emplace(key, FailedInstance{group.scripts.at(key.asset),
                                            entity.get_component<ScriptComponent>().parameters});
                rejected.push_back(std::move(failed));
                for(const auto& [handle, script] : group.scripts)
                    blocked.insert(handle);
                LOG_WARN("Cannot reload script group: {}", failure->message);
                continue;
            }
            if(auto installed = install_reload(prepared); !installed)
                return Result<std::set<AssetHandle>, Error>::failure(installed.error());
            LOG_INFO("Reloaded script group ({} assets, {} instances)", group.scripts.size(),
                prepared.size());
        }
        m_failed_reloads = std::move(rejected);
        return Result<std::set<AssetHandle>, Error>::success(std::move(blocked));
    }

    Result<void, Error> ScriptSystem::install_reload(std::map<Key, Entry>& prepared) {
        // 关联组全部准备成功才结束旧实例；on_start 的场景副作用不承诺回滚。
        for(auto it = m_start_order.rbegin(); it != m_start_order.rend(); ++it) {
            if(!prepared.contains(*it))
                continue;
            auto previous = m_entries.find(*it);
            stop_entry(previous->first, previous->second, StopReason::LiveChange);
            m_entries.erase(previous);
        }
        std::erase_if(m_start_order, [&](const Key& key) { return prepared.contains(key); });
        for(auto& [key, candidate] : prepared) {
            auto& entry = m_entries.emplace(key, std::move(candidate)).first->second;
            auto& component = entry.entity.get_component<ScriptComponent>();
            component.parameters = *entry.overrides;
            component.m_running_script = entry.script;
            m_start_order.push_back(key);
            if(auto started = invoke(key, entry, Script::Phase::Start); !started)
                return started;
        }
        return Result<void, Error>::success();
    }

    Result<void, Error> ScriptSystem::synchronize(Scene& scene) {
        for(auto it = m_start_order.rbegin(); it != m_start_order.rend(); ++it) {
            auto found = m_entries.find(*it);
            if(is_live(found->first, found->second))
                continue;
            stop_entry(found->first, found->second, StopReason::LiveChange);
            m_entries.erase(found);
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
        const auto reloaded = reload_changed_scripts(pending);
        if(!reloaded)
            return Result<void, Error>::failure(reloaded.error());
        for(const auto& [key, entity] : pending) {
            if(reloaded.value().contains(key.asset) || m_entries.contains(key))
                continue;
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

    Result<void, Error> ScriptSystem::on_start(
        Scene& scene, RuntimeSession& session, const RuntimeServices& services) {
        m_scene = &scene;
        m_session = &session;
        m_audio = services.audio;
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
                default:
                    return Result<void, Error>::failure({"Unknown contact event kind"});
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
    void ScriptSystem::on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept {
        stop_all();
    }
}

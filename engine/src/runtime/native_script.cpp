#include "native_script.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace Comet {
    namespace {
        uint64_t next_instance_key() {
            static std::atomic<uint64_t> next{0};
            return next.fetch_add(1, std::memory_order_relaxed) + 1;
        }
    }

    ScriptComponent::ScriptComponent() : m_instance_key(next_instance_key()) {}
    ScriptComponent::ScriptComponent(const ScriptComponent&) : ScriptComponent() {}
    ScriptComponent& ScriptComponent::operator=(const ScriptComponent& other) {
        if(this != &other)
            m_instance_key = next_instance_key();
        return *this;
    }

    NativeScriptSystem::NativeScriptSystem(const ComponentRegistry& registry) {
        for(const auto& descriptor : registry.components())
            if(descriptor.create_script)
                m_types.push_back(descriptor);
    }

    NativeScriptSystem::~NativeScriptSystem() {
        stop_all();
    }

    bool NativeScriptSystem::is_live(const Key& key, const Instance& instance) const {
        const auto& type = m_types[key.type];
        return instance.entity && instance.entity.get_uuid() == key.entity
               && type.has_component(instance.entity)
               && type.script_instance_key(instance.entity) == key.instance;
    }

    void NativeScriptSystem::stop_missing() {
        for(auto it = m_instances.begin(); it != m_instances.end();) {
            if(!is_live(it->first, it->second)) {
                it->second.script->on_stop();
                it = m_instances.erase(it);
            } else {
                ++it;
            }
        }
    }

    void NativeScriptSystem::stop_all() noexcept {
        for(auto it = m_instances.rbegin(); it != m_instances.rend(); ++it)
            it->second.script->on_stop();
        m_instances.clear();
    }

    void NativeScriptSystem::synchronize(Scene& scene) {
        stop_missing();
        const auto entities = scene.get_entities();
        std::vector<std::pair<Key, Entity>> pending;
        for(size_t type = 0; type < m_types.size(); ++type) {
            for(const auto& entity : entities) {
                if(m_types[type].has_component(entity)) {
                    const Key key{type, entity.get_uuid(),
                        m_types[type].script_instance_key(entity)};
                    if(!m_instances.contains(key))
                        pending.emplace_back(key, entity);
                }
            }
        }
        std::ranges::sort(pending, {}, [](const auto& item) { return item.first; });
        for(const auto& [key, entity] : pending) {
            Instance instance{entity, nullptr};
            if(!is_live(key, instance))
                continue;
            instance.script = m_types[key.type].create_script();
            if(!instance.script)
                throw std::runtime_error("Native script factory returned null");
            if(!is_live(key, instance))
                continue;
            auto [it, inserted] = m_instances.emplace(key, std::move(instance));
            if(inserted)
                it->second.script->on_start(scene, entity);
        }
        stop_missing();
    }

    void NativeScriptSystem::on_start(Scene& scene) {
        if(!m_instances.empty())
            throw std::logic_error("Native script system is already active");
        synchronize(scene);
    }

    void NativeScriptSystem::dispatch(Scene& scene, const Context& context, bool fixed) {
        synchronize(scene);
        // 每次回调重新验证句柄／组件代，前一个脚本可删除或替换后一个。
        for(auto& [key, instance] : m_instances) {
            if(!is_live(key, instance))
                continue;
            if(fixed)
                instance.script->fixed_update(scene, instance.entity, context);
            else
                instance.script->update(scene, instance.entity, context);
        }
        stop_missing();
    }

    void NativeScriptSystem::fixed_update(Scene& scene, const Context& context) {
        dispatch(scene, context, true);
    }

    void NativeScriptSystem::update(Scene& scene, const Context& context) {
        dispatch(scene, context, false);
    }

    void NativeScriptSystem::on_stop(Scene&) noexcept {
        stop_all();
    }
}

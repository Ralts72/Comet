#include "scene/script_component.h"
#include <atomic>

namespace Comet {
    namespace {
        std::atomic<uint64_t> next_lifetime{0};
    }
    ScriptComponent::ScriptComponent()
        : m_lifetime(next_lifetime.fetch_add(1, std::memory_order_relaxed) + 1) {}
    ScriptComponent::ScriptComponent(const ScriptComponent& other) : ScriptComponent() {
        asset = other.asset;
        parameters = other.parameters;
    }
    ScriptComponent& ScriptComponent::operator=(const ScriptComponent& other) {
        if(this != &other) {
            asset = other.asset;
            parameters = other.parameters;
            m_running_script.reset();
            m_lifetime = next_lifetime.fetch_add(1, std::memory_order_relaxed) + 1;
        }
        return *this;
    }
}

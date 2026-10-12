#include "scene/script_component.h"
#include "common/version.h"

namespace Comet {
    namespace {
        VersionSequence next_lifetime;
    }
    ScriptComponent::ScriptComponent() : m_lifetime(next_lifetime.next()) {}
    ScriptComponent::ScriptComponent(const ScriptComponent& other) : ScriptComponent() {
        asset = other.asset;
        parameters = other.parameters;
    }
    ScriptComponent& ScriptComponent::operator=(const ScriptComponent& other) {
        if(this != &other) {
            asset = other.asset;
            parameters = other.parameters;
            m_lifetime = next_lifetime.next();
        }
        return *this;
    }
}

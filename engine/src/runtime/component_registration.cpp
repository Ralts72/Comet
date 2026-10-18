#include "scene/component_registration.h"
#include "diagnostics/logger.h"

namespace Comet {
    void ComponentRegistration::add(ComponentRegistry& registry, ComponentDescriptor descriptor) {
        if(!registry.register_component(std::move(descriptor)))
            LOG_FATAL("Invalid built-in component descriptor");
    }
    ComponentRegistry create_scene_component_registry() {
        ComponentRegistry registry;
        ComponentRegistration::core(registry);
        ComponentRegistration::render(registry);
        ComponentRegistration::animation(registry);
        ComponentRegistration::physics(registry);
        ComponentRegistration::gameplay(registry);
        ComponentRegistration::audio(registry);
        ComponentRegistration::particles(registry);
        return registry;
    }
}

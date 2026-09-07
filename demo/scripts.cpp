#include "scripts.h"

#include <stdexcept>

namespace CometDemo {
    namespace {
        class SpinScript final: public Comet::NativeScript {
        public:
            void fixed_update(Comet::Scene&, Comet::Entity entity,
                const Comet::System::Context& context) override {
                const auto& spin = entity.get_component<SpinComponent>();
                if(spin.enabled)
                    entity.get_component<Comet::TransformComponent>().rotate(
                        {0, spin.speed * float(context.delta_time), 0});
            }
        };
    }

    Comet::ComponentRegistry create_component_registry() {
        auto registry = Comet::create_scene_component_registry();
        auto descriptor = Comet::make_script_descriptor<SpinComponent, SpinScript>(
            "demo.spin", "Spin Script",
            {Comet::make_property_descriptor("speed", "Degrees per second",
                 &SpinComponent::speed, {.numeric = {.speed = 1.0f}}),
                Comet::make_property_descriptor(
                    "enabled", "Enabled", &SpinComponent::enabled)});
        if(!registry.register_component(std::move(descriptor)))
            throw std::logic_error("Invalid demo script descriptor");
        return registry;
    }
}

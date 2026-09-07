#pragma once

#include "runtime/native_script.h"

namespace CometDemo {
    struct SpinComponent: Comet::ScriptComponent {
        float speed = 100.0f;
        bool enabled = true;
    };

    Comet::ComponentRegistry create_component_registry();
}

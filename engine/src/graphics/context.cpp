#include "context.h"

namespace Comet {
    Context::Context(const Window& window) {
        create_instance();
        create_surface(window);
        pickup_physical_device();
    }

    Context::~Context() {}
    void Context::create_instance() {}
    void Context::pickup_physical_device() {}
    void Context::create_surface(const Window& window) {}
    void Context::create_device(Vec2i size) {}
}

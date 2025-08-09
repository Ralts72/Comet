#pragma once
#include <vulkan/vulkan.hpp>
#include "core/window/window.h"

namespace Comet {
    class Context {
    public:
        explicit Context(const Window& window);

        Context(const Context&) = delete;

        Context(Context&&) = delete;

        Context& operator=(const Context&) = delete;

        Context& operator=(Context&&) = delete;

        ~Context();

        [[nodiscard]] vk::Instance instance() const { return m_instance; }
        [[nodiscard]] vk::PhysicalDevice get_physical_device() const { return m_physical_device; }
        [[nodiscard]] vk::SurfaceKHR get_surface() const { return m_surface; }

    private:
        void create_instance();

        void pickup_physical_device();

        void create_surface(const Window& window);

        void create_device(Vec2i size);

        vk::Instance m_instance;
        vk::PhysicalDevice m_physical_device;
        vk::SurfaceKHR m_surface;
    };
}



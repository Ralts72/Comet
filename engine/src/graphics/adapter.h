#pragma once
#include "../core/window.h"

namespace Comet {
    class Device;

#pragma once
    class Adapter {
    public:
        explicit Adapter(const Window& window);

        Adapter(const Adapter&) = delete;

        Adapter& operator=(const Adapter&) = delete;

        ~Adapter();

        [[nodiscard]] vk::Instance getInstance() const { return m_instance; }
        [[nodiscard]] vk::PhysicalDevice getPhysicalDevice() const { return m_physical_device; }
        [[nodiscard]] vk::SurfaceKHR getSurface() const { return m_surface; }
        [[nodiscard]] Device* getDevice() const { return m_device; }

    private:
        void createInstance();

        void pickupPhysicalDevice();

        void createSurface(const Window& window);

        void createDevice();

        vk::Instance m_instance;
        vk::PhysicalDevice m_physical_device;
        vk::SurfaceKHR m_surface;
        Device* m_device{};
    };
}

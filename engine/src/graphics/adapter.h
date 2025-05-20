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

        [[nodiscard]] VkInstance getInstance() const { return m_instance; }
        [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const { return m_phyDevice; }
        [[nodiscard]] VkSurfaceKHR getSurface() const { return m_surface; }
        [[nodiscard]] Device* getDevice() const { return m_device; }

    private:
        void createInstance();

        void pickupPhysicalDevice();

        void createSurface(const Window& window);

        void createDevice();

        VkInstance m_instance{};
        VkPhysicalDevice m_phyDevice{};
        VkSurfaceKHR m_surface{};
        Device* m_device{};
    };
}

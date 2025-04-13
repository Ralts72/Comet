#pragma once
// #include <vulkan/vulkan.hpp>
#include "../core/Log.h"
#include "../graphics/Swapchain.h"
#include <GLFW/glfw3.h>

namespace Comet
{

    class Context
    {
    public:
        Context(GLFWwindow *window);
        ~Context();

        struct QueueFamilyIndices
        {
            std::optional<uint32_t> graphicsQueue;
            std::optional<uint32_t> presentQueue;
            operator bool() const { return graphicsQueue.has_value() && presentQueue.has_value(); }
        };
        void init();
        void swapBuffers();

        vk::Instance getInstance() const { return m_instance; }
        vk::PhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
        vk::Device getDevice() const { return m_device; }
        vk::SurfaceKHR getSurface() const { return m_surface; }
        QueueFamilyIndices getQueueFamilyIndices() const { return m_queueFamilyIndices; }
        Swapchain *getSwapchain() const { return m_swapchain.get(); }

    private:
        void createInstance();
        void selectPhysicalDevice();
        void createDevice();
        void createSurface();
        void queryQueueFamilies();
        void getQueues();

        vk::Instance m_instance;
        vk::SurfaceKHR m_surface;
        vk::PhysicalDevice m_physicalDevice;
        QueueFamilyIndices m_queueFamilyIndices;
        vk::Device m_device;
        vk::PhysicalDeviceMemoryProperties m_physicalDeviceMemoryProperties;
        vk::Queue m_graphicsQueue;
        vk::Queue m_presentQueue;

        std::unique_ptr<Swapchain> m_swapchain;

        GLFWwindow *m_windowHandle;
    };
}
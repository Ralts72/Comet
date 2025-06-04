#include "device.h"

namespace Comet {
    Device::Device(const Adapter& adapter, Vec2i size): m_adapter(adapter) {
        auto physicalDevice = adapter.getPhysicalDevice();
        chooseQueue(physicalDevice, adapter.getSurface());
        if(!m_queue_indices) {
            LOG_FATAL("no graphics queue in your GPU");
        }
        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> indices{
            m_queue_indices.graphicsIndex.value(),
            m_queue_indices.presentIndex.value()
        };
        float priority = 1.0;
        for(auto idx: indices) {
            vk::DeviceQueueCreateInfo queueCreateInfo = {};
            queueCreateInfo.queueFamilyIndex = idx;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &priority;
            queueCreateInfos.push_back(queueCreateInfo);
        }
        std::vector<const char*> requiredExtensions = {
            "VK_KHR_portability_subset",
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            "VK_KHR_buffer_device_address"
        };

        std::set<std::string> availableExtensions;
        for(const auto& prop: physicalDevice.enumerateDeviceExtensionProperties()) {
            availableExtensions.emplace(prop.extensionName);
        }

        std::vector<const char*> extensionNames;
        for(const auto& ext: requiredExtensions) {
            if(availableExtensions.contains(ext)) {
                LOG_INFO("Enable Vulkan device extension: {}", ext);
                extensionNames.push_back(ext);
            } else {
                LOG_WARN("Required device extension not supported and skipped: {}", ext);
            }
        }

        auto features = physicalDevice.getFeatures();
        // features.geometryShader = true;
        vk::DeviceCreateInfo createInfo = {};
        createInfo.queueCreateInfoCount = queueCreateInfos.size();
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.ppEnabledExtensionNames = extensionNames.data();
        createInfo.enabledExtensionCount = extensionNames.size();
        createInfo.pEnabledFeatures = &features;
        m_device = physicalDevice.createDevice(createInfo);
        if(!m_device) {
            LOG_FATAL("failed to create vulkan device");
        }
        m_graphics_queue = m_device.getQueue(m_queue_indices.graphicsIndex.value(), 0);
        m_present_queue = m_device.getQueue(m_queue_indices.presentIndex.value(), 0);
    }

    Device::~Device() {
        m_device.destroy();
    }

    void Device::chooseQueue(const vk::PhysicalDevice& physicalDevice, const vk::SurfaceKHR& surface) {
        const auto queueFamilies = physicalDevice.getQueueFamilyProperties();

        for(int i = 0; i < queueFamilies.size(); i++) {
            const auto& prop = queueFamilies[i];
            if(prop.queueFlags & vk::QueueFlagBits::eGraphics) {
                m_queue_indices.graphicsIndex = i;
            }
            if(physicalDevice.getSurfaceSupportKHR(i, surface)) {
                m_queue_indices.presentIndex = i;
            }
            if(m_queue_indices) {
                break;
            }
        }
    }
}

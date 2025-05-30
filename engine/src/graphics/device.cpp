#include "device.h"
#include "../common/macro.h"

namespace Comet {
    Device::Device(const Adapter& adapter) {
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
        std::vector requiredExtensions = {"VK_KHR_portability_subset",VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        auto extensionsProps = physicalDevice.enumerateDeviceExtensionProperties();
        removeUnexistsElems<const char*, vk::ExtensionProperties>(
            requiredExtensions, extensionsProps,
            [](const auto& e1, const vk::ExtensionProperties& e2) {
                return std::strcmp(e1, e2.extensionName) == 0;
            });
        for(auto ext: requiredExtensions) {
            LOG_INFO("enable vulkan device extension: {}", ext);
        }
        std::vector<const char*> extensionNames;
        extensionNames.reserve(extensionsProps.size());
        for(const auto& ext: extensionsProps) {
            extensionNames.push_back(ext.extensionName);
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

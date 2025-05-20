#include "device.h"
#include "../common/log.h"
#include "../common/macro.h"

namespace Comet {
    Device::Device(const Adapter& adapter) {
        auto physical_device = adapter.getPhysicalDevice();
        m_queue_indices = chooseQueue(physical_device, adapter.getSurface());

        if(!m_queue_indices) {
            LOG_FATAL("no graphics queue in your GPU");
        }

        VkDeviceCreateInfo device_ci{};
        device_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

        std::set indices{
            m_queue_indices.graphicsIndex.value(),
            m_queue_indices.presentIndex.value()
        };

        float priority = 1.0;
        for(auto idx: indices) {
            VkDeviceQueueCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            ci.queueCount = 1;
            ci.queueFamilyIndex = idx;
            ci.pQueuePriorities = &priority;
            queueCreateInfos.push_back(ci);
        }
        device_ci.queueCreateInfoCount = queueCreateInfos.size();
        device_ci.pQueueCreateInfos = queueCreateInfos.data();

        std::vector requireExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        std::vector<VkExtensionProperties> extension_props;
        uint32_t extensionCount = 0;
        VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extensionCount, nullptr));
        extension_props.resize(extensionCount);
        VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &extensionCount, extension_props.data()));

        removeUnexistsElems<const char*, VkExtensionProperties>(
            requireExtensions, extension_props,
            [](const auto& e1, const VkExtensionProperties& e2) {
                return std::strcmp(e1, e2.extensionName) == 0;
            });

        for(auto ext: requireExtensions) {
            LOG_INFO("enable vulkan device extension: {}", ext);
        }

        std::vector<const char*> extension_names;
        extension_names.reserve(extension_props.size());
        for(const auto& ext: extension_props) {
            extension_names.push_back(ext.extensionName);
        }

        device_ci.ppEnabledExtensionNames = extension_names.data();
        device_ci.enabledExtensionCount = extension_names.size();

        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(physical_device, &features);

        // features.geometryShader = true;
        device_ci.pEnabledFeatures = &features;

        VK_CHECK(vkCreateDevice(physical_device, &device_ci, nullptr, &m_device));

        if(!m_device) {
            LOG_FATAL("failed to create vulkan device");
        }
        volkLoadDevice(m_device);

        vkGetDeviceQueue(m_device, m_queue_indices.graphicsIndex.value(), 0,
            &m_graphics_queue);
        vkGetDeviceQueue(m_device, m_queue_indices.presentIndex.value(), 0,
            &m_present_queue);
    }

    Device::~Device() {
        vkDestroyDevice(m_device, nullptr);
    }

    Device::QueueFamilyIndices Device::chooseQueue(VkPhysicalDevice phyDevice, VkSurfaceKHR surface) {
        uint32_t count = 0;
        std::vector<VkQueueFamilyProperties> queue_families;
        vkGetPhysicalDeviceQueueFamilyProperties(phyDevice, &count, nullptr);
        queue_families.resize(count);
        vkGetPhysicalDeviceQueueFamilyProperties(phyDevice, &count, queue_families.data());

        QueueFamilyIndices indices;

        for(int i = 0; i < queue_families.size(); i++) {
            const auto& prop = queue_families[i];
            if(prop.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsIndex = i;
            }
            VkBool32 supportSurface = false;
            VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(phyDevice, i, surface, &supportSurface));
            if(supportSurface) {
                indices.presentIndex = i;
            }

            if(indices) {
                break;
            }
        }

        return indices;
    }
}

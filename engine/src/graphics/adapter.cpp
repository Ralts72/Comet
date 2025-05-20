#include "adapter.h"
#include "device.h"
#include "../common/log.h"
#include "../common/macro.h"

namespace Comet {
    Adapter::Adapter(const Window& window) {
        if(volkInitialize() != VK_SUCCESS) {
            LOG_ERROR("volk init failed");
        }

        LOG_INFO("creating vulkan instance");
        createInstance();
        LOG_INFO("picking up physics device");
        pickupPhysicalDevice();
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_phyDevice, &props);
        LOG_INFO("pick {}", props.deviceName);
        LOG_INFO("creating surface");
        createSurface(window);

        LOG_INFO("creating render device");
        createDevice();
    }

    Adapter::~Adapter() {
        delete m_device;
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        vkDestroyInstance(m_instance, nullptr);
    }

    void Adapter::createInstance() {
        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.apiVersion = VK_API_VERSION_1_3;
        appInfo.pEngineName = "NickelEngine";
        ci.pApplicationInfo = &appInfo;

        unsigned int count;
        const auto extensions = SDL_Vulkan_GetInstanceExtensions(&count);

        ci.enabledExtensionCount = count;
        ci.ppEnabledExtensionNames = extensions;

        std::vector<VkLayerProperties> supportLayers;
        uint32_t layerCount;
        VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));
        supportLayers.resize(layerCount);
        VK_CHECK(vkEnumerateInstanceLayerProperties(&layerCount, supportLayers.data()));

        std::vector<const char*> requireLayers;
        requireLayers.push_back("VK_LAYER_KHRONOS_validation");
        LOG_INFO("Vulkan enable validation layer");

        using LiteralString = const char*;
        removeUnexistsElems<const char*, VkLayerProperties>(
            requireLayers, supportLayers,
            [](const LiteralString& require, const VkLayerProperties& prop) {
                return std::strcmp(prop.layerName, require) == 0;
            });

        ci.enabledLayerCount = static_cast<uint32_t>(requireLayers.size());
        ci.ppEnabledLayerNames = requireLayers.data();
        VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance));
        volkLoadInstance(m_instance);
    }

    void Adapter::pickupPhysicalDevice() {
        if(!m_instance) {
            LOG_ERROR("vulkan instance not created");
        }

        uint32_t count = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, nullptr));
        std::vector<VkPhysicalDevice> physicalDevices(count);
        VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, physicalDevices.data()));

        if(physicalDevices.empty()) {
            LOG_ERROR("vulkan physical device not found");
        }
        m_phyDevice = physicalDevices[0];
    }

    void Adapter::createSurface(const Window& window) {
        if(!SDL_Vulkan_CreateSurface(window.getWindow(), m_instance, nullptr, &m_surface)) {
            LOG_FATAL("create vulkan surface failed");
        }
    }

    void Adapter::createDevice() {
        m_device = new Device{*this};
    }
}

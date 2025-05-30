#include "adapter.h"
#include "device.h"
#include "../common/macro.h"


namespace Comet {
    Adapter::Adapter(const Window& window) {
        LOG_INFO("creating vulkan instance");
        createInstance();
        LOG_INFO("picking up physics device");
        pickupPhysicalDevice();
        LOG_INFO("creating surface");
        createSurface(window);
        LOG_INFO("creating render device");
        createDevice();
    }

    Adapter::~Adapter() {
        delete m_device;
        m_instance.destroySurfaceKHR(m_surface);
        m_instance.destroy();
    }

    void Adapter::createInstance() {
        vk::ApplicationInfo appInfo{};
        appInfo.pApplicationName = "CometApp";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "CometEngine";
        appInfo.apiVersion = VK_API_VERSION_1_3;

        const auto availableLayers = vk::enumerateInstanceLayerProperties();
        LOG_INFO("available layers:");
        for(const auto& layer: availableLayers) {
            LOG_INFO("  {}", std::string(layer.layerName.begin(), layer.layerName.end()));
        }
        std::vector<const char*> requiredLayers;
#ifdef BUILD_TYPE_DEBUG
        requiredLayers.push_back("VK_LAYER_KHRONOS_validation");
        LOG_INFO("Vulkan enable validation layer");
#endif
        removeUnexistsElems<const char*, vk::LayerProperties>(
            requiredLayers, availableLayers,
            [](const char* require, const vk::LayerProperties& prop) {
                return std::strcmp(prop.layerName, require) == 0;
            });
        unsigned int count;
        const auto extensions = SDL_Vulkan_GetInstanceExtensions(&count);
        vk::InstanceCreateInfo createInfo = {};
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledLayerCount = static_cast<uint32_t>(requiredLayers.size());
        createInfo.ppEnabledLayerNames = requiredLayers.data();
        createInfo.enabledExtensionCount = count;
        createInfo.ppEnabledExtensionNames = extensions;
        m_instance = vk::createInstance(createInfo);
    }

    void Adapter::pickupPhysicalDevice() {
        if(!m_instance) {
            LOG_ERROR("vulkan instance not created");
        }

        const auto devices = m_instance.enumeratePhysicalDevices();
        if(devices.empty()) {
            LOG_ERROR("vulkan physical device not found");
        }
        m_physical_device = devices[0];
        std::cout << "Comet Engine: " << "physical device : " << m_physical_device.getProperties().deviceName << std::endl;
    }

    void Adapter::createSurface(const Window& window) {
        if(!SDL_Vulkan_CreateSurface(window.getWindow(), m_instance, nullptr, reinterpret_cast<VkSurfaceKHR*>(&m_surface))) {
            LOG_FATAL("create vulkan surface failed");
        }
    }

    void Adapter::createDevice() {
        m_device = new Device(*this);
    }
}

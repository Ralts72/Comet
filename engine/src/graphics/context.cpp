#include "context.h"

namespace Comet {
    static std::vector<DeviceFeature> s_required_layers = {
#ifdef BUILD_TYPE_DEBUG
        {"VK_LAYER_KHRONOS_validation", true},
#endif
    };

    static std::vector<DeviceFeature> s_required_extensions = {
#ifdef __APPLE__
        // 在 macOS 上添加必要的 MoltenVK 扩展
        {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, true},
        {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, true},
#endif
        {VK_KHR_SURFACE_EXTENSION_NAME, true},
        // {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, true},
        {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, true}
    };

    static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_utils_messenger_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData) noexcept {
        if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            LOG_ERROR("Vulkan Validation: {}", pCallbackData->pMessage);
        } else if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            LOG_WARN("Vulkan Validation: {}", pCallbackData->pMessage);
        } else if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
            LOG_INFO("Vulkan Validation: {}", pCallbackData->pMessage);
        } else if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
            LOG_DEBUG("Vulkan Validation: {}", pCallbackData->pMessage);
        }

        return VK_FALSE;
    }

    Context::Context(const Window& window) {
        create_instance();
        create_surface(window);
        pickup_physical_device();
        choose_queue_families();
        m_memory_properties = m_physical_device.getMemoryProperties();
    }

    Context::~Context() {
        m_instance.destroySurfaceKHR(m_surface);
        m_instance.destroy();
    }

    void Context::create_instance() {
        vk::ApplicationInfo app_info{};
        app_info.pApplicationName = "CometApp";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "CometEngine";
        app_info.apiVersion = vk::ApiVersion13;

        // 1.构建layer
        const auto available_layers_props = vk::enumerateInstanceLayerProperties();
        std::set<std::string> available_layers;
        for(const auto& prop: available_layers_props) {
            available_layers.emplace(prop.layerName);
        }
        const std::vector<const char*> enabled_layers = build_enabled_list(s_required_layers, available_layers, "layer");

        // 2. 构建扩展
        // 获取 GLFW 所需的扩展
        unsigned int glfw_extension_count;
        const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
        std::vector<const char*> enabled_extensions(glfw_extensions, glfw_extensions + glfw_extension_count);

        // 检查扩展可用性
        const auto available_extensions = vk::enumerateInstanceExtensionProperties();
        std::set<std::string> available_extension_names;
        for(const auto& ext: available_extensions) {
            available_extension_names.insert(ext.extensionName);
        }
        const std::vector<const char*> custom_extensions = build_enabled_list(s_required_extensions, available_extension_names, "extension");
        enabled_extensions.insert(enabled_extensions.end(), custom_extensions.begin(), custom_extensions.end());

        // 3. debug utils messenger
        vk::DebugUtilsMessengerCreateInfoEXT debug_utils_create_info{};
        debug_utils_create_info.pNext = nullptr;
        debug_utils_create_info.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                                  vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        debug_utils_create_info.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                              vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                                              vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        debug_utils_create_info.pfnUserCallback = vk_debug_utils_messenger_callback;

        vk::InstanceCreateInfo create_info = {};
#ifdef __APPLE__
        // macOS 需要启用可移植性枚举标志
        create_info.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif

#ifdef BUILD_TYPE_DEBUG
        create_info.pNext = &debug_utils_create_info;
#endif
        create_info.pApplicationInfo = &app_info;
        create_info.enabledLayerCount = static_cast<uint32_t>(enabled_layers.size());
        create_info.ppEnabledLayerNames = enabled_layers.data();
        create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
        create_info.ppEnabledExtensionNames = enabled_extensions.data();
        m_instance = vk::createInstance(create_info);
    }

    void Context::pickup_physical_device() {
        if(!m_instance) {
            LOG_FATAL("vulkan instance not created");
        }

        const auto devices = m_instance.enumeratePhysicalDevices();
        if(devices.empty()) {
            LOG_FATAL("vulkan physical device not found");
        }
        m_physical_device = devices[0];
        const std::string deviceName = "physical device : " + static_cast<std::string>(m_physical_device.getProperties().deviceName);
        LOG_INFO(deviceName);
    }

    void Context::create_surface(const Window& window) {
        const auto glfw_window = window.get_window();
        if(!glfw_window) {
            LOG_FATAL("glfw window not created");
        }
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if(glfwCreateWindowSurface(m_instance, glfw_window, nullptr, &surface) != VK_SUCCESS) {
            LOG_FATAL("create vulkan surface failed");
        }
        m_surface = vk::SurfaceKHR(surface);
    }

    void Context::choose_queue_families() {
        auto queue_families = m_physical_device.getQueueFamilyProperties();

        std::optional<uint32_t> graphics_index;
        std::optional<uint32_t> present_index;

        for(uint32_t i = 0; i < queue_families.size(); i++) {
            const auto& props = queue_families[i];
            if(props.queueCount == 0) {
                continue;
            }
            const auto is_graphics = props.queueFlags & vk::QueueFlagBits::eGraphics;
            const bool is_present = m_physical_device.getSurfaceSupportKHR(i, m_surface);
            if(is_graphics && !graphics_index.has_value()) {
                graphics_index = i;
                continue;
            }
            if(is_present && !present_index.has_value()) {
                present_index = i;
                continue;
            }
            if(graphics_index.has_value() && present_index.has_value()) {
                break;
            }
        }
        // 如果都没找到，报错
        if(!graphics_index.has_value() && !present_index.has_value()) {
            LOG_FATAL("not found graphics and present queue family");
        }
        // 如果present还没找到，且graphics队列支持，使用graphics队列簇
        if(!present_index.has_value() && m_physical_device.getSurfaceSupportKHR(graphics_index.value(), m_surface)) {
            LOG_DEBUG("graphics queue family also is present queue family");
            present_index = graphics_index;
        }
        // 如果present队列簇还没找到，且present队列簇支持graphics队列簇
        if(!graphics_index.has_value() && (queue_families[present_index.value()].queueFlags & vk::QueueFlagBits::eGraphics)) {
            LOG_DEBUG("present queue family also is graphics queue family");
            graphics_index = present_index;
        }
        // 保存结果
        m_graphics_queue_family.queue_family_index = graphics_index;
        m_graphics_queue_family.queue_count = queue_families[graphics_index.value()].queueCount;

        m_present_queue_family.queue_family_index = present_index;
        m_present_queue_family.queue_count = queue_families[present_index.value()].queueCount;

        LOG_INFO("graphics queue family index : {}, queue count is {}", graphics_index.value(), m_graphics_queue_family.queue_count);
        LOG_INFO("present queue family index : {}, queue count is {}", present_index.value(), m_present_queue_family.queue_count);
    }
}

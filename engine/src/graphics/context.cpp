#include "context.h"

namespace Comet {
    static std::vector<DeviceFeature> required_layers = {
#ifdef BUILD_TYPE_DEBUG
        {"VK_LAYER_KHRONOS_validation", true},
#endif
    };

    static std::vector<DeviceFeature> required_extensions = {
#ifdef __APPLE__
        // 在 macOS 上添加必要的 MoltenVK 扩展
        {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, true},
        {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, true},
#endif
        {VK_KHR_SURFACE_EXTENSION_NAME, true},
        {VK_EXT_DEBUG_REPORT_EXTENSION_NAME, true},
    };

    static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_report_callback(
        VkDebugReportFlagsEXT flags,
        VkDebugReportObjectTypeEXT objectType,
        uint64_t object,
        size_t location,
        int32_t messageCode,
        const char* pLayerPrefix,
        const char* pMessage,
        void* pUserData) noexcept {
        if(flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
            LOG_ERROR("{}", pMessage);
        } else if(flags & (VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)) {
            LOG_WARN("{}", pMessage);
        }

        return VK_TRUE;
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
        const std::vector<const char*> enabled_layers = build_enabled_list(required_layers, available_layers, "layer");

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
        const std::vector<const char*> custom_extensions = build_enabled_list(required_extensions, available_extension_names, "extension");
        enabled_extensions.insert(enabled_extensions.end(), custom_extensions.begin(), custom_extensions.end());

        // 3. debug report
        vk::DebugReportCallbackCreateInfoEXT debug_create_info{};
        debug_create_info.pNext = nullptr;
        debug_create_info.flags = vk::DebugReportFlagBitsEXT::eWarning |
                                  vk::DebugReportFlagBitsEXT::ePerformanceWarning |
                                  vk::DebugReportFlagBitsEXT::eError;
        debug_create_info.pfnCallback = vk_debug_report_callback;

        vk::InstanceCreateInfo create_info = {};
#ifdef __APPLE__
        // macOS 需要启用可移植性枚举标志
        create_info.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif
#ifdef BUILD_TYPE_DEBUG
        create_info.pNext = &debug_create_info;
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
            LOG_ERROR("vulkan instance not created");
        }

        const auto devices = m_instance.enumeratePhysicalDevices();
        if(devices.empty()) {
            LOG_ERROR("vulkan physical device not found");
        }
        m_physical_device = devices[0];
        const std::string deviceName = "physical device : " + static_cast<std::string>(m_physical_device.getProperties().deviceName);
        LOG_INFO(deviceName);
    }

    void Context::create_surface(const Window& window) {
        auto glfw_window = window.get_window();
        if(!glfw_window) {
            LOG_ERROR("glfw window not created");
        }
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if(glfwCreateWindowSurface(m_instance, glfw_window, nullptr, &surface) != VK_SUCCESS) {
            LOG_FATAL("create vulkan surface failed");
        }
        m_surface = vk::SurfaceKHR(surface);
    }


    void Context::create_device(Vec2i size) {}

    void Context::choose_queue_families() {
        auto queue_families = m_physical_device.getQueueFamilyProperties();

        std::optional<uint32_t> graphics_index;
        std::optional<uint32_t> present_index;

        // 先遍历一次找到任意一个graphics和任意一个present（不考虑是否相同）
        for(uint32_t i = 0; i < queue_families.size(); i++) {
            const auto& props = queue_families[i];

            const auto is_graphics = props.queueFlags & vk::QueueFlagBits::eGraphics;
            const bool is_present = m_physical_device.getSurfaceSupportKHR(i, m_surface);

            if(is_graphics && !graphics_index.has_value()) {
                graphics_index = i;
            }

            if(is_present && !present_index.has_value()) {
                present_index = i;
            }

            if(graphics_index.has_value() && present_index.has_value()) {
                break;
            }
        }

        // 如果graphics和present是同一个队列簇，尝试找不同的present队列簇做优先分开
        if(graphics_index.has_value() && present_index.has_value() && graphics_index == present_index) {
            for(uint32_t i = 0; i < queue_families.size(); i++) {
                if(i == graphics_index.value()) continue;

                if(m_physical_device.getSurfaceSupportKHR(i, m_surface)) {
                    present_index = i;
                    break;
                }
            }
        }

        // 如果present还没找到，且graphics队列支持，使用graphics队列簇
        if(!present_index.has_value() && graphics_index.has_value() && m_physical_device.getSurfaceSupportKHR(graphics_index.value(), m_surface)) {
            present_index = graphics_index;
        }

        // 保存结果
        m_graphics_queue_family.queue_family_index = graphics_index;
        m_graphics_queue_family.queue_count = graphics_index ? queue_families[graphics_index.value()].queueCount : 0;

        m_present_queue_family.queue_family_index = present_index;
        m_present_queue_family.queue_count = present_index ? queue_families[present_index.value()].queueCount : 0;

        if(!graphics_index.has_value()) {
            LOG_FATAL("not found graphics queue family");
        }
        LOG_INFO("graphics queue family index : {}, queue count is {}", graphics_index.value(), m_graphics_queue_family.queue_count);
        LOG_INFO("present queue family index : {}, queue count is {}", present_index.value(), m_present_queue_family.queue_count);
    }
}


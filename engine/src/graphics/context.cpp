#include "context.h"

#include <algorithm>
#include <string_view>

namespace Comet {
    namespace {
        const std::vector<const char*> requested_instance_extensions = {
#ifdef __APPLE__
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#endif
        };

        VKAPI_ATTR vk::Bool32 VKAPI_CALL vk_debug_utils_messenger_callback(
            vk::DebugUtilsMessageSeverityFlagBitsEXT message_severity,
            vk::DebugUtilsMessageTypeFlagsEXT,
            const vk::DebugUtilsMessengerCallbackDataEXT* callback_data,
            void*) noexcept {
            const auto logger = Logger::get_console_logger();
            if(!logger) {
                return VK_FALSE;
            }

            if(message_severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
                logger->error("Vulkan Validation: {}", callback_data->pMessage);
            } else if(message_severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
                logger->warn("Vulkan Validation: {}", callback_data->pMessage);
            } else if(message_severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
                logger->info("Vulkan Validation: {}", callback_data->pMessage);
            } else if(message_severity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose) {
                logger->debug("Vulkan Validation: {}", callback_data->pMessage);
            }

            return VK_FALSE;
        }

        vk::DebugUtilsMessengerCreateInfoEXT make_debug_messenger_create_info() {
            vk::DebugUtilsMessengerCreateInfoEXT create_info{};
            create_info.messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
                                          | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
            create_info.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
                                      | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
                                      | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
            create_info.pfnUserCallback = vk_debug_utils_messenger_callback;
            return create_info;
        }

        vk::DebugUtilsMessengerEXT create_debug_messenger(
            const vk::Instance instance,
            const vk::DebugUtilsMessengerCreateInfoEXT& create_info) {
            const auto create_function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                instance.getProcAddr("vkCreateDebugUtilsMessengerEXT"));
            if(!create_function) {
                LOG_FATAL("Failed to load vkCreateDebugUtilsMessengerEXT");
            }

            VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
            const VkResult result = create_function(
                static_cast<VkInstance>(instance),
                reinterpret_cast<const VkDebugUtilsMessengerCreateInfoEXT*>(&create_info),
                nullptr,
                &messenger);
            if(result != VK_SUCCESS) {
                LOG_FATAL("Failed to create Vulkan debug messenger: {}",
                    static_cast<int>(result));
            }
            return {messenger};
        }

        void destroy_debug_messenger(
            const vk::Instance instance,
            const vk::DebugUtilsMessengerEXT messenger) {
            const auto destroy_function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                instance.getProcAddr("vkDestroyDebugUtilsMessengerEXT"));
            if(!destroy_function) {
                LOG_FATAL("Failed to load vkDestroyDebugUtilsMessengerEXT");
            }
            destroy_function(
                static_cast<VkInstance>(instance),
                static_cast<VkDebugUtilsMessengerEXT>(messenger),
                nullptr);
        }
    }

    Context::Context(
        const Window& window,
        const Config::Vulkan& config,
        const DeviceCapabilityRequest& capability_request)
        : m_config(config) {
        create_instance();
        create_surface(window);
        pickup_physical_device(capability_request);
        m_memory_properties = m_device_capability.physical_device.getMemoryProperties();
    }

    Context::~Context() {
        if(m_surface) {
            m_instance.destroySurfaceKHR(m_surface);
        }
        if(m_debug_messenger) {
            destroy_debug_messenger(m_instance, m_debug_messenger);
        }
        if(m_instance) {
            m_instance.destroy();
        }
    }

    void Context::create_instance() {
        const uint32_t loader_api_version = vk::enumerateInstanceVersion();
        if(loader_api_version < REQUIRED_VULKAN_API_VERSION) {
            LOG_FATAL(
                "Vulkan loader API version {}.{}.{} is below required {}.{}.{}",
                VK_API_VERSION_MAJOR(loader_api_version),
                VK_API_VERSION_MINOR(loader_api_version),
                VK_API_VERSION_PATCH(loader_api_version),
                VK_API_VERSION_MAJOR(REQUIRED_VULKAN_API_VERSION),
                VK_API_VERSION_MINOR(REQUIRED_VULKAN_API_VERSION),
                VK_API_VERSION_PATCH(REQUIRED_VULKAN_API_VERSION));
        }

        vk::ApplicationInfo app_info{};
        app_info.pApplicationName = "CometApp";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "CometEngine";
        app_info.apiVersion = REQUIRED_VULKAN_API_VERSION;

        std::vector<const char*> requested_layers;
        if(m_config.enable_validation) {
            requested_layers.push_back("VK_LAYER_KHRONOS_validation");
        }

        std::set<std::string> available_layers;
        for(const auto& properties: vk::enumerateInstanceLayerProperties()) {
            available_layers.emplace(properties.layerName);
        }
        const std::vector<const char*> enabled_layers = get_available_names(
            requested_layers, available_layers, "layer");
        m_validation_enabled = !enabled_layers.empty();

        std::set<std::string> available_extension_names;
        for(const auto& extension: vk::enumerateInstanceExtensionProperties()) {
            available_extension_names.emplace(extension.extensionName);
        }

        unsigned int glfw_extension_count = 0;
        const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
        if(!glfw_extensions || glfw_extension_count == 0) {
            LOG_FATAL("GLFW did not provide the required Vulkan instance extensions");
        }

        std::vector<const char*> enabled_extensions;
        enabled_extensions.reserve(
            glfw_extension_count + requested_instance_extensions.size() + 1);
        for(uint32_t i = 0; i < glfw_extension_count; ++i) {
            if(!available_extension_names.contains(glfw_extensions[i])) {
                LOG_FATAL("Required GLFW Vulkan instance extension is unavailable: {}",
                    glfw_extensions[i]);
            }
            enabled_extensions.push_back(glfw_extensions[i]);
            LOG_INFO("Enabled GLFW instance extension: {}", glfw_extensions[i]);
        }

        const auto custom_extensions = get_available_names(
            requested_instance_extensions,
            available_extension_names,
            "instance extension");
        for(const char* extension: custom_extensions) {
            const bool already_enabled = std::ranges::any_of(
                enabled_extensions,
                [extension](const char* enabled_extension) {
                    return std::string_view(enabled_extension) == extension;
                });
            if(!already_enabled) {
                enabled_extensions.push_back(extension);
            }
        }

        if(m_validation_enabled) {
            if(available_extension_names.contains(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
                enabled_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                m_debug_utils_enabled = true;
                LOG_INFO("Enabled instance extension: {}", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            } else {
                LOG_WARN("Vulkan validation is enabled, but {} is unavailable; validation messages cannot be captured",
                    VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }
        } else if(m_config.enable_validation) {
            LOG_WARN("Vulkan validation was requested, but no validation layer is available");
        }

        const auto debug_messenger_create_info = make_debug_messenger_create_info();
        vk::InstanceCreateInfo create_info{};
#ifdef __APPLE__
        create_info.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif
        if(m_debug_utils_enabled) {
            create_info.pNext = &debug_messenger_create_info;
        }
        create_info.pApplicationInfo = &app_info;
        create_info.enabledLayerCount = static_cast<uint32_t>(enabled_layers.size());
        create_info.ppEnabledLayerNames = enabled_layers.data();
        create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
        create_info.ppEnabledExtensionNames = enabled_extensions.data();
        m_instance = vk::createInstance(create_info);

        if(m_debug_utils_enabled) {
            m_debug_messenger = create_debug_messenger(
                m_instance, debug_messenger_create_info);
            LOG_INFO("Vulkan debug messenger created successfully");
        }
        LOG_INFO("Vulkan instance created successfully (validation: {})",
            m_validation_enabled ? "enabled" : "disabled");
    }

    void Context::pickup_physical_device(
        const DeviceCapabilityRequest& capability_request) {
        if(!m_instance) {
            LOG_FATAL("Vulkan instance not created");
        }

        m_device_capability = select_physical_device(
            m_instance.enumeratePhysicalDevices(),
            m_surface,
            capability_request);
    }

    void Context::create_surface(const Window& window) {
        const auto glfw_window = window.get();
        if(!glfw_window) {
            LOG_FATAL("GLFW window not created");
        }
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if(glfwCreateWindowSurface(m_instance, glfw_window, nullptr, &surface) != VK_SUCCESS) {
            LOG_FATAL("Create Vulkan surface failed");
        }
        m_surface = vk::SurfaceKHR(surface);
        LOG_INFO("Vulkan surface created successfully");
    }
}

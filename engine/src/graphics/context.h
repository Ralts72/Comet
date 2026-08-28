#pragma once
#include "vk_common.h"
#include "vk_capability.h"
#include "core/window.h"
#include "common/config.h"

namespace Comet {
    class Context {
    public:
        Context(const Window& window, const Config::Vulkan& config,
                const DeviceCapabilityRequest& capability_request);

        Context(const Context&) = delete;

        Context(Context&&) = delete;

        Context& operator=(const Context&) = delete;

        Context& operator=(Context&&) = delete;

        ~Context();

        [[nodiscard]] vk::Instance instance() const { return m_instance; }
        [[nodiscard]] vk::PhysicalDevice get_physical_device() const {
            return m_device_capability.physical_device;
        }
        [[nodiscard]] vk::SurfaceKHR get_surface() const { return m_surface; }

        [[nodiscard]] bool is_same_queue_families() const {
            return m_device_capability.graphics_queue_family.queue_family_index
                == m_device_capability.present_queue_family.queue_family_index;
        }

        [[nodiscard]] QueueFamilyInfo get_graphics_queue_family() const {
            return m_device_capability.graphics_queue_family;
        }

        [[nodiscard]] QueueFamilyInfo get_present_queue_family() const {
            return m_device_capability.present_queue_family;
        }

        [[nodiscard]] const DeviceCapability& get_device_capability() const {
            return m_device_capability;
        }

    private:
        void create_instance(bool validation_requested);

        void pickup_physical_device(const DeviceCapabilityRequest& capability_request);

        void create_surface(const Window& window);

        vk::Instance m_instance;
        vk::DebugUtilsMessengerEXT m_debug_messenger;
        vk::SurfaceKHR m_surface;

        DeviceCapability m_device_capability;
    };
}

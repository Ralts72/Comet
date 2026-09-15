#pragma once
#include "common/export.h"
#include "vk_common.h"
#include "vk_capability.h"
#include "config/config.h"
#include "graphics/result.h"
#include <memory>

namespace Comet {
    class Window;
    class COMET_API Context {
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
        [[nodiscard]] vk::SurfaceKHR get_surface() const {
            return m_surface ? m_surface->get() : vk::SurfaceKHR{};
        }

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
        friend class Swapchain;
        const std::shared_ptr<vk::UniqueSurfaceKHR>& get_surface_owner() const { return m_surface; }
        Result<void, GraphicsError> recreate_surface(const Window& window);
        void create_instance(bool validation_requested);

        void pickup_physical_device(const DeviceCapabilityRequest& capability_request);

        void create_surface(const Window& window);
        Result<vk::UniqueSurfaceKHR, GraphicsError> create_surface_candidate(const Window& window);

        vk::Instance m_instance;
        vk::DebugUtilsMessengerEXT m_debug_messenger;
        std::shared_ptr<vk::UniqueSurfaceKHR> m_surface;

        DeviceCapability m_device_capability;
    };
}

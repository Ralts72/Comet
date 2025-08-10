#pragma once
#include "vk_common.h"
#include "core/window/window.h"

namespace Comet {
    struct QueueFamilyInfo {
        std::optional<uint32_t> queue_family_index;
        uint32_t queue_count = 0;
    };

    class Context {
    public:
        explicit Context(const Window& window);

        Context(const Context&) = delete;

        Context(Context&&) = delete;

        Context& operator=(const Context&) = delete;

        Context& operator=(Context&&) = delete;

        ~Context();

        [[nodiscard]] vk::Instance instance() const { return m_instance; }
        [[nodiscard]] vk::PhysicalDevice get_physical_device() const { return m_physical_device; }
        [[nodiscard]] vk::SurfaceKHR get_surface() const { return m_surface; }

        [[nodiscard]] bool is_same_queue_families() const {
            return m_graphics_queue_family.queue_family_index == m_present_queue_family.queue_family_index;
        }

        [[nodiscard]] QueueFamilyInfo get_graphics_queue_family() const { return m_graphics_queue_family; }
        [[nodiscard]] QueueFamilyInfo get_present_queue_family() const { return m_present_queue_family; }

        [[nodiscard]] vk::PhysicalDeviceMemoryProperties get_memory_properties() const {
            return m_memory_properties;
        }

    private:
        void create_instance();

        void pickup_physical_device();

        void create_surface(const Window& window);

        void create_device(Vec2i size);

        void choose_queue_families();

        vk::Instance m_instance;
        vk::PhysicalDevice m_physical_device;
        vk::SurfaceKHR m_surface;

        QueueFamilyInfo m_graphics_queue_family;
        QueueFamilyInfo m_present_queue_family;
        vk::PhysicalDeviceMemoryProperties m_memory_properties;
    };
}



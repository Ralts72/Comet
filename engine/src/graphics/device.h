#pragma once
#include "../pch.h"
#include "adapter.h"

namespace Comet {
#pragma once
    class Device {
    public:
        struct QueueFamilyIndices {
            std::optional<uint32_t> graphicsIndex;
            std::optional<uint32_t> presentIndex;

            explicit operator bool() const { return graphicsIndex && presentIndex; }

            [[nodiscard]] std::set<uint32_t> getUniqueIndices() const {
                return {graphicsIndex.value(), presentIndex.value()};
            }

            [[nodiscard]] bool HasSeparateQueue() const {
                return graphicsIndex.value() != presentIndex.value();
            }
        };

        explicit Device(const Adapter& adapter);

        ~Device();

        [[nodiscard]] vk::Device getVkDevice() const { return m_device; }
        [[nodiscard]] vk::Queue getGraphicsQueue() const { return m_graphics_queue; }
        [[nodiscard]] vk::Queue getPresentQueue() const { return m_present_queue; }
        [[nodiscard]] const QueueFamilyIndices& getQueueFamilyIndices() const { return m_queue_indices; }

        Device(const Device&) = delete;

        Device& operator=(const Device&) = delete;

    private:
        void chooseQueue(const vk::PhysicalDevice& physicalDevice, const vk::SurfaceKHR& surface);

        vk::Device m_device;
        vk::Queue m_graphics_queue;
        vk::Queue m_present_queue;
        QueueFamilyIndices m_queue_indices;
    };
}

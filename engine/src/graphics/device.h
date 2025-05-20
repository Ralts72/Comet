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

        [[nodiscard]] VkDevice getVkDevice() const { return m_device; }
        [[nodiscard]] VkQueue getGraphicsQueue() const { return m_graphics_queue; }
        [[nodiscard]] VkQueue getPresentQueue() const { return m_present_queue; }
        [[nodiscard]] const QueueFamilyIndices& getQueueFamilyIndices() const { return m_queue_indices; }

        Device(const Device&) = delete;

        Device& operator=(const Device&) = delete;

    private:
        static QueueFamilyIndices chooseQueue(VkPhysicalDevice phyDevice, VkSurfaceKHR surface);

        VkDevice m_device{};
        VkQueue m_present_queue{};
        VkQueue m_graphics_queue{};
        QueueFamilyIndices m_queue_indices;
    };
}

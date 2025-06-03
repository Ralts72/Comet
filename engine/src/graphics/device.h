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

            [[nodiscard]] std::vector<uint32_t> getIndices() const {
                std::vector<uint32_t> indices;
                if(!graphicsIndex.has_value() || !presentIndex.has_value()) {
                    return {};
                }
                if(graphicsIndex.has_value() == presentIndex.has_value()) {
                    indices.push_back(graphicsIndex.value());
                } else {
                    indices.push_back(graphicsIndex.value());
                    indices.push_back(presentIndex.value());
                }
                return indices;
            }
        };

        explicit Device(const Adapter& adapter, Vec2i size);

        ~Device();

        [[nodiscard]] vk::Device getVkDevice() const { return m_device; }
        [[nodiscard]] vk::Queue getGraphicsQueue() const { return m_graphics_queue; }
        [[nodiscard]] vk::Queue getPresentQueue() const { return m_present_queue; }
        [[nodiscard]] const QueueFamilyIndices& getQueueFamilyIndices() const { return m_queue_indices; }

        Device(const Device&) = delete;

        Device(Device&&) = delete;

        Device& operator=(Device&&) = delete;

        Device& operator=(const Device&) = delete;

    private:
        void chooseQueue(const vk::PhysicalDevice& physicalDevice, const vk::SurfaceKHR& surface);

        vk::Device m_device;
        vk::Queue m_graphics_queue;
        vk::Queue m_present_queue;
        QueueFamilyIndices m_queue_indices;
    };
}

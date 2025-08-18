#pragma once
#include "vk_common.h"

namespace Comet {
    class VertexInputDescription {
    public:
        VertexInputDescription() = default;

        void add_binding(uint32_t binding = 0, uint32_t stride = 0, vk::VertexInputRate input_rate = vk::VertexInputRate::eVertex);

        void add_attribute(uint32_t location, uint32_t binding, vk::Format format, size_t offset);

        [[nodiscard]] const std::vector<vk::VertexInputBindingDescription>& get_bindings() const { return m_bindings; }
        [[nodiscard]] const std::vector<vk::VertexInputAttributeDescription>& get_attributes() const { return m_attributes; }

    private:
        std::vector<vk::VertexInputBindingDescription> m_bindings;
        std::vector<vk::VertexInputAttributeDescription> m_attributes;
    };
}

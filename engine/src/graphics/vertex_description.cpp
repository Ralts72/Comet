#include "vertex_description.h"

namespace Comet {
    void VertexInputDescription::add_binding(const uint32_t binding, const uint32_t stride, const VertexInputRate input_rate) {
        vk::VertexInputBindingDescription bind_desc{};
        bind_desc.binding = binding;
        bind_desc.stride = stride;
        bind_desc.inputRate = Graphics::vertex_input_rate_to_vk(input_rate);
        m_bindings.push_back(bind_desc);
    }

    void VertexInputDescription::add_attribute(const uint32_t location, const uint32_t binding,
        const Format format, const size_t offset) {
        vk::VertexInputAttributeDescription attr_desc{};
        attr_desc.location = location;
        attr_desc.binding = binding;
        attr_desc.format = Graphics::format_to_vk(format);
        attr_desc.offset = static_cast<uint32_t>(offset);
        m_attributes.push_back(attr_desc);
    }
}

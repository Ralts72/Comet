#pragma once
#include "pch.h"
#include <vulkan/vulkan.hpp>
#include "core/logger/logger.h"

namespace Comet {
    struct DeviceFeature {
        const char* name;
        bool required;
    };

    enum class QueueType {
        Graphics, Present, transfer, Compute
    };

    inline std::vector<const char*> build_enabled_list(
        const std::vector<DeviceFeature>& required_items,
        const std::set<std::string>& available_names,
        const std::string& item_type
    ) {
        std::vector<const char*> enabled_list;
        for(const auto& [name, required]: required_items) {
            if(available_names.contains(name) && required) {
                enabled_list.push_back(name);
                LOG_INFO("Enabled {}: {}", item_type, name);
            } else if(required) {
                LOG_WARN("Required {} not supported and skipped: {}", item_type, name);
            }
        }
        return enabled_list;
    }

    inline vk::Viewport get_viewport(const float width, const float height) {
        vk::Viewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = width;
        viewport.height = height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        return viewport;
    }

    inline vk::Rect2D get_scissor(const float width, const float height) {
        vk::Rect2D scissor = {};
        scissor.offset = vk::Offset2D(0, 0);
        scissor.extent = vk::Extent2D(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        return scissor;
    }

    inline bool is_depth_only_format(const vk::Format format) {
        return format == vk::Format::eD16Unorm || format == vk::Format::eD32Sfloat;
    }

    inline bool is_depth_stencil_format(const vk::Format format) {
        return is_depth_only_format(format)
               || format == vk::Format::eD16UnormS8Uint
               || format == vk::Format::eD24UnormS8Uint
               || format == vk::Format::eD32SfloatS8Uint;
    }

    inline uint32_t format_size_in_bytes(const vk::Format format) {
        switch(format) {
            case vk::Format::eR8Unorm: return 1;
            case vk::Format::eR8G8B8A8Unorm: return 4;
            case vk::Format::eB8G8R8A8Unorm: return 4;
            case vk::Format::eR16G16B16A16Sfloat: return 8;
            default: LOG_FATAL("Unsupported format for byte size calculation"); return 0;
        }
    }
}

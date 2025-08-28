#pragma once
#include <vulkan/vulkan.hpp>
#include "convert.h"
#include "common/logger.h"
#include "core/math_utils.h"
#include <vector>
#include <string>
#include <set>

namespace Comet {
    struct DeviceFeature {
        const char* name;
        bool required;
    };

    struct VkSettings {
        Format surface_format = Format::B8G8R8A8_UNORM;
        ImageColorSpace color_space = ImageColorSpace::SrgbNonlinearKHR;
        Format depth_format = Format::D32_SFLOAT;
        vk::PresentModeKHR present_mode = vk::PresentModeKHR::eImmediate;
        uint32_t swapchain_image_count = 3;
        SampleCount msaa_samples = SampleCount::Count1;
    };

    struct ClearValue {
        enum class Type { Color, DepthStencil } type;

        ClearValue() = default;
        explicit ClearValue(const Math::Vec4 clear_color): type(Type::Color), color(clear_color)  {}
        explicit ClearValue(const float depth_value, const uint32_t stencil_value)
            : type(Type::DepthStencil), depth(depth_value), stencil(stencil_value) {}

        Math::Vec4 color{0.0f};
        float depth = 1.0f;
        uint32_t stencil = 0;

        [[nodiscard]] vk::ClearValue vk_value() const {
            vk::ClearValue cv{};
            if (type == Type::Color) {
                cv.color = vk::ClearColorValue{color.x, color.y, color.z, color.w};
            } else {
                cv.depthStencil = vk::ClearDepthStencilValue{depth, stencil};
            }
            return cv;
        }
    };

    namespace Graphics {

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

        inline vk::Extent3D get_extent(const uint32_t width, const uint32_t height, const uint32_t depth = 1) {
            vk::Extent3D extent = {};
            extent.width = width;
            extent.height = height;
            extent.depth = depth;
            return extent;
        }

        inline bool is_depth_only_format(const Format format) {
            return format == Format::D16_UNORM || format == Format::D32_SFLOAT;
        }

        inline bool is_depth_stencil_format(const Format format) {
            return is_depth_only_format(format)
                   || format == Format::D16_UNORM_S8_UINT
                   || format == Format::D24_UNORM_S8_UINT
                   || format == Format::D32_SFLOAT_S8_UINT;
        }

        inline uint32_t format_size_in_bytes(const Format format) {
            switch(format) {
                case Format::R8_UNORM: return 1;
                case Format::R8G8B8A8_UNORM:
                case Format::B8G8R8A8_UNORM:
                    return 4;
                case Format::R16G16B16A16_SFLOAT: return 8;
                default: LOG_FATAL("Unsupported format for byte size calculation");
            }
        }
    }
}

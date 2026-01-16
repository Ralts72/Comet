#pragma once
#include "pch.h"
#include <vulkan/vulkan.hpp>
#include "convert.h"
#include "common/logger.h"
#include "core/math_utils.h"

namespace Comet {
    struct DeviceFeature {
        const char* name;
        bool required;
    };

    struct ClearValue {
        using ColorType = Math::Vec4;

        struct DepthStencilType {
            float depth;
            uint32_t stencil;
        };

        ClearValue() : value(ColorType(0.0f, 0.0f, 0.0f, 0.0f)) {}
        explicit ClearValue(const Math::Vec4& clear_color) : value(clear_color) {}
        explicit ClearValue(const float depth, const uint32_t stencil = 0) : value(DepthStencilType{depth, stencil}) {}
        [[nodiscard]] bool is_color() const { return std::holds_alternative<ColorType>(value); }
        [[nodiscard]] bool is_depth_stencil() const { return std::holds_alternative<DepthStencilType>(value); }

        [[nodiscard]] vk::ClearValue vk_value() const {
            vk::ClearValue cv{};
            std::visit([&cv]<typename T0>(T0&& arg) {
                using T = std::decay_t<T0>;
                if constexpr(std::is_same_v<T, ColorType>) {
                    cv.color = vk::ClearColorValue{arg.x, arg.y, arg.z, arg.w};
                } else if constexpr(std::is_same_v<T, DepthStencilType>) {
                    cv.depthStencil = vk::ClearDepthStencilValue{arg.depth, arg.stencil};
                }
            }, value);
            return cv;
        }

        std::variant<ColorType, DepthStencilType> value;
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

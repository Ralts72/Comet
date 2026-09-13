#pragma once

#include "common/result.h"

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vulkan/vulkan.hpp>

namespace Comet {
    struct GraphicsError {
        std::string message;
        std::optional<vk::Result> result = std::nullopt;

        friend std::ostream& operator<<(std::ostream& stream, const GraphicsError& error) {
            return stream << error.message;
        }
    };

    namespace Graphics {
        template<typename Handle, typename Create,
            typename Dispatch = VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>
        auto create_handle(vk::Device device, std::string_view operation, Create&& create,
            const Dispatch& dispatch = VULKAN_HPP_DEFAULT_DISPATCHER)
            -> Result<vk::UniqueHandle<Handle, Dispatch>, GraphicsError> {
            using Owner = vk::UniqueHandle<Handle, Dispatch>;
            using CreationResult = Result<Owner, GraphicsError>;
            static_assert(std::is_nothrow_invocable_r_v<vk::Result, Create&, Handle*>,
                "Native creation callback must not throw");
            Handle handle{};
            const vk::Result status = create(&handle);
            Owner owner(handle, {device, nullptr, dispatch});
            if(status != vk::Result::eSuccess) {
                return CreationResult::failure(
                    {std::string(operation) + ": " + vk::to_string(status), status});
            }
            if(!owner)
                return CreationResult::failure(
                    {std::string(operation) + ": returned success without a handle", status});
            return CreationResult::success(std::move(owner));
        }
    }
}

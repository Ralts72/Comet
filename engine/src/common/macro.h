#pragma once
#include "../pch.h"

namespace Comet {
#ifdef BUILD_TYPE_DEBUG
#define ASSERT_1_ARGS(x)     \
if (!(x)) {                  \
LOG_FATAL("{}", #x);         \
}

#define ASSERT_2_ARGS(x, msg)     \
if (!(x)) {                       \
LOG_FATAL("{}: {}", #x, msg);     \
}

#define GET_NTH_ARGS(arg1, arg2, arg3, ...) arg3
#define ASSERT_CHOOSER(...)                          \
GET_NTH_ARGS(__VA_ARGS__, ASSERT_2_ARGS, ASSERT_1_ARGS, )

#define ASSERT(...)                               \
do {                                              \
ASSERT_CHOOSER(__VA_ARGS__)(__VA_ARGS__)          \
} while (0)

#define CANT_REACH() ASSERT(false, "won't reach here")
#else
#define ASSERT(...)
#define CANT_REACH(msg)
#endif

#define SDL_CHECK(x)                                    \
do {                                                    \
if (!(x)) LOG_ERROR("SDL error: {}", SDL_GetError());   \
} while (0)

#define VK_CREATE_CHECK(expr) VkCheck((expr), #expr)

    template<typename T>
    T&& VkCheck(vk::ResultValue<T>&& result, std::string_view expr) {
        if(result.result != vk::Result::eSuccess) {
            LOG_FATAL("Vulkan call '{}' failed: {}", expr, vk::to_string(result.result));
            std::abort();
        }
        return std::move(result.value);
    }

    inline vk::Result VkCheckAcquire(vk::Result result, std::string_view expr) {
        if(result != vk::Result::eSuccess &&
           result != vk::Result::eSuboptimalKHR &&
           result != vk::Result::eTimeout) {
            LOG_FATAL("Vulkan call '{}' failed: {}", expr, vk::to_string(result));
        }
        return result;
    }
}

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
}

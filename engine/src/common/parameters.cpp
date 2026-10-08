#include "common/parameters.h"

#include <cmath>

namespace Comet {
    bool valid_parameter_name(const std::string_view name) {
        return !name.empty() && name.size() <= 128 && name.find('\0') == std::string_view::npos;
    }

    bool valid_parameter_value(const ParameterValue& value) {
        return std::visit(
            [](const auto& item) {
                using T = std::remove_cvref_t<decltype(item)>;
                if constexpr(std::is_same_v<T, float>)
                    return std::isfinite(item);
                else if constexpr(std::is_same_v<T, Math::Vec3> || std::is_same_v<T, Math::Vec4>)
                    return Math::is_finite(item);
                else if constexpr(std::is_same_v<T, std::string>)
                    return item.size() <= 4096;
                else
                    return true;
            },
            value);
    }

    bool valid_parameters(const ParameterMap& parameters) {
        if(parameters.size() > 128)
            return false;
        for(const auto& [name, value] : parameters) {
            if(!valid_parameter_name(name) || !valid_parameter_value(value))
                return false;
        }
        return true;
    }
}

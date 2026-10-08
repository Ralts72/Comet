#pragma once

#include "common/export.h"
#include "common/uuid.h"
#include "core/math_utils.h"

#include <map>
#include <string>
#include <string_view>
#include <variant>

namespace Comet {
    using ParameterValue = std::variant<bool, float, Math::Vec3, Math::Vec4, std::string, Uuid>;
    using ParameterMap = std::map<std::string, ParameterValue>;
    [[nodiscard]] COMET_API bool valid_parameter_name(std::string_view name);
    [[nodiscard]] COMET_API bool valid_parameter_value(const ParameterValue& value);
    [[nodiscard]] COMET_API bool valid_parameters(const ParameterMap& parameters);
}

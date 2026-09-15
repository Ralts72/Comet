#pragma once

#include <ostream>
#include <string>
#include <system_error>

namespace Comet {
    struct Error {
        std::string message;
        std::error_code code;

        friend std::ostream& operator<<(std::ostream& stream, const Error& error) {
            return stream << error.message;
        }
    };
}

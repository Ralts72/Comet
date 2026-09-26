#pragma once

#include "common/result.h"

#include <optional>
#include <string>
#include <utility>

namespace CometEditor {
    class ProjectNameDialog {
    public:
        void request(std::string current_name);
        void render();
        [[nodiscard]] std::optional<std::string> take_request() {
            return std::exchange(m_request, std::nullopt);
        }
        void complete(const Comet::Result<void>& result);

    private:
        std::string m_name;
        std::string m_error;
        std::optional<std::string> m_request;
        bool m_active = false;
        bool m_open_requested = false;
        bool m_close_requested = false;
    };
}

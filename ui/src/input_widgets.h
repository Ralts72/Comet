#pragma once

#include <cstddef>
#include <map>
#include <string>

namespace Comet {
    class InputActions;
}

namespace CometUi {
    using Translations = std::map<std::string, std::string, std::less<>>;

    // 只绘制有效配置的两两关系正文；调用方拥有标题、滚动区域和草稿错误。
    void render_binding_relationships(const Comet::InputActions& actions,
        std::size_t selected_action, const Translations& translations = {});
}

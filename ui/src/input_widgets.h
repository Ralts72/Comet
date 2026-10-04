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

    // 在根 ID 域调用；宿主持有原始诊断并决定关闭请求，关闭当帧仍阻断游戏输入。
    [[nodiscard]] bool render_player_input_error(
        std::string& error, bool close_requested, const Translations& translations = {});
}

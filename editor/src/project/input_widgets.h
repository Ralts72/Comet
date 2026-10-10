#pragma once

#include "input/input_actions.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

struct ImVec2;

namespace CometEditor::Ui {
    // 菜单共享输入类型、来源名称和可选控制；选中后的写回由面板负责。
    [[nodiscard]] const char* input_type_name(Comet::InputActions::Type type);
    [[nodiscard]] const char* input_type_label(Comet::InputActions::Type type);
    [[nodiscard]] std::span<const std::string_view> input_sources(Comet::InputActions::Type type);
    [[nodiscard]] const char* input_source_name(std::string_view source);
    [[nodiscard]] std::string input_source_label(std::string_view source);
    [[nodiscard]] std::span<const Comet::InputActions::Control> input_controls(
        std::string_view source);

    // 只绘制有效配置的两两关系正文；调用方拥有标题、滚动区域和草稿错误。
    void render_binding_relationships(
        const Comet::InputActions& actions, std::size_t selected_action);

    void set_next_input_modal_bounds(
        const char* title, bool opening, ImVec2 initial_size, ImVec2 minimum_size);

    // 在根 ID 域调用；宿主持有原始诊断并决定关闭请求，关闭当帧仍阻断游戏输入。
    [[nodiscard]] bool render_player_input_error(std::string& error, bool close_requested);
}
